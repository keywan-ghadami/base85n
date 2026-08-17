# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""The shared cross-language vectors, run through the built extension module."""

import pytest

from base85n import Base85NDecodeError, decode, encode

from conftest import load_json

VECTORS = load_json("vectors.json")
ADVERSARIAL = load_json("adversarial_vectors.json")


@pytest.mark.parametrize("vector", VECTORS, ids=[v["name"] for v in VECTORS])
def test_encode_matches_golden(vector):
    data = bytes.fromhex(vector["input_hex"])
    assert encode(data) == vector["output"]


@pytest.mark.parametrize("vector", VECTORS, ids=[v["name"] for v in VECTORS])
def test_decode_golden_matches_original(vector):
    data = bytes.fromhex(vector["input_hex"])
    assert decode(vector["output"]) == data


@pytest.mark.parametrize(
    "vector", ADVERSARIAL, ids=[f"{v['category']}:{v['name']}" for v in ADVERSARIAL]
)
def test_adversarial_vector(vector):
    # Each byte becomes the character of the same value -- the identity on
    # ASCII, and what the C ABI, the Rust runner and the TypeScript runner all
    # do. A UTF-8 decode here could not express a vector that is not valid
    # UTF-8, which is most of what a decoder actually receives.
    text = bytes.fromhex(vector["input_hex"]).decode("latin-1")

    if vector["kind"] == "must_fail":
        with pytest.raises(Base85NDecodeError) as exc_info:
            decode(text)
        assert exc_info.value.code == vector["error_code"]
    elif vector["kind"] == "valid":
        assert decode(text) == bytes.fromhex(vector["expected_hex"])
    else:
        raise AssertionError(f"unknown vector kind {vector['kind']!r}")


def test_vector_files_are_non_trivial():
    assert len(VECTORS) >= 40
    assert len(ADVERSARIAL) >= 15
