# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Randomized round trips through the extension module.

The exhaustive property testing lives in `rust/src/tests/roundtrip.rs`; this is
the same idea at the Python boundary, where the conversions on either side are
what could lose bytes.
"""

import random

import pytest

import base85n
from base85n import decode, encode

RSET = bytes(base85n.R_SET)
DONORS = "".join(sorted(set("".join(base85n.PROFILES))))

LENGTHS = [0, 1, 2, 3, 4, 5, 10, 19, 20, 21, 50, 255, 256, 2047, 2048, 2049, 5000]


def _random_bytes(rng: random.Random, length: int) -> bytes:
    return bytes(rng.randrange(0, 256) for _ in range(length))


def _random_mixed(rng: random.Random, length: int) -> bytes:
    pool = (base85n.ALPHABET_N + DONORS).encode() + RSET
    out = bytearray()
    while len(out) < length:
        r = rng.random()
        if r < 0.25:
            out.append(rng.randrange(0, 256))
        elif r < 0.35:
            # A run, so Fill and the segment breaks around it get exercised.
            byte = rng.choice(pool)
            out.extend([byte] * rng.randrange(1, 30))
        else:
            out.append(rng.choice(pool))
    return bytes(out[:length])


@pytest.mark.parametrize("length", LENGTHS)
@pytest.mark.parametrize("kind", ["random", "mixed"])
def test_roundtrip_various_lengths(kind, length):
    for trial in range(5):
        rng = random.Random(f"base85n-roundtrip:{kind}:{length}:{trial}")
        data = _random_bytes(rng, length) if kind == "random" else _random_mixed(rng, length)
        assert decode(encode(data)) == data


def test_roundtrip_every_single_byte_value():
    for b in range(256):
        assert decode(encode(bytes([b]))) == bytes([b])


def test_output_uses_only_alphabet_n():
    rng = random.Random("base85n-alphabet")
    allowed = set(base85n.ALPHABET_N)
    for _ in range(20):
        data = _random_mixed(rng, rng.randrange(0, 3000))
        assert set(encode(data)) <= allowed


def test_whitespace_between_tokens_is_ignored():
    data = b"the quick brown fox jumps over the lazy dog, twice over"
    encoded = encode(data)
    wrapped = "\n".join(encoded[i : i + 16] for i in range(0, len(encoded), 16))
    assert decode(wrapped) == data
