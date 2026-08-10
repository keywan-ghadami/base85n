# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

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
