#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Generate differential cases for the other implementations to check against.

The shared vectors in ``testvectors/`` pin down agreed answers for a few dozen
inputs. This generates thousands more, from the shapes that the encoder's
branches actually turn on -- run boundaries, the DP threshold, the analysis
window, donor-character density -- and writes the Python implementation's answer for
each. Another implementation can then be checked line by line against it, which
is how the C, Go, Rust and TypeScript encoders are verified byte-identical after
a change.

Writes two parallel line-oriented files: ``inputs.txt`` holds one hex-encoded
byte string per line, ``expected.txt`` its Base85N encoding.

    python3 tools/gen_differential_cases.py [outdir] [seed]

Consumers, one per implementation -- all four are expected to produce the same
line for the same input, which is what "byte-identical" means here:

    rust/examples/differential.rs    cargo run --release --example differential -- <inputs> <expected>
    c/tools/differential.c           cc -O2 -Ic/include c/src/base85n.c c/tools/differential.c -o d && ./d <inputs> <expected>
    go/cmd/differential              go run ./cmd/differential <inputs> <expected>
    typescript/tools/differential.ts npx tsx tools/differential.ts <inputs> <expected>
"""

from __future__ import annotations

import os
import random
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

try:
    import base85n as B
except ImportError:  # pragma: no cover - a setup problem, not a test failure
    sys.exit(
        "base85n is not importable. Build the bindings first:\n"
        "    pip install -e python/\n"
        "or  maturin develop --release -m python/Cargo.toml"
    )


def build_cases(seed: int) -> list[bytes]:
    rnd = random.Random(seed)
    representable = sorted({ord(c) for c in B.ALPHABET_N} | set(B.R_SET))
    unrepresentable = [b for b in range(256) if b not in representable]
    alpha = bytes(representable)
    cases: list[bytes] = []

    # Short inputs, where every branch of the partial-block handling is reachable.
    for n in range(0, 3):
        cases.extend(bytes(rnd.getrandbits(8) for _ in range(n)) for _ in range(200))
    for b in range(256):
        cases.append(bytes([b]))
        cases.append(bytes([b, b]))
        cases.append(bytes([b]) * 21)

    # Byte pairs, sampled on a stride so both bytes vary independently.
    for a in range(0, 256, 7):
        for b in range(0, 256, 5):
            cases.append(bytes([a, b]))

    # Lengths around the DP threshold (20) and the analysis window (1024 bytes).
    for n in list(range(1, 40)) + [255, 256, 511, 512, 513, 1022, 1023, 1024,
                                   1025, 1026, 2047, 2048, 2049]:
        cases.append(bytes(rnd.choice(alpha) for _ in range(n)))
        cases.append(b"a" * n)
        cases.append(b" " * n)
        cases.append(b"^" * n)
        cases.append((b"^a" * n)[:n])
        cases.append((b" ^" * n)[:n])

    # Zero runs at every length and every offset the tail variant turns on,
    # with both orders and with neighbours that are themselves zero.
    for z in range(0, B.MAX_TAIL_ZEROS + 3):
        for pre in (b"", b"\x81", b"\x81\x82", b"\x81\x82\x83"):
            for post in (b"", b"AB", b"\x00\x00", b"\xff\xfe\xfd"):
                cases.append(pre + b"\x00" * z + post)
                cases.append(bytes(range(16)) + pre + b"\x00" * z + post + bytes(range(16)))
                cases.append(b"the quick brown fox " + pre + b"\x00" * z + post + b" jumps over")
    for _ in range(400):
        parts = []
        for _ in range(rnd.randrange(1, 8)):
            parts.append(b"\x00" * rnd.randrange(1, 40))
            parts.append(bytes(rnd.getrandbits(8) for _ in range(rnd.randrange(1, 6))))
        cases.append(b"".join(parts))

    # Mixtures weighted towards the characters that drive escaping decisions.
    for _ in range(400):
        parts = []
        for _ in range(rnd.randrange(0, 200)):
            r = rnd.random()
            if r < 0.35:
                parts.append(rnd.choice(alpha))
            elif r < 0.5:
                parts.append(rnd.choice(unrepresentable))
            elif r < 0.7:
                parts.append(rnd.choice(b" ,;<>&\"'|\\\t\n\r"))
            elif r < 0.85:
                # The donor characters: the bytes whose meaning depends on
                # which alphabet the encoder picks for the segment.
                parts.append(rnd.choice(b"^@%$?!~#*+=_`{"))
            else:
                parts.append(rnd.choice(b"abcXYZ019"))
        cases.append(bytes(parts))

    # Alternating binary and text, which is what the block-mode lookahead has to
    # get right: it decides how far ahead DP provably cannot apply.
    for _ in range(60):
        chunks = []
        for _ in range(rnd.randrange(1, 40)):
            if rnd.random() < 0.5:
                chunks.append(bytes(rnd.getrandbits(8) for _ in range(rnd.randrange(1, 60))))
            else:
                chunks.append(b"hello world, this is text 0123456789 " * rnd.randrange(1, 4))
        cases.append(b"".join(chunks))

    # Two inputs big enough for the batching paths to run at size.
    cases.append(bytes(rnd.getrandbits(8) for _ in range(50_000)))
    with open(os.path.join(REPO_ROOT, "README.md"), "rb") as fh:
        cases.append(fh.read()[:50_000])

    return cases


def main() -> int:
    outdir = sys.argv[1] if len(sys.argv) > 1 else "."
    seed = int(sys.argv[2]) if len(sys.argv) > 2 else 1
    cases = build_cases(seed)

    os.makedirs(outdir, exist_ok=True)
    inputs_path = os.path.join(outdir, "inputs.txt")
    expected_path = os.path.join(outdir, "expected.txt")
    with open(inputs_path, "w") as fi, open(expected_path, "w") as fe:
        for case in cases:
            fi.write(case.hex() + "\n")
            fe.write(B.encode(case) + "\n")

    print("wrote %d cases (seed %d) to %s and %s"
          % (len(cases), seed, inputs_path, expected_path))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
