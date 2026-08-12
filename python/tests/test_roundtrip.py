# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import random

import pytest

from base85n import ALPHABET_N_CHARS_STR, decode, encode

RSET_CHARS = " \"',;\\|<>&\t\n\r"
# Every character any replacement alphabet spends as a donor (spec 4.2).
DONOR_CHARS = "^@%$?!~#*+=_`{"

LENGTHS = [0, 1, 2, 3, 4, 5, 10, 19, 20, 21, 50, 100, 255, 256, 511, 512, 513, 1000, 2500]


def _random_bytes(rng: random.Random, length: int) -> bytes:
    return bytes(rng.randrange(0, 256) for _ in range(length))


def _random_mixed(rng: random.Random, length: int) -> bytes:
    pool_chars = ALPHABET_N_CHARS_STR + RSET_CHARS + DONOR_CHARS
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


def test_roundtrip_donor_heavy():
    rng = random.Random("donor-heavy")
    pool = [ord(c) for c in DONOR_CHARS] + [ord("a"), ord(" "), 0x00, 0xFF]
    data = bytes(rng.choice(pool) for _ in range(2000))
    assert decode(encode(data)) == data


def test_roundtrip_every_donor_against_every_rset_char():
    for donor in DONOR_CHARS:
        for rset in RSET_CHARS:
            data = (f"aaaa{donor}bbbb{rset}" * 6).encode("latin-1")
            assert decode(encode(data)) == data


def test_roundtrip_rset_heavy():
    rng = random.Random("rset-heavy")
    data = "".join(rng.choice(RSET_CHARS) for _ in range(2000)).encode("latin-1")
    assert decode(encode(data)) == data
