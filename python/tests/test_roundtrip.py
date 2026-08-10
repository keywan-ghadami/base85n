import random

import pytest

from base85n import ALPHABET_N_CHARS_STR, decode, encode

RSET_CHARS = " \"',;\\|<>&\t\n\r"
REPLACEMENT_CHARS = ":+=^!/*?`()[]"

LENGTHS = [0, 1, 2, 3, 4, 5, 10, 19, 20, 21, 50, 100, 255, 256, 511, 512, 513, 1000, 2500]


def _random_bytes(rng: random.Random, length: int) -> bytes:
    return bytes(rng.randrange(0, 256) for _ in range(length))


def _random_mixed(rng: random.Random, length: int) -> bytes:
    pool_chars = ALPHABET_N_CHARS_STR + RSET_CHARS + REPLACEMENT_CHARS + "~"
    out = bytearray()
    for _ in range(length):
        r = rng.random()
        if r < 0.3:
            out.append(rng.randrange(0, 256))
        else:
            out.append(ord(rng.choice(pool_chars)))
    return bytes(out)


@pytest.mark.parametrize("length", LENGTHS)
@pytest.mark.parametrize("kind", ["random", "mixed"])
def test_roundtrip_various_lengths(kind, length):
    for trial in range(5):
        rng2 = random.Random(f"base85n-roundtrip:{kind}:{length}:{trial}")
        data = _random_bytes(rng2, length) if kind == "random" else _random_mixed(rng2, length)
        assert decode(encode(data)) == data


def test_roundtrip_all_single_byte_values():
    for b in range(256):
        data = bytes([b])
        assert decode(encode(data)) == data


def test_roundtrip_pure_alphabet_literals():
    data = (ALPHABET_N_CHARS_STR * 30).encode("latin-1")
    assert decode(encode(data)) == data


def test_roundtrip_escape_heavy():
    rng = random.Random("escape-heavy")
    data = bytes(rng.choice([ord("~"), ord("a"), 0x00, 0xFF]) for _ in range(2000))
    assert decode(encode(data)) == data


def test_roundtrip_rset_heavy():
    rng = random.Random("rset-heavy")
    data = "".join(rng.choice(RSET_CHARS) for _ in range(2000)).encode("latin-1")
    assert decode(encode(data)) == data
