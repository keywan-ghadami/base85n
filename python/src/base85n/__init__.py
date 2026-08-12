# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Base85N: a binary-to-text encoding scheme using a single 85-character
alphabet (Alphabet-N) with a Dynamic Passthrough (DP) mode for efficient,
partially human-readable representation of compatible byte sequences.

See the specification in spec/ (base85n-v0.3.0.md) for the full text, in
particular Section 4.2's eight replacement alphabets and Section 6.1's
single-scan Dynamic Passthrough prefix identification, which this package
follows exactly.
"""

from __future__ import annotations

import enum
import re

__all__ = [
    "encode",
    "decode",
    "Base85NDecodeError",
    "Base85NErrorCode",
    "ALPHABET_N_CHARS_STR",
    "REPLACEMENT_ALPHABETS",
    "MIN_PASSTHROUGH_BYTES",
    "MAX_DP_ANALYSIS_BYTES",
    "MAX_DP_OUTPUT_CHARS_PER_SIGNAL",
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

# R-Set ASCII values, indexed by R-Set index j (Section 4.1).
_RSET_ASCII = (32, 34, 39, 44, 59, 92, 124, 60, 62, 38, 9, 10, 13)

_WHITESPACE = frozenset(" \t\n\r")

# ---------------------------------------------------------------------
# Replacement alphabets (Section 4.2)
# ---------------------------------------------------------------------

# Each alphabet is a tuple of (R-Set index, donor character) substitutions.
# Under alphabet a, R_Char[j] is written as its donor character, the donor
# character itself becomes unrepresentable, and every other Alphabet-N
# character represents itself. Each is therefore injective, which is why
# version 0.3.0 needs no escape character.
REPLACEMENT_ALPHABETS: tuple[tuple[tuple[int, str], ...], ...] = (
    (),  # 0 none
    ((0, "^"), (11, "@"), (12, "%"), (10, "$")),  # 1 text
    ((0, "^"), (11, "@"), (3, "%"), (1, "$"), (2, "?"), (4, "!")),  # 2 prose
    (
        (0, "^"), (11, "@"), (7, "%"), (8, "$"),
        (9, "?"), (1, "!"), (2, "~"), (3, "{"),
    ),  # 3 markup
    ((0, "^"), (11, "@"), (1, "%"), (3, "$"), (5, "?"), (12, "!")),  # 4 json
    (
        (0, "^"), (11, "@"), (3, "%"), (4, "$"),
        (1, "?"), (2, "!"), (10, "~"), (8, "`"),
    ),  # 5 code
    (
        (0, "^"), (11, "@"), (6, "%"), (5, "$"),
        (1, "?"), (2, "!"), (9, "~"), (4, "#"),
    ),  # 6 shell
    (
        (0, "^"), (11, "@"), (12, "%"), (10, "$"), (3, "?"), (4, "!"),
        (1, "~"), (2, "#"), (7, "*"), (8, "+"), (9, "="), (6, "_"), (5, "`"),
    ),  # 7 full
)
assert len(REPLACEMENT_ALPHABETS) == 8
for _subs in REPLACEMENT_ALPHABETS:
    assert len({j for j, _ in _subs}) == len(_subs), "duplicate R-Set index"
    assert len({d for _, d in _subs}) == len(_subs), "duplicate donor character"
    assert all(d in _CHAR_TO_VALUE for _, d in _subs), "donor outside Alphabet-N"


def _build_encode_tables() -> tuple[list[bytes], list[bytes], list[bytes]]:
    """Per alphabet: the byte->character translation, the set of representable
    input bytes, and that set as a regular-expression character class."""
    xlats, members, classes = [], [], []
    alphabet_bytes = {ord(c) for c in ALPHABET_N_CHARS_STR}
    for subs in REPLACEMENT_ALPHABETS:
        donors = {ord(d) for _, d in subs}
        table = bytearray(256)
        ok = set()
        for b in alphabet_bytes - donors:
            table[b] = b  # an Alphabet-N character represents itself
            ok.add(b)
        for j, d in subs:
            table[_RSET_ASCII[j]] = ord(d)
            ok.add(_RSET_ASCII[j])
        xlats.append(bytes(table))
        members.append(bytes(sorted(ok)))
        classes.append(b"[" + re.escape(bytes(sorted(ok))) + b"]")
    return xlats, members, classes


_XLAT, _MEMBERS, _CLASSES = _build_encode_tables()

# Per alphabet, the reverse map used when decoding: a 256-byte translation
# that turns each donor character back into its R-Set character and leaves
# every other character alone.
_UNXLAT: list[bytes] = []
for _subs in REPLACEMENT_ALPHABETS:
    _table = bytearray(range(256))
    for _j, _d in _subs:
        _table[ord(_d)] = _RSET_ASCII[_j]
    _UNXLAT.append(bytes(_table))

# ---------------------------------------------------------------------
# Constants (Section 6.4)
# ---------------------------------------------------------------------

MAX_DP_ANALYSIS_BYTES = 1024
MAX_DP_OUTPUT_CHARS_PER_SIGNAL = 1024
MIN_PASSTHROUGH_BYTES = 20

_BLOCK_SIGNAL_BASE = 1 << 32  # decodedValue threshold: DP signal iff >= 2**32
_MAX_SIGNAL_PAYLOAD = (1 << 13) - 1

# Matched at a position, this gives the end of the run representable under that
# alphabet; searched, it finds the next run long enough for Dynamic Passthrough.
# Both are per-byte walks, handed to `re` so they run in C.
_RUN_RE = [re.compile(c + b"*") for c in _CLASSES]


# ---------------------------------------------------------------------
# Errors (Section 10)
# ---------------------------------------------------------------------


class Base85NErrorCode(enum.Enum):
    """Identifies which of spec Section 10's error conditions was hit."""

    INVALID_CHARACTER = "invalid_character"
    UNEXPECTED_END_OF_STREAM = "unexpected_end_of_stream"
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


