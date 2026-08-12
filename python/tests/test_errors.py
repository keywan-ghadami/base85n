# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import pytest

from base85n import Base85NDecodeError, Base85NErrorCode, decode, encode

_MAX_SIGNAL_PAYLOAD = (1 << 13) - 1


def _signal(alphabet: int, length: int) -> str:
    """The 5-character DP signal for an alphabet and a real character length."""
    from base85n import _value_to_chars  # type: ignore[attr-defined]

    return _value_to_chars((1 << 32) + ((alphabet << 10) | (length - 1)))


def test_invalid_character():
    with pytest.raises(Base85NDecodeError) as exc_info:
        decode("0000\x01")  # a full 5-char group with '\x01', not part of Alphabet-N
    assert exc_info.value.code == Base85NErrorCode.INVALID_CHARACTER


def test_invalid_character_inside_dp_segment():
    with pytest.raises(Base85NDecodeError) as exc_info:
        decode(_signal(0, 5) + "ab\x01cd")
    assert exc_info.value.code == Base85NErrorCode.INVALID_CHARACTER


def test_dp_signal_declares_more_than_available():
    with pytest.raises(Base85NDecodeError) as exc_info:
        decode(_signal(0, 100) + "abcde")
    assert exc_info.value.code == Base85NErrorCode.UNEXPECTED_END_OF_STREAM


def test_dp_signal_declares_exactly_one_more_than_available():
    with pytest.raises(Base85NDecodeError) as exc_info:
        decode(_signal(0, 6) + "abcde")
    assert exc_info.value.code == Base85NErrorCode.UNEXPECTED_END_OF_STREAM


def test_length_field_is_biased_by_one():
    # Section 9: the stored value is length - 1, so the smallest segment a
    # signal can name is one character. A decoder that forgets the bias reads
    # zero characters here and then misparses everything after it.
    assert decode(_signal(0, 1) + "a") == b"a"
    with pytest.raises(Base85NDecodeError) as exc_info:
        decode(_signal(0, 1))
    assert exc_info.value.code == Base85NErrorCode.UNEXPECTED_END_OF_STREAM


def test_reserved_signal_payload():
    from base85n import _value_to_chars  # type: ignore[attr-defined]

    # SignalPayload must be <= 2**13 - 1; construct one just above that.
    bad_signal = _value_to_chars((1 << 32) + _MAX_SIGNAL_PAYLOAD + 1)
    with pytest.raises(Base85NDecodeError) as exc_info:
        decode(bad_signal + "a" * 1024)
    assert exc_info.value.code == Base85NErrorCode.RESERVED_SIGNAL_VALUE


def test_maximum_signal_payload_is_still_valid():
    # The adjacent still-legal case, so the two together pin the boundary:
    # payload 2**13 - 1 is alphabet 7 with a 1024-character segment.
    from base85n import _value_to_chars  # type: ignore[attr-defined]

    good = _value_to_chars((1 << 32) + _MAX_SIGNAL_PAYLOAD)
    assert decode(good + "a" * 1024) == b"a" * 1024


def test_invalid_single_character_trailing_group():
    # A valid full 5-char block followed by exactly one leftover character
    # cannot form any valid partial block.
    data = b"abcd"
    encoded = encode(data)
    assert len(encoded) == 5
    with pytest.raises(Base85NDecodeError) as exc_info:
        decode(encoded + "0")
    assert exc_info.value.code == Base85NErrorCode.INVALID_PARTIAL_BLOCK_LENGTH


def test_partial_block_padded_value_must_stay_below_2_32():
    # Spec 7.1: a trailing group is padded with '#' and the result must be
    # below 2**32. These two four-character groups are adjacent -- "%nSb" pads
    # to 2**32 - 2 and "%nSc" to 2**32 + 83 -- so together they pin the
    # boundary rather than just its far side.
    assert decode("%nSb") == b"\xff\xff\xff"
    with pytest.raises(Base85NDecodeError) as exc_info:
        decode("%nSc")
    assert exc_info.value.code == Base85NErrorCode.INVALID_PARTIAL_BLOCK_LENGTH

    # The 2- and 3-character forms take a different branch of the padding.
    for over_limit in ("##", "###"):
        with pytest.raises(Base85NDecodeError) as exc_info:
            decode(over_limit)
        assert exc_info.value.code == Base85NErrorCode.INVALID_PARTIAL_BLOCK_LENGTH


def test_decode_never_raises_unexpected_exception_types():
    garbage_inputs = [
        "\x00\x01\x02",
        "~",
        "#####",
        "0" * 4,
        "0" * 6,
        "".join(chr(i) for i in range(1, 32)),
        _signal(7, 1024),
        _signal(3, 40) + "x" * 5,
    ]
    for g in garbage_inputs:
        try:
            decode(g)
        except Base85NDecodeError:
            pass
