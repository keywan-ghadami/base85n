import json
from pathlib import Path

import pytest

from base85n import decode, encode

_VECTORS_PATH = Path(__file__).resolve().parents[2] / "testvectors" / "vectors.json"
with open(_VECTORS_PATH, encoding="utf-8") as _f:
    VECTORS = json.load(_f)


@pytest.mark.parametrize("vector", VECTORS, ids=[v["name"] for v in VECTORS])
def test_encode_matches_golden(vector):
    data = bytes.fromhex(vector["input_hex"])
    assert encode(data) == vector["output"]


@pytest.mark.parametrize("vector", VECTORS, ids=[v["name"] for v in VECTORS])
def test_decode_golden_matches_original(vector):
    data = bytes.fromhex(vector["input_hex"])
    assert decode(vector["output"]) == data


def test_vector_file_is_non_trivial():
    assert len(VECTORS) >= 40
