# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Base85N: a binary-to-text encoding scheme using a single 85-character
alphabet (Alphabet-N) with a Dynamic Passthrough (DP) mode for efficient,
partially human-readable representation of compatible byte sequences.

See the specification in spec/ (base85n-v0.2.0.md) for the full text, in
particular Section 6.1's two-pass ("Pass 1" window/mask discovery,
"Pass 2" boundary finalization) Dynamic Passthrough encoding procedure,
which this package follows exactly.
"""

from __future__ import annotations

import enum
import math
import re

__all__ = [
    "encode",
    "decode",
    "Base85NDecodeError",
    "Base85NErrorCode",
    "ALPHABET_N_CHARS_STR",
    "MIN_PASSTHROUGH_BYTES",
    "MAX_DP_OUTPUT_CHARS_PER_SIGNAL",
    "MAX_CONSECUTIVE_ESCAPES",
]

# ---------------------------------------------------------------------
# Alphabet-N and derived tables (spec Section 4)
# ---------------------------------------------------------------------

ALPHABET_N_CHARS_STR = (
    "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
    ".-:+=^!/*?`_~()[]{}@%$#"
)
assert len(ALPHABET_N_CHARS_STR) == 85

_CHAR_TO_VALUE = {c: i for i, c in enumerate(ALPHABET_N_CHARS_STR)}

_ESCAPE_CHAR = "~"

# R-Set ASCII values, indexed by R-Set index j (Section 4.1).
_RSET_ASCII = (32, 34, 39, 44, 59, 92, 124, 60, 62, 38, 9, 10, 13)
# allowedPassthroughSafeReplacementCharacters[j] (Section 4.2), indexed by j.
_REPLACEMENT_CHARS = (":", "+", "=", "^", "!", "/", "*", "?", "`", "(", ")", "[", "]")
assert len(_RSET_ASCII) == 13 and len(_REPLACEMENT_CHARS) == 13

_RSET_INDEX_BY_ASCII = {b: j for j, b in enumerate(_RSET_ASCII)}
_REPLACEMENT_INDEX_BY_CHAR = {c: j for j, c in enumerate(_REPLACEMENT_CHARS)}

_WHITESPACE = frozenset(" \t\n\r")

# A byte is representable -- i.e. belongs to a Pass 1 run -- iff it is an R-Set
# character or an Alphabet-N character (which includes the escape character and
# every replacement character, regardless of escaping cost).
_REPRESENTABLE_BYTES = bytes(
    sorted({ord(c) for c in ALPHABET_N_CHARS_STR} | set(_RSET_ASCII))
)
_RSET_BYTES = tuple(bytes([b]) for b in _RSET_ASCII)

# Matched at a position, this gives the end of the representable run starting
# there; searched, it finds the next run long enough for Dynamic Passthrough.
# Both are per-byte walks that the encoder used to do in Python, and the `re`
# module does them in C.
_REPRESENTABLE_CLASS = b"[" + re.escape(_REPRESENTABLE_BYTES) + b"]"
_RUN_RE = re.compile(_REPRESENTABLE_CLASS + b"*")

# ---------------------------------------------------------------------
# Constants (Section 6.4)
# ---------------------------------------------------------------------

MAX_CONSECUTIVE_ESCAPES = 3
MAX_DP_OUTPUT_CHARS_PER_SIGNAL = 511
MIN_PASSTHROUGH_BYTES = 20

_BLOCK_SIGNAL_BASE = 1 << 32  # decodedValue threshold: DP signal iff >= 2**32
_MAX_SIGNAL_PAYLOAD = (1 << 22) - 1

# The first position where a Dynamic Passthrough candidate could begin: a
# candidate is never longer than the representable run it starts in, so this is
# the first run that reaches MIN_PASSTHROUGH_BYTES.
_DP_CAPABLE_RUN_RE = re.compile(
    _REPRESENTABLE_CLASS + b"{%d,}" % MIN_PASSTHROUGH_BYTES
)


# ---------------------------------------------------------------------
# Errors (Section 10)
# ---------------------------------------------------------------------


class Base85NErrorCode(enum.Enum):
    """Identifies which of spec Section 10's error conditions was hit."""

    INVALID_CHARACTER = "invalid_character"
    UNEXPECTED_END_OF_STREAM = "unexpected_end_of_stream"
    DANGLING_ESCAPE_CHARACTER = "dangling_escape_character"
    RESERVED_SIGNAL_VALUE = "reserved_signal_value"
    INVALID_PARTIAL_BLOCK_LENGTH = "invalid_partial_block_length"


