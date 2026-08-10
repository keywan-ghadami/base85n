import json
from pathlib import Path

import pytest

VECTORS_PATH = Path(__file__).resolve().parents[2] / "testvectors" / "vectors.json"


def load_vectors():
    with open(VECTORS_PATH, encoding="utf-8") as f:
        return json.load(f)


@pytest.fixture(scope="session")
def golden_vectors():
    return load_vectors()