def encode(data: bytes) -> str:
    """Encode ``data`` into its Base85N string representation.

    Runs in time linear in ``len(data)`` (spec Section 6.6). The eight scans of
    Section 6.1 step 1 are not redone per iteration: ``stop[a]`` holds the
    offset of the first byte not representable under alphabet ``a``, and is
    recomputed only once the position has reached it. Each of the eight offsets
    therefore advances monotonically across the input.

    Consecutive block-mode iterations are accumulated and converted in one
    call rather than four bytes at a time. That is not a change of trajectory:
    the loop still visits exactly the positions Section 6.1 visits, and every
    block-mode consumption is a whole number of 4-byte groups, so the
    concatenation of the per-iteration results is the block-mode encoding of
    the accumulated range.
    """
    out = []
    n = len(data)
    pos = 0
    block_start = -1  # start of the pending run of block-mode bytes, or -1
    # stop[a] < 0 means "not yet scanned from a position at or before pos".
    stop = [-1] * 8

    while pos < n:
        best_len = -1
        best_alphabet = 0
        for a in range(8):
            if pos >= stop[a]:
                stop[a] = _RUN_RE[a].match(data, pos).end()
            length = stop[a] - pos
            if length > MAX_DP_ANALYSIS_BYTES:
                length = MAX_DP_ANALYSIS_BYTES
            # Strictly greater keeps the smallest identifier on a tie, which
            # Section 6.1 step 1 requires.
            if length > best_len:
                best_len = length
                best_alphabet = a

        if best_len >= MIN_PASSTHROUGH_BYTES:
            # Section 6.1 step 2.a. The length test implies the size test at
            # MIN_PASSTHROUGH_BYTES = 20 (25 characters either way), and DP only
            # gains from there, so no separate comparison is needed.
            if block_start >= 0:
                out.append(_process_block_mode(data[block_start:pos]))
                block_start = -1
            segment = data[pos : pos + best_len].translate(_XLAT[best_alphabet])
            payload = (best_alphabet << 10) | (best_len - 1)
            out.append(_value_to_chars(_BLOCK_SIGNAL_BASE + payload))
            out.append(segment.decode("ascii"))
            pos += best_len
            continue

        # Section 6.1 step 2.b, block-mode branch. A candidate of 4 bytes or
        # more gives up only its whole 4-byte groups; the 1-3 byte remainder
        # stays in the buffer for the next iteration rather than being padded
        # here, which would misalign everything after it.
        if best_len >= 4:
            consumed = (best_len // 4) * 4
        else:
            consumed = min(4, n - pos)
        if block_start < 0:
            block_start = pos
        pos += consumed

    if block_start >= 0:
        out.append(_process_block_mode(data[block_start:pos]))

    return "".join(out)


# ---------------------------------------------------------------------
# Decoding (Section 7)
# ---------------------------------------------------------------------


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
            alphabet = (signal_payload >> 10) & 0x7
            # Section 9: the length field is biased by one, so the shortest
            # segment a signal can declare is 1 character and the longest 1024.
            length = (signal_payload & 0x3FF) + 1
            if i + length > n:
                raise Base85NDecodeError(
                    Base85NErrorCode.UNEXPECTED_END_OF_STREAM,
                    f"DP segment declares {length} characters but only {n - i} remain"
                    f" at offset {i}",
                    i,
                )
            segment = clean[i : i + length]
            for k, c in enumerate(segment):
                if c not in _CHAR_TO_VALUE:
                    raise Base85NDecodeError(
                        Base85NErrorCode.INVALID_CHARACTER,
                        f"invalid character {c!r} in DP segment at offset {i + k}",
                        i + k,
                    )
            # Section 7.1.e: one character in, one byte out, with no state
            # carried between characters -- so the whole segment is a single
            # table lookup per character.
            result += segment.encode("ascii").translate(_UNXLAT[alphabet])
            i += length
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