class Base85NDecodeError(ValueError):
    """Raised by :func:`decode` on malformed input.

    ``code`` identifies the specific error condition (see
    :class:`Base85NErrorCode`); ``position`` is the character offset (after
    inter-token whitespace has been stripped) at which the error was
    detected, or ``None`` if not applicable.
    """

    def __init__(self, code: Base85NErrorCode, message: str, position: int | None = None):
        super().__init__(message)
        self.code = code
        self.position = position


# ---------------------------------------------------------------------
# Base85 digit conversion (Section 8)
# ---------------------------------------------------------------------


_POW85_2 = 85**2
_POW85_3 = 85**3

# Alphabet-N characters for every two-digit base-85 value, so that a group is
# read out as two pairs and a middle digit instead of five successive divisions.
_PAIR_CHARS = [
    ALPHABET_N_CHARS_STR[v // 85] + ALPHABET_N_CHARS_STR[v % 85]
    for v in range(_POW85_2)
]


def _value_to_chars(value: int) -> str:
    """Section 8's ValueToBase85Digits, read out in pairs.

    The obvious loop divides by 85 five times, each division waiting on the one
    before it. Two divisions and three lookups do the same work: value // 85**2
    is head*85 + mid, because 85**3 = 85 * 85**2, so the middle digit falls out
    of a quotient that does not depend on the head.
    """
    q, tail = divmod(value, _POW85_2)
    head, mid = divmod(q, 85)
    return _PAIR_CHARS[head] + ALPHABET_N_CHARS_STR[mid] + _PAIR_CHARS[tail]


def _chars_to_value(chars: str, base_offset: int) -> int:
    value = 0
    for i, c in enumerate(chars):
        v = _CHAR_TO_VALUE.get(c)
        if v is None:
            raise Base85NDecodeError(
                Base85NErrorCode.INVALID_CHARACTER,
                f"invalid character {c!r} at offset {base_offset + i}",
                base_offset + i,
            )
        value = value * 85 + v
    return value


# ---------------------------------------------------------------------
# Encoding (Section 6)
# ---------------------------------------------------------------------


def _process_block_mode(buf: bytes) -> str:
    """Section 6.2 (ProcessWithBlockMode): 4-byte full blocks each become 5
    Alphabet-N characters; a trailing 1-3 byte remainder is zero-padded,
    converted, and truncated to its first 2-4 characters."""
    out = []
    append = out.append
    pair = _PAIR_CHARS
    alphabet = ALPHABET_N_CHARS_STR
    n = len(buf)
    full = n - n % 4
    for i in range(0, full, 4):
        value = (buf[i] << 24) | (buf[i + 1] << 16) | (buf[i + 2] << 8) | buf[i + 3]
        q, tail = divmod(value, _POW85_2)
        head, mid = divmod(q, 85)
        append(pair[head])
        append(alphabet[mid])
        append(pair[tail])
    remainder = n - full
    if remainder > 0:
        chunk = buf[full:n] + b"\x00" * (4 - remainder)
        chars = _value_to_chars(int.from_bytes(chunk, "big"))
        append(chars[: remainder + 1])
    return "".join(out)


def _scan_run(data: bytes, pos: int) -> tuple[int, list[int]]:
    """Section 6.1, step 1.a (Pass 1 -- Window and Mask Discovery), scanned
    once for a whole representable run rather than once per loop iteration.

    Returns the exclusive end of the maximal representable run starting at
    ``pos`` -- bounded *only* by representability, never by escaping cost or
    the consecutive-escape limit -- together with the last offset within the
    run of each of the 13 R-Set characters (-1 where absent).

    Scanning the run once is what makes the encoder linear (spec Section 6.6):
    Pass 1's window for a position further inside the same run is a suffix of
    this one, and rescanning it -- what a literal reading of Section 6.1 does
    -- is what made encoding quadratic.

    The last offsets are what answer a suffix's mask, and they answer it with a
    comparison rather than a recount: R_Char[j] occurs in ``[off, end)`` exactly
    when its last occurrence is at or after ``off``. Both this scan and the
    search for those offsets are byte walks, so they are handed to ``re`` and
    ``bytes.rfind``, which do them in C.
    """
    end = _RUN_RE.match(data, pos).end()
    last = [data.rfind(r, pos, end) for r in _RSET_BYTES]
    return end, last


def _mask_from(last: list[int], off: int) -> int:
    """The window mask for the suffix of a scanned run that starts at ``off``:
    bit j iff R_Char[j] still occurs at or after ``off``."""
    mask = 0
    for j, at in enumerate(last):
        if at >= off:
            mask |= 1 << j
    return mask


def _first_dp_capable_run(data: bytes, pos: int) -> int:
    """The first offset at or after ``pos`` where a Dynamic Passthrough
    candidate could begin, or ``len(data)`` if there is none.

    A DP candidate is never longer than the representable run it starts in, so
    before this offset the encoder is certain to take the block-mode branch.
    Block mode over a whole number of 4-byte groups is exactly the
    concatenation of the per-group results, so that whole stretch can be
    encoded in one call instead of re-entering the mode decision every 4 bytes.
    The output is unchanged.
    """
    match = _DP_CAPABLE_RUN_RE.search(data, pos)
    return match.start() if match else len(data)


def _pass2_candidate(
    data: bytes, start: int, end: int, final_mask: int
) -> tuple[int, int, list[str]]:
    """Section 6.1, step 1.b (Pass 2 -- Boundary Finalization with Fixed
    Mask): walks ``data[start:end]`` against the single, fixed final_mask
    (never modified here) applying Case i/ii/iii and the consecutive-escape
    limit. Returns the candidate prefix's length, the total number of
    transformed characters, and those characters already split into segments.

    Step 1.d's DP Output Segmentation rides along in the same pass. It is
    greedy over the same byte sequence -- close the current segment *before*
    adding a piece that would push it past MAX_DP_OUTPUT_CHARS_PER_SIGNAL, so a
    boundary never falls inside a Case ii escape pair -- which makes it a
    prefix computation like everything else here, and lets it run in this loop
    instead of in a second walk over the pieces.

    Pass 2 stops no later than it consumes, so unlike Pass 1 it needs no
    memoization: its cost is bounded by what the caller then removes from
    the buffer."""
    segments: list[str] = []
    current: list[str] = []
    seg_len = 0
    total = 0
    consecutive_escapes = 0
    i = start
    while i < end:
        b = data[i]
        j = _RSET_INDEX_BY_ASCII.get(b)
        if j is not None:
            # Case i. final_mask is guaranteed to have bit j set: the run
            # scan always sees any R-Set byte still ahead of us.
            piece = _REPLACEMENT_CHARS[j]
            piece_len = 1
            consecutive_escapes = 0
        else:
            c = chr(b)
            repl_j = _REPLACEMENT_INDEX_BY_CHAR.get(c)
            if c == _ESCAPE_CHAR or (
                repl_j is not None and (final_mask & (1 << repl_j)) != 0
            ):
                # Case ii, against the fixed final_mask.
                consecutive_escapes += 1
                if consecutive_escapes > MAX_CONSECUTIVE_ESCAPES:
                    break  # terminate; b and the rest of the run are excluded
                piece = "~" + c
                piece_len = 2
            else:
                # Case iii: plain literal (the run guarantees representability).
                piece = c
                piece_len = 1
                consecutive_escapes = 0

        if seg_len + piece_len > MAX_DP_OUTPUT_CHARS_PER_SIGNAL:
            segments.append("".join(current))
            current = []
            seg_len = 0
        current.append(piece)
        seg_len += piece_len
        total += piece_len
        i += 1

    if current:
        segments.append("".join(current))
    return i - start, total, segments


def encode(data: bytes) -> str:
    """Encode ``data`` into its Base85N string representation.

    Runs in time linear in ``len(data)`` (spec Section 6.6): the Pass 1 scan
    is performed once per representable run and then maintained
    incrementally, and the position is tracked as an index so no input is
    re-copied either."""
    out = []
    n = len(data)
    pos = 0
    run_end = 0
    last: list[int] = []

    while pos < n:
        if pos >= run_end:
            # Entering a run we have not scanned yet. Consumption below can
            # step past run_end (the final block-mode branch ignores
            # representability), in which case we land in a later run and
            # scan that one; runs handled this way are disjoint, so the
            # total scanning work stays O(n).
            run_end, last = _scan_run(data, pos)

        # Skip the mode decision where it cannot change the answer. Until the
        # next run that reaches MIN_PASSTHROUGH_BYTES, the block-mode branch is
        # certain, and block mode over whole 4-byte groups is the concatenation
        # of the per-group results -- so that entire stretch is encoded in one
        # call rather than four bytes at a time. Only worth trying when the
        # current window is itself too short for DP; inside a long
        # representable run the search would return immediately.
        if run_end - pos < MIN_PASSTHROUGH_BYTES:
            limit = _first_dp_capable_run(data, pos)
            batch = ((limit - pos) // 4) * 4
            if batch >= 4:
                out.append(_process_block_mode(data[pos : pos + batch]))
                pos += batch
                # The batch may end mid-run, so the scanned run state no longer
                # describes the new position.
                run_end = 0
                continue

        final_mask = _mask_from(last, pos)
        cand_len, l_transformed, segments = _pass2_candidate(
            data, pos, run_end, final_mask
        )

        use_dp = False
        if cand_len >= MIN_PASSTHROUGH_BYTES:
            conceptual = len(segments) * 5 + l_transformed
            block_len = math.ceil(cand_len / 4) * 5
            if conceptual <= block_len:
                use_dp = True

        if use_dp:
            for seg in segments:
                signal_payload = (final_mask << 9) | len(seg)
                out.append(_value_to_chars(_BLOCK_SIGNAL_BASE + signal_payload))
                out.append(seg)
            consumed = cand_len
        elif cand_len >= 4:
            # DP mode not chosen. Per spec Section 6.1 step 2.b,
            # block-encode only the exact multiple-of-4 leading portion of
            # the candidate; any 1-3 trailing bytes are deferred, unpadded,
            # to the next loop iteration.
            consumed = (cand_len // 4) * 4
            out.append(_process_block_mode(data[pos : pos + consumed]))
        else:
            # Fewer than 4 candidate bytes (or none at all, e.g. because the
            # byte at pos is unrepresentable). This branch is the one that
            # may consume past run_end.
            consumed = min(4, n - pos)
            out.append(_process_block_mode(data[pos : pos + consumed]))

        pos += consumed

    return "".join(out)


# ---------------------------------------------------------------------
# Decoding (Section 7)
# ---------------------------------------------------------------------


def _decode_dp_segment(segment: str, mask: int, base_offset: int) -> bytes:
    """Section 7.1.e: converts transformed DP data back to original bytes
    using the fixed mask for the whole segment."""
    out = bytearray()
    idx = 0
    length = len(segment)
    while idx < length:
        c1 = segment[idx]
        if c1 not in _CHAR_TO_VALUE:
            raise Base85NDecodeError(
                Base85NErrorCode.INVALID_CHARACTER,
                f"invalid character {c1!r} in DP segment at offset {base_offset + idx}",
                base_offset + idx,
            )
        if c1 == _ESCAPE_CHAR:
            esc_offset = base_offset + idx
            idx += 1
            if idx >= length:
                raise Base85NDecodeError(
                    Base85NErrorCode.DANGLING_ESCAPE_CHARACTER,
                    f"escape character at end of DP segment at offset {esc_offset}",
                    esc_offset,
                )
            c2 = segment[idx]
            if c2 not in _CHAR_TO_VALUE:
                raise Base85NDecodeError(
                    Base85NErrorCode.INVALID_CHARACTER,
                    f"invalid character {c2!r} in DP segment at offset {base_offset + idx}",
                    base_offset + idx,
                )
            out.append(ord(c2))
            idx += 1
            continue
        repl_j = _REPLACEMENT_INDEX_BY_CHAR.get(c1)
        if repl_j is not None and (mask & (1 << repl_j)) != 0:
            out.append(_RSET_ASCII[repl_j])
            idx += 1
            continue
        out.append(ord(c1))
        idx += 1
    return bytes(out)


def decode(s: str) -> bytes:
    """Decode a Base85N string ``s`` back into the original bytes.

    Raises :class:`Base85NDecodeError` on any malformed input; ``position`` on
    the error is an offset into the stream after inter-token whitespace has
    been stripped.
    """
    try:
        return _decode_scan(s)
    except Base85NDecodeError:
        # Section 7.1 has the decoder ignore inter-token whitespace. Rather
        # than copy every input to strip characters that a valid stream never
        # contains, take the rejection as the signal: none of the four
        # whitespace characters is in Alphabet-N, and _decode_scan validates
        # every character it consumes, so a stream with whitespace in it can
        # never decode successfully. Only once it has failed is it worth
        # building the filtered copy and decoding again.
        #
        # The retry is on any failure, not just an invalid character:
        # whitespace also shifts the group boundaries after it, so it can
        # equally well surface as a truncated final group or a short DP
        # segment.
        clean = "".join(c for c in s if c not in _WHITESPACE)
        if len(clean) == len(s):
            raise
        return _decode_scan(clean)


def _decode_scan(clean: str) -> bytes:
    """Decode ``clean``, which must already be free of the inter-token
    whitespace Section 7.1 allows."""
    n = len(clean)
    result = bytearray()
    i = 0
    while i < n:
        remaining = n - i
        take = min(5, remaining)

        if take == 5:
            group_offset = i
            value = _chars_to_value(clean[i : i + 5], i)
            i += 5
            if value < _BLOCK_SIGNAL_BASE:
                result += value.to_bytes(4, "big")
                continue

            signal_payload = value - _BLOCK_SIGNAL_BASE
            if signal_payload > _MAX_SIGNAL_PAYLOAD:
                raise Base85NDecodeError(
                    Base85NErrorCode.RESERVED_SIGNAL_VALUE,
                    f"signal payload {signal_payload} exceeds maximum {_MAX_SIGNAL_PAYLOAD}"
                    f" at offset {group_offset}",
                    group_offset,
                )
            mask = (signal_payload >> 9) & 0x1FFF
            length = signal_payload & 0x1FF
            if i + length > n:
                raise Base85NDecodeError(
                    Base85NErrorCode.UNEXPECTED_END_OF_STREAM,
                    f"DP segment declares {length} characters but only {n - i} remain"
                    f" at offset {i}",
                    i,
                )
            segment = clean[i : i + length]
            seg_offset = i
            i += length
            result += _decode_dp_segment(segment, mask, seg_offset)
            continue

        # Fewer than 5 characters remain: this must be the trailing partial
        # block for the whole stream (Section 7.1, last bullet).
        if take <= 1:
            raise Base85NDecodeError(
                Base85NErrorCode.INVALID_PARTIAL_BLOCK_LENGTH,
                f"a single trailing character cannot form a valid partial block at offset {i}",
                i,
            )
        digits = clean[i : i + take] + "#" * (5 - take)
        value = _chars_to_value(digits, i)
        if value >= _BLOCK_SIGNAL_BASE:
            # Spec 7.1: the padded group's value must be below 2**32. The
            # encoder truncates a group whose value already is, and re-padding
            # with '#' raises it by at most 614124, so a group that crosses
            # 2**32 cannot be this format's output. Reducing it modulo 2**32
            # instead would accept several character sequences as encodings of
            # the same bytes.
            raise Base85NDecodeError(
                Base85NErrorCode.INVALID_PARTIAL_BLOCK_LENGTH,
                f"partial final block of {take} characters pads to {value}, which is not"
                f" below 2**32, at offset {i}",
                i,
            )
        n_bytes = take - 1
        result += value.to_bytes(4, "big")[:n_bytes]
        i += take

    return bytes(result)
