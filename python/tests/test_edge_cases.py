# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

from base85n import (
    MAX_CONSECUTIVE_ESCAPES,
    MAX_DP_OUTPUT_CHARS_PER_SIGNAL,
    MIN_PASSTHROUGH_BYTES,
    decode,
    encode,
)


def test_empty_input():
    assert encode(b"") == ""
    assert decode("") == b""


def test_lengths_one_through_four():
    for length in range(1, 5):
        data = bytes(range(length))
        assert decode(encode(data)) == data


def test_min_passthrough_boundary():
    below = b"a" * (MIN_PASSTHROUGH_BYTES - 1)
    at = b"a" * MIN_PASSTHROUGH_BYTES
    above = b"a" * (MIN_PASSTHROUGH_BYTES + 1)
    for data in (below, at, above):
        assert decode(encode(data)) == data


def test_multi_segment_dp_signal():
    # Pure literals: 1 output char per input byte, so this forces at least
    # two DP signal segments (transformed length > 511).
    data = b"a" * (MAX_DP_OUTPUT_CHARS_PER_SIGNAL * 2 + 50)
    encoded = encode(data)
    assert decode(encoded) == data


def test_consecutive_escape_limit_is_exceeded():
    # A run of escape characters longer than MAX_CONSECUTIVE_ESCAPES,
    # surrounded by enough data to stay above MIN_PASSTHROUGH_BYTES.
    data = b"a" * 25 + b"~" * (MAX_CONSECUTIVE_ESCAPES + 1) + b"b" * 25
    assert decode(encode(data)) == data


def test_unrepresentable_first_byte_falls_back_to_small_block():
    data = bytes([0xFF, 0x00, 0x01, 0x02, 0x03]) + b"a" * 30
    assert decode(encode(data)) == data
    assert decode(encode(bytes([0xFF]))) == bytes([0xFF])


def test_all_byte_values_present():
    data = bytes(range(256))
    assert decode(encode(data)) == data


def test_replacement_char_before_rset_char_same_prefix():
    # ':' (replacement for space, j=0) appears before any space in the same
    # candidate prefix -- exercises the two-pass window/mask discovery from
    # spec Section 6.1 (a naive growing-mask scan would mis-encode the
    # leading ':' as an unescaped literal instead of escaping it).
    data = b":" + b"a" * 19 + b" "
    assert decode(encode(data)) == data
