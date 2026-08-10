import pytest

from base85n import Base85NDecodeError, Base85NErrorCode, decode, encode


def test_invalid_character():
    with pytest.raises(Base85NDecodeError) as exc_info:
        decode("0000\x01")  # a full 5-char group with '\x01', not part of Alphabet-N
    assert exc_info.value.code == Base85NErrorCode.INVALID_CHARACTER


def test_dp_signal_declares_more_than_available():
    # Craft a valid DP signal (mask=0, length=100) with no following data.
    data = b"a" * 25
    encoded = encode(data)
    # Corrupt: find any encoded form isn't easy by hand, so instead build a
    # minimal valid DP signal directly by encoding then truncating a
    # multi-segment payload to strip trailing segment data.
    long_data = b"a" * 600
    long_encoded = encode(long_data)
    truncated = long_encoded[:10]  # signal + a few chars, well short of declared length
    with pytest.raises(Base85NDecodeError) as exc_info:
        decode(truncated)
    assert exc_info.value.code == Base85NErrorCode.UNEXPECTED_END_OF_STREAM


def test_dangling_escape_at_end_of_dp_segment():
    # Build a real DP-encoded string containing R-Set data, then verify a
    # segment ending in a lone '~' is rejected.
    data = b"a" * 19 + b"~" + b"a" * 19  # ensure DP mode with an escape pair
    encoded = encode(data)
    assert decode(encoded) == data
    # Manually construct a malformed segment: signal for mask=0, length=1,
    # followed by a single '~' with nothing after it.
    from base85n import _value_to_chars  # type: ignore[attr-defined]

    signal = _value_to_chars((1 << 32) + (0 << 9) + 1)
    malformed = signal + "~"
    with pytest.raises(Base85NDecodeError) as exc_info:
        decode(malformed)
    assert exc_info.value.code == Base85NErrorCode.DANGLING_ESCAPE_CHARACTER


def test_reserved_signal_payload():
    from base85n import _value_to_chars  # type: ignore[attr-defined]

    # SignalPayload must be <= 2**22 - 1; construct one just above that.
    bad_value = (1 << 32) + (1 << 22)
    bad_signal = _value_to_chars(bad_value)
    with pytest.raises(Base85NDecodeError) as exc_info:
        decode(bad_signal)
    assert exc_info.value.code == Base85NErrorCode.RESERVED_SIGNAL_VALUE


def test_invalid_single_character_trailing_group():
    # A valid full 5-char block followed by exactly one leftover character
    # cannot form any valid partial block.
    data = b"abcd"
    encoded = encode(data)
    assert len(encoded) == 5
    with pytest.raises(Base85NDecodeError) as exc_info:
        decode(encoded + "0")
    assert exc_info.value.code == Base85NErrorCode.INVALID_PARTIAL_BLOCK_LENGTH


def test_decode_never_raises_unexpected_exception_types():
    garbage_inputs = [
        "\x00\x01\x02",
        "~",
        "#####",
        "0" * 4,
        "0" * 6,
        "".join(chr(i) for i in range(1, 32)),
    ]
    for g in garbage_inputs:
        try:
            decode(g)
        except Base85NDecodeError:
            pass
