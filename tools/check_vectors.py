#!/usr/bin/env python3
"""Consistency checks for the shared test vectors in ``testvectors/``.

The five language test suites each verify that *their* implementation agrees
with ``vectors.json`` / ``adversarial_vectors.json``. Nothing in those suites
checks the vector files themselves, so this script does that:

1. every vector file parses, has the expected fields, and has unique names;
2. the ``.json`` and ``.tsv`` forms of each set carry identical data (they are
   two serializations of one source of truth, and it is easy to update one and
   forget the other);
3. the Python reference implementation reproduces every golden vector and
   satisfies every adversarial expectation.

Run it from anywhere: ``python3 tools/check_vectors.py``.
"""

from __future__ import annotations

import json
import os
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VECTOR_DIR = os.path.join(REPO_ROOT, "testvectors")

sys.path.insert(0, os.path.join(REPO_ROOT, "python", "src"))

from base85n import (  # noqa: E402  (path set up above)
    Base85NDecodeError,
    decode,
    encode,
)

VALID_ERROR_CODES = {
    "invalid_character",
    "unexpected_end_of_stream",
    "dangling_escape_character",
    "reserved_signal_value",
    "invalid_partial_block_length",
}

failures: list[str] = []


def fail(msg: str) -> None:
    failures.append(msg)
    print("FAIL: " + msg)


def load_json(name: str):
    with open(os.path.join(VECTOR_DIR, name), encoding="utf-8") as fh:
        return json.load(fh)


def load_tsv(name: str) -> list[dict]:
    with open(os.path.join(VECTOR_DIR, name), encoding="utf-8") as fh:
        lines = fh.read().split("\n")
    while lines and lines[-1] == "":
        lines.pop()
    header = lines[0].split("\t")
    rows = []
    for line in lines[1:]:
        fields = line.split("\t")
        if len(fields) != len(header):
            fail("%s: row %r has %d fields, header has %d"
                 % (name, line, len(fields), len(header)))
            continue
        rows.append(dict(zip(header, fields)))
    return rows


def check_tsv_matches_json(json_name: str, tsv_name: str, columns: list[str]) -> None:
    """The TSV is the JSON with empty strings for absent optional fields."""
    entries = load_json(json_name)
    rows = load_tsv(tsv_name)
    if len(entries) != len(rows):
        fail("%s has %d entries but %s has %d rows"
             % (json_name, len(entries), tsv_name, len(rows)))
        return
    for entry, row in zip(entries, rows):
        for column in columns:
            expected = entry.get(column, "")
            actual = row.get(column, "")
            if expected != actual:
                fail("%s/%s disagree for %r, column %r: %r vs %r"
                     % (json_name, tsv_name, entry.get("name"), column,
                        expected, actual))


def check_unique_names(name: str, entries) -> None:
    seen = set()
    for entry in entries:
        vector_name = entry.get("name")
        if not vector_name:
            fail("%s: an entry has no name" % name)
        elif vector_name in seen:
            fail("%s: duplicate vector name %r" % (name, vector_name))
        seen.add(vector_name)


def check_golden_vectors() -> int:
    entries = load_json("vectors.json")
    check_unique_names("vectors.json", entries)
    for entry in entries:
        name = entry["name"]
        data = bytes.fromhex(entry["input_hex"])
        expected = entry["output"]
        actual = encode(data)
        if actual != expected:
            fail("vectors.json %r: encode mismatch\n  expected %r\n  actual   %r"
                 % (name, expected, actual))
            continue
        try:
            round_tripped = decode(expected)
        except Base85NDecodeError as err:
            fail("vectors.json %r: decode raised %s" % (name, err))
            continue
        if round_tripped != data:
            fail("vectors.json %r: decode mismatch\n  expected %s\n  actual   %s"
                 % (name, data.hex(), round_tripped.hex()))
    return len(entries)


def check_adversarial_vectors() -> int:
    entries = load_json("adversarial_vectors.json")
    check_unique_names("adversarial_vectors.json", entries)
    for entry in entries:
        name = entry["name"]
        kind = entry.get("kind")
        # The vectors are stored as hex so that a byte sequence which is not
        # valid UTF-8 (deliberately, for several of them) survives the file
        # format; decoders take text, so this is the same lossy step every
        # language suite performs.
        raw = bytes.fromhex(entry["input_hex"])
        try:
            text = raw.decode("utf-8")
        except UnicodeDecodeError:
            text = raw.decode("utf-8", errors="replace")

        if kind == "must_fail":
            code = entry.get("error_code")
            if code not in VALID_ERROR_CODES:
                fail("adversarial_vectors.json %r: unknown error_code %r"
                     % (name, code))
            try:
                result = decode(text)
            except Base85NDecodeError as err:
                if err.code.value != code:
                    fail("adversarial_vectors.json %r: expected %s, got %s"
                         % (name, code, err.code.value))
            except Exception as err:  # noqa: BLE001 - that is the point
                fail("adversarial_vectors.json %r: decode raised %s (%s), "
                     "not a Base85NDecodeError"
                     % (name, type(err).__name__, err))
            else:
                fail("adversarial_vectors.json %r: decode succeeded (%s), "
                     "expected %s" % (name, result.hex(), code))
        elif kind == "valid":
            expected_hex = entry.get("expected_hex", "")
            try:
                result = decode(text)
            except Exception as err:  # noqa: BLE001 - that is the point
                fail("adversarial_vectors.json %r: decode raised %s (%s), "
                     "expected success" % (name, type(err).__name__, err))
                continue
            if result.hex() != expected_hex:
                fail("adversarial_vectors.json %r: decoded %s, expected %s"
                     % (name, result.hex(), expected_hex))
        else:
            fail("adversarial_vectors.json %r: unknown kind %r" % (name, kind))
    return len(entries)


def main() -> int:
    check_tsv_matches_json(
        "vectors.json", "vectors.tsv", ["name", "input_hex", "output"]
    )
    check_tsv_matches_json(
        "adversarial_vectors.json",
        "adversarial_vectors.tsv",
        ["name", "category", "kind", "input_hex", "error_code", "expected_hex"],
    )
    golden = check_golden_vectors()
    adversarial = check_adversarial_vectors()

    print("checked %d golden vectors and %d adversarial vectors"
          % (golden, adversarial))
    if failures:
        print("\n%d check(s) failed." % len(failures))
        return 1
    print("all vector consistency checks passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
