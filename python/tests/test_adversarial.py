# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Adversarial decode vectors (testvectors/adversarial_vectors.json):
multi-byte Unicode input at various positions (character-position vs.
storage-unit discrepancies -- moot for Python's codepoint-indexed str,
but still exercised here for cross-language parity), 0-length DP signals,
invalid/reserved DP signals, and deliberately malformed escaping.
"""

import json
from pathlib import Path

import pytest

from base85n import Base85NDecodeError, Base85NErrorCode, decode

_VECTORS_PATH = Path(__file__).resolve().parents[2] / "testvectors" / "adversarial_vectors.json"
with open(_VECTORS_PATH, encoding="utf-8") as _f:
    VECTORS = json.load(_f)

assert len(VECTORS) >= 15, "expected a non-trivial adversarial vector set"


@pytest.mark.parametrize("vector", VECTORS, ids=[f"{v['category']}:{v['name']}" for v in VECTORS])
def test_adversarial_vector(vector):
    input_str = bytes.fromhex(vector["input_hex"]).decode("utf-8")

    if vector["kind"] == "must_fail":
        with pytest.raises(Base85NDecodeError) as exc_info:
            decode(input_str)
        assert exc_info.value.code == Base85NErrorCode(vector["error_code"])
    elif vector["kind"] == "valid":
        expected = bytes.fromhex(vector["expected_hex"])
        assert decode(input_str) == expected
    else:
        raise AssertionError(f"unknown vector kind {vector['kind']!r}")
