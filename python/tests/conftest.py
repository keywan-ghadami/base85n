# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Shared paths and fixtures for the binding test suite.

What these tests are for: the `base85n` module is a PyO3 wrapper around the
Rust crate, so the *format* is tested in `rust/src/tests/` and against the
shared vectors in `testvectors/`. What is specific to Python -- argument
types, the exception and its attributes, the constants the module re-exports,
released-GIL behaviour -- is what this suite covers, plus the golden vectors as
an end-to-end check that the built wheel really is the implementation the rest
of the repository agrees on.
"""

import json
from pathlib import Path

import pytest

VECTOR_DIR = Path(__file__).resolve().parents[2] / "testvectors"


def load_json(name: str):
    with open(VECTOR_DIR / name, encoding="utf-8") as f:
        return json.load(f)


@pytest.fixture(scope="session")
def golden_vectors():
    return load_json("vectors.json")
