# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""The binding layer itself: argument types, results, errors and constants."""

import threading

import pytest

import base85n
from base85n import Base85NDecodeError, decode, encode


class TestArgumentTypes:
    def test_encode_accepts_bytes_and_bytearray(self):
        assert encode(b"hello, world!") == encode(bytearray(b"hello, world!"))

    def test_encode_returns_str(self):
        assert isinstance(encode(b"abc"), str)

    def test_decode_returns_bytes_not_a_list_of_ints(self):
        result = decode(encode(b"abc"))
        assert isinstance(result, bytes)
        assert result == b"abc"

    def test_decode_accepts_str_bytes_and_bytearray(self):
        text = encode(b"round trip")
        assert decode(text) == decode(text.encode()) == decode(bytearray(text.encode()))

    @pytest.mark.parametrize("bad", [[1, 2, 3], (1, 2), 5, None, 3.5])
    def test_encode_rejects_things_that_are_not_byte_strings(self, bad):
        with pytest.raises(TypeError):
            encode(bad)

    @pytest.mark.parametrize("bad", [[1, 2, 3], 5, None])
    def test_decode_rejects_things_that_are_not_text_or_bytes(self, bad):
        with pytest.raises(TypeError):
            decode(bad)

    def test_empty_input(self):
        assert encode(b"") == ""
        assert decode("") == b""


class TestErrors:
    def test_error_is_a_value_error(self):
        assert issubclass(Base85NDecodeError, ValueError)

    @pytest.mark.parametrize(
        "text,code",
        [
            ("abc|e", "invalid_character"),
            ("a", "invalid_final_block"),
            ("#####", "undefined_signal"),
        ],
    )
    def test_error_codes(self, text, code):
        with pytest.raises(Base85NDecodeError) as info:
            decode(text)
        assert info.value.code == code

    def test_invalid_character_carries_its_position(self):
        with pytest.raises(Base85NDecodeError) as info:
            decode("abcd|efghi")
        assert info.value.code == "invalid_character"
        assert info.value.position == 4

    def test_position_is_none_where_the_condition_does_not_name_one(self):
        with pytest.raises(Base85NDecodeError) as info:
            decode("a")
        assert info.value.position is None

    def test_non_utf8_bytes_are_an_invalid_character(self):
        with pytest.raises(Base85NDecodeError) as info:
            decode(b"vpA\xff")
        assert info.value.code == "invalid_character"
        assert info.value.position == 3


class TestConstants:
    def test_alphabet_is_the_one_from_section_4(self):
        assert len(base85n.ALPHABET_N) == 85
        assert len(set(base85n.ALPHABET_N)) == 85
        assert base85n.ALPHABET_N.startswith("0123456789abc")

    def test_rset_is_disjoint_from_the_alphabet(self):
        assert len(base85n.R_SET) == 13
        assert not set(base85n.R_SET) & {ord(c) for c in base85n.ALPHABET_N}

    def test_profiles_are_eight_rankings_of_thirteen_alphabet_characters(self):
        assert len(base85n.PROFILES) == base85n.NUM_PROFILES == 8
        for profile in base85n.PROFILES:
            assert len(profile) == len(set(profile)) == 13
            assert set(profile) <= set(base85n.ALPHABET_N)

    def test_signal_ranges(self):
        assert base85n.DP_SIGNAL_BASE == 2**32
        assert base85n.FILL_SIGNAL_BASE == 2**32 + 2**27
        assert base85n.TAIL_SIGNAL_BASE == 2**32 + 2**27 + 2**19
        assert base85n.FUTURE_SIGNAL_BASE == 2**32 + 2**27 + 2**19 + 2**22
        assert base85n.FUTURE_SIGNAL_BASE < 85**5

    def test_thresholds(self):
        assert base85n.MIN_PASSTHROUGH_BYTES == 20
        assert base85n.MIN_FILL_BYTES == 5
        assert base85n.MAX_FILL_BYTES == base85n.MAX_DP_SEGMENT_CHARS == 2048
        assert base85n.MIN_FILL_BYTES <= base85n.MIN_FILL_IN_SEGMENT_BYTES
        assert base85n.MIN_TAIL_ZEROS == 3
        assert base85n.MAX_TAIL_ZEROS == 32

    def test_version_metadata(self):
        assert base85n.SPEC_VERSION == "0.5.0"
        assert base85n.__version__


class TestModes:
    """One case per mode, so a mis-built wheel cannot pass silently."""

    def test_block_mode_expands_four_bytes_to_five_characters(self):
        # Every byte here is outside Alphabet-N and the R-Set, so neither
        # passthrough nor Fill can take any of it.
        data = bytes(range(128, 256)) * 2
        assert len(encode(data)) == len(data) // 4 * 5

    def test_passthrough_is_one_character_per_byte_plus_a_signal(self):
        data = b"the quick brown fox jumps over the lazy dog"
        assert len(encode(data)) == len(data) + 5

    def test_fill_carries_a_run_in_five_characters(self):
        assert len(encode(b"\x00" * base85n.MAX_FILL_BYTES)) == 5

    def test_decoded_output_is_bounded_by_the_fill_ratio(self):
        encoded = encode(b"\x00" * 100_000)
        assert len(decode(encoded)) == 100_000
        assert len(decode(encoded)) / len(encoded) <= base85n.MAX_FILL_BYTES / 5


def test_the_gil_is_released_during_a_long_encode():
    """A second thread has to make progress while one encodes."""
    ticks = []
    stop = threading.Event()

    def tick():
        while not stop.is_set():
            ticks.append(1)

    t = threading.Thread(target=tick)
    t.start()
    try:
        encode(bytes(range(256)) * 40_000)  # ~10 MB, block mode throughout
    finally:
        stop.set()
        t.join()
    assert ticks, "the encoder held the GIL for the whole call"


class TestParallelEncoding:
    """`threads` changes how the work is divided and nothing about the result.

    The format has one canonical encoding, so a thread count that changed the
    output would be a bug in the encoder rather than an option (spec section
    11.3).
    """

    @staticmethod
    def _mixed(length: int) -> bytes:
        import random

        rng = random.Random(20260816)
        out = bytearray()
        while len(out) < length:
            kind = rng.randrange(4)
            if kind == 0:
                out += b"\x00" * rng.randrange(1, 40)
            elif kind == 1:
                out += b'    "key": [1, 2, 3],\n'
            elif kind == 2:
                out += b"the quick brown fox jumps over the lazy dog\n"
            else:
                out += bytes(rng.randrange(256) for _ in range(rng.randrange(1, 24)))
        return bytes(out[:length])

    def test_thread_count_does_not_change_the_output(self):
        data = self._mixed(5 * 1024 * 1024 + 12345)
        expected = encode(data)
        for threads in (0, 1, 2, 3, 8):
            assert encode(data, threads=threads) == expected
            assert decode(encode(data, threads=threads)) == data

    def test_small_inputs_ignore_the_thread_count(self):
        for data in (b"", b"x", b"\x00" * 100_000, bytes(range(256))):
            assert encode(data, threads=8) == encode(data)

    def test_threads_is_keyword_or_positional(self):
        data = b"\x00" * 4096 + b"text goes here"
        assert encode(data, 4) == encode(data, threads=4) == encode(data)
