# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Finds the corpus, which lives in binary2textbench.

`bench/corpus.py` and `bench/wire_samples.py` used to be here. Base91z carried
a second copy of the same generator, and two corpus generators that are
supposed to agree are a bug waiting to happen -- so there is one now, in
binary2textbench, which also measures this codec against Base64, classic
basE91, Ascii85, Base91z and Base94Max on the same bytes.

The scripts here import the modules exactly as before; this is what puts them
on the path. Run `bench/fetch.sh` first, or set B2TB_DIR to a checkout you
already have.
"""

from __future__ import annotations

import os
import sys
from pathlib import Path

BENCH_DIR = Path(__file__).resolve().parent


def corpus_dir() -> Path:
    """The directory holding corpus.py, wire_samples.py and manifest.py."""
    override = os.environ.get("B2TB_DIR")
    root = Path(override) if override else BENCH_DIR / ".b2tb"
    d = root / "corpus"
    if not (d / "corpus.py").exists():
        raise SystemExit(
            f"no corpus generator at {d}\n"
            f"  run: bench/fetch.sh\n"
            f"  or:  B2TB_DIR=/path/to/binary2textbench {' '.join(sys.argv)}"
        )
    return d


def on_path() -> Path:
    """Put the corpus modules where `import corpus` will find them."""
    d = corpus_dir()
    if str(d) not in sys.path:
        sys.path.insert(0, str(d))
    return d
