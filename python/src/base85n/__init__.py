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

# ---------------------------------------------------------------------
# Constants (Section 6.4)
# ---------------------------------------------------------------------

MAX_CONSECUTIVE_ESCAPES = 3
MAX_DP_OUTPUT_CHARS_PER_SIGNAL = 511
MIN_PASSTHROUGH_BYTES = 20

_BLOCK_SIGNAL_BASE = 1 << 32  # decodedValue threshold: DP signal iff >= 2**32
_MAX_SIGNAL_PAYLOAD = (1 << 22) - 1


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


def _value_to_chars(value: int) -> str:
    digits = [0] * 5
    v = value
    for i in range(4, -1, -1):
        digits[i] = v % 85
        v //= 85
    return "".join(ALPHABET_N_CHARS_STR[d] for d in digits)


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
    n = len(buf)
    i = 0
    while i + 4 <= n:
        out.append(_value_to_chars(int.from_bytes(buf[i : i + 4], "big")))
        i += 4
    remainder = n - i
    if remainder > 0:
        chunk = buf[i:n] + b"\x00" * (4 - remainder)
        chars = _value_to_chars(int.from_bytes(chunk, "big"))
        out.append(chars[: remainder + 1])
    return "".join(out)


def _scan_run(data: bytes, pos: int) -> tuple[int, list[int], int]:
    """Section 6.1, step 1.a (Pass 1 -- Window and Mask Discovery), scanned
    once for a whole representable run rather than once per loop iteration.

    Returns the exclusive end of the maximal representable run starting at
    ``pos`` -- bounded *only* by representability, never by escaping cost or
    the consecutive-escape limit -- together with an occurrence count for
    each of the 13 R-Set characters within it and the corresponding
    window_mask.

    The counts are what make the encoder linear (spec Section 6.6). Pass 1's
    window for a position further inside the same run is a suffix of this
    one, and its mask is the OR of the R-Set bits still present in that
    suffix; keeping counts lets the caller derive that in constant time
    instead of rescanning, which is what made encoding quadratic.
    """
    counts = [0] * len(_RSET_ASCII)
    mask = 0
    i = pos
    n = len(data)
    while i < n:
        b = data[i]
        j = _RSET_INDEX_BY_ASCII.get(b)
        if j is not None:
            counts[j] += 1
            mask |= 1 << j
            i += 1
            continue
        c = chr(b) if b < 128 else None
        if c is not None and c in _CHAR_TO_VALUE:
            i += 1
            continue
        break  # unrepresentable byte: the run ends here

    return i, counts, mask


def _pass2_candidate(
    data: bytes, start: int, end: int, final_mask: int
) -> tuple[int, list[str]]:
    """Section 6.1, step 1.b (Pass 2 -- Boundary Finalization with Fixed
    Mask): walks ``data[start:end]`` against the single, fixed final_mask
    (never modified here) applying Case i/ii/iii and the consecutive-escape
    limit. Returns the candidate prefix's length and its per-source-byte
    transformed "pieces" (1 or 2 characters each).

    Pass 2 stops no later than it consumes, so unlike Pass 1 it needs no
    memoization: its cost is bounded by what the caller then removes from
    the buffer."""
    pieces: list[str] = []
    consecutive_escapes = 0
    i = start
    while i < end:
        b = data[i]
        j = _RSET_INDEX_BY_ASCII.get(b)
        if j is not None:
            # Case i. final_mask is guaranteed to have bit j set: the run
            # scan always counts any R-Set byte still ahead of us.
            pieces.append(_REPLACEMENT_CHARS[j])
            consecutive_escapes = 0
            i += 1
            continue

        c = chr(b)
        repl_j = _REPLACEMENT_INDEX_BY_CHAR.get(c)
        needs_escape = c == _ESCAPE_CHAR or (
            repl_j is not None and (final_mask & (1 << repl_j)) != 0
        )
        if needs_escape:
            # Case ii, against the fixed final_mask.
            consecutive_escapes += 1
            if consecutive_escapes > MAX_CONSECUTIVE_ESCAPES:
                break  # terminate; b and the rest of the run are excluded
            pieces.append("~~" if c == _ESCAPE_CHAR else ("~" + c))
            i += 1
            continue

        # Case iii: plain literal (the run guarantees representability).
        pieces.append(c)
        consecutive_escapes = 0
        i += 1
    return i - start, pieces


def _pack_segments(pieces: list[str], max_len: int = MAX_DP_OUTPUT_CHARS_PER_SIGNAL) -> list[str]:
    """Section 6.1, step 1.d (DP Output Segmentation): greedily packs
    pieces (each 1 or 2 characters) into segments of at most max_len
    characters, closing the current segment *before* adding a piece that
    would push it over the limit -- so a segment boundary never falls
    inside a Case ii 2-character escape pair."""
    segments = []
    current: list[str] = []
    current_len = 0
    for piece in pieces:
        if current_len + len(piece) > max_len:
            segments.append("".join(current))
            current = []
            current_len = 0
        current.append(piece)
        current_len += len(piece)
    if current:
        segments.append("".join(current))
    return segments


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
    counts: list[int] = []
    final_mask = 0

    while pos < n:
        if pos >= run_end:
            # Entering a run we have not scanned yet. Consumption below can
            # step past run_end (the final block-mode branch ignores
            # representability), in which case we land in a later run and
            # scan that one; runs handled this way are disjoint, so the
            # total scanning work stays O(n).
            run_end, counts, final_mask = _scan_run(data, pos)

        cand_len, pieces = _pass2_candidate(data, pos, run_end, final_mask)

        use_dp = False
        segments: list[str] | None = None
        if cand_len >= MIN_PASSTHROUGH_BYTES:
            l_transformed = sum(len(p) for p in pieces)
            segments = _pack_segments(pieces)
            num_segments = len(segments)
            conceptual = num_segments * 5 + l_transformed
            block_len = math.ceil(cand_len / 4) * 5
            if conceptual <= block_len:
                use_dp = True

        if use_dp:
            assert segments is not None
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

        end = pos + consumed
        if end < run_end:
            # Still inside the same run: retire the consumed bytes from the
            # counts so the next iteration's mask covers exactly the
            # remainder, without rescanning it.
            for k in range(pos, end):
                j = _RSET_INDEX_BY_ASCII.get(data[k])
                if j is not None:
                    counts[j] -= 1
                    if counts[j] == 0:
                        final_mask &= ~(1 << j)
        pos = end

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

    Raises :class:`Base85NDecodeError` on any malformed input.
    """
    clean = "".join(c for c in s if c not in _WHITESPACE)
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
        value &= 0xFFFFFFFF  # conceptually "converting to a 32-bit number"
        n_bytes = take - 1
        result += value.to_bytes(4, "big")[:n_bytes]
        i += take

    return bytes(result)
