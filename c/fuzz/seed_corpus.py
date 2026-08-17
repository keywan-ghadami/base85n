#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Write a libFuzzer seed corpus into the given directory.

A fuzzer that starts from nothing spends its first hours rediscovering that
input is bytes. These seeds start it at the constructs instead: the shared
test vectors carry every mode the format has, the adversarial vectors carry
the malformed streams the decoder is meant to reject, and a handful of
generated shapes sit exactly on the thresholds the encoder branches on.

    python3 seed_corpus.py corpus/
"""
import hashlib
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def write(outdir, blob):
    if not blob:
        return
    name = hashlib.sha256(blob).hexdigest()[:16]
    with open(os.path.join(outdir, name), "wb") as fh:
        fh.write(blob)


def main():
    outdir = sys.argv[1] if len(sys.argv) > 1 else "corpus"
    os.makedirs(outdir, exist_ok=True)

    # Both vector files, on both sides: a vector's input is a seed for the
    # encoder harnesses and its expected output is a seed for the decoder.
    for name in ("vectors.json", "adversarial_vectors.json"):
        path = os.path.join(ROOT, "testvectors", name)
        if not os.path.exists(path):
            continue
        with open(path, encoding="utf-8") as fh:
            doc = json.load(fh)
        for entry in _walk(doc):
            for key in ("input_hex", "data_hex", "bytes_hex", "expected_hex"):
                if isinstance(entry.get(key), str):
                    try:
                        write(outdir, bytes.fromhex(entry[key]))
                    except ValueError:
                        pass
            for key in ("output", "encoded", "input", "string"):
                if isinstance(entry.get(key), str):
                    write(outdir, entry[key].encode("utf-8", "surrogatepass"))

    # The thresholds the encoder turns on, one seed each side of every one.
    for n in (0, 1, 3, 4, 5, 15, 19, 20, 21, 2047, 2048, 2049):
        write(outdir, b"A" * n)
        write(outdir, b"\x00" * n)
        write(outdir, bytes(range(256)) * (n // 256 + 1))
    write(outdir, b"\x00" * 3 + b"AB")            # tail variant, order 0
    write(outdir, b"AB" + b"\x00" * 3)            # tail variant, order 1
    write(outdir, b"\x00" * 32 + b"XY")           # tail length saturated
    write(outdir, b"hello world, this is text " * 4)
    write(outdir, b'{"a": 1, "b": [2, 3]}\n' * 4)
    write(outdir, b"\t\n\r " * 16)                # the whitespace R-Set
    write(outdir, b"~^?%@+`$#!*.-" * 4)           # every R-Set character

    print("seeded %d files into %s" % (len(os.listdir(outdir)), outdir))


def _walk(doc):
    """Every dict in a vector file, whatever the file's shape is."""
    if isinstance(doc, dict):
        yield doc
        for v in doc.values():
            yield from _walk(v)
    elif isinstance(doc, list):
        for v in doc:
            yield from _walk(v)


if __name__ == "__main__":
    main()
