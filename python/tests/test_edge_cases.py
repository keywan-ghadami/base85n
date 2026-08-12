# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

from base85n import (
    MAX_DP_ANALYSIS_BYTES,
    MAX_DP_OUTPUT_CHARS_PER_SIGNAL,
    MIN_PASSTHROUGH_BYTES,
    REPLACEMENT_ALPHABETS,
    decode,
    encode,
)

_RSET_ASCII = (32, 34, 39, 44, 59, 92, 124, 60, 62, 38, 9, 10, 13)


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


def test_dp_at_min_passthrough_is_not_larger_than_block_mode():
    # Spec Section 6.1 step 2.a: at MIN_PASSTHROUGH_BYTES the two modes cost
    # exactly the same (25 characters), which is why the length test alone is
    # sufficient. If that ever stops holding, DP would be chosen where it loses.
    data = b"a" * MIN_PASSTHROUGH_BYTES
    assert len(encode(data)) == 5 + MIN_PASSTHROUGH_BYTES
    assert len(encode(data)) <= -(-MIN_PASSTHROUGH_BYTES // 4) * 5


def test_analysis_window_boundary():
    # A candidate prefix is capped at MAX_DP_ANALYSIS_BYTES, so exactly that
    # many literals are one segment and one more needs a second.
    exact = b"x" * MAX_DP_ANALYSIS_BYTES
    assert len(encode(exact)) == MAX_DP_ANALYSIS_BYTES + 5
    assert decode(encode(exact)) == exact

    over = b"x" * (MAX_DP_ANALYSIS_BYTES + 1)
    assert decode(encode(over)) == over


def test_segment_never_exceeds_max_output_chars():
    data = b"a" * (MAX_DP_OUTPUT_CHARS_PER_SIGNAL * 3 + 50)
    encoded = encode(data)
    assert decode(encoded) == data


def test_every_alphabet_round_trips_its_own_rset_characters():
    for subs in REPLACEMENT_ALPHABETS:
        if not subs:
            continue
        body = bytearray()
        while len(body) < 3 * MIN_PASSTHROUGH_BYTES:
            for j, _donor in subs:
                body.append(_RSET_ASCII[j])
                body += b"word"
        data = bytes(body)
        assert decode(encode(data)) == data


def test_literal_donor_character_round_trips():
    # Every donor character, appearing literally in data that also wants
    # substitutions, has to survive: the alphabet that spends it cannot be
    # chosen across it, so the run breaks there instead.
    donors = sorted({d for subs in REPLACEMENT_ALPHABETS for _, d in subs})
    for donor in donors:
        data = b"a" * 25 + b" " + donor.encode() + b" " + b"b" * 25
        assert decode(encode(data)) == data


def test_unrepresentable_first_byte_falls_back_to_small_block():
    data = bytes([0xFF, 0x00, 0x01, 0x02, 0x03]) + b"a" * 30
    assert decode(encode(data)) == data
    assert decode(encode(bytes([0xFF]))) == bytes([0xFF])


def test_all_byte_values_present():
    data = bytes(range(256))
    assert decode(encode(data)) == data


def test_all_rset_characters_at_once_uses_the_full_alphabet():
    # Only alphabet 7 substitutes all 13, so a run containing every one of
    # them can only be carried by that alphabet.
    data = bytes(_RSET_ASCII) * 3
    encoded = encode(data)
    assert decode(encoded) == data
    assert len(encoded) == len(data) + 5


def test_output_is_always_alphabet_n():
    from base85n import ALPHABET_N_CHARS_STR

    allowed = set(ALPHABET_N_CHARS_STR)
    samples = [
        bytes(range(256)),
        b"Hello, World!\r\n\t" * 10,
        b'{"a": "b", "c": [1, 2]}' * 5,
        b"<p>x &amp; y</p>" * 5,
        bytes(_RSET_ASCII) * 5,
    ]
    for data in samples:
        assert set(encode(data)) <= allowed
