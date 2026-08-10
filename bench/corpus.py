# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Benchmark corpus: definition, download and extraction.

Nothing in the corpus is vendored into this repository. Every sample is
pulled at benchmark time from a pinned package on a public registry
(PyPI or the npm registry) and verified against a recorded SHA-256 of
the *archive*, so a rerun either reproduces the exact same bytes or
fails loudly.

The samples were chosen to cover the input classes that actually travel
over the wire, and to be recognisable rather than synthetic:

  binary   a WebAssembly module, a native ELF shared object, a TrueType
           font -- three unrelated real binary container formats
  json     one widely used open dataset, shipped both pretty-printed and
           minified, so the cost of structural whitespace is visible
  spec     the CommonMark Specification: long-form English technical
           prose with code blocks, the closest reachable stand-in for an
           RFC (see README.md for why an actual RFC is not used)
  image    two public-domain images, one JPEG photograph and one PNG

Short protocol-field samples (names, numbers, phone numbers, ...) are
authored directly in wire_samples.py and need no download.
"""

from __future__ import annotations

import hashlib
import io
import shutil
import sys
import tarfile
import urllib.request
import zipfile
from dataclasses import dataclass
from pathlib import Path

BENCH_DIR = Path(__file__).resolve().parent
CACHE_DIR = BENCH_DIR / "corpus" / "_archives"
CORPUS_DIR = BENCH_DIR / "corpus"


@dataclass(frozen=True)
class Archive:
    """A pinned upstream archive that one or more samples come from."""

    key: str
    url: str
    sha256: str
    kind: str  # "zip" or "tar.gz"


@dataclass(frozen=True)
class Sample:
    """One benchmark input, extracted from an Archive."""

    name: str  # file name written into bench/corpus/
    category: str  # binary | json | spec | image
    archive: str  # Archive.key
    member: str  # path of the file inside the archive
    origin: str  # human-readable provenance, shown in the report


ARCHIVES: dict[str, Archive] = {
    a.key: a
    for a in [
        Archive(
            key="matplotlib",
            url=(
                "https://files.pythonhosted.org/packages/01/75/"
                "6c7ce560e95714a10fcbb3367d1304975a1a3e620f72af28921b796403f3/"
                "matplotlib-3.9.2-cp311-cp311-manylinux_2_17_x86_64."
                "manylinux2014_x86_64.whl"
            ),
            sha256="8912ef7c2362f7193b5819d17dae8629b34a95c58603d781329712ada83f9447",
            kind="zip",
        ),
        Archive(
            key="cffi",
            url=(
                "https://files.pythonhosted.org/packages/ff/6b/"
                "d45873c5e0242196f042d555526f92aa9e0c32355a1be1ff8c27f077fd37/"
                "cffi-1.17.1-cp311-cp311-manylinux_2_17_x86_64."
                "manylinux2014_x86_64.whl"
            ),
            sha256="610faea79c43e44c71e1ec53a554553fa22321b65fae24889706c0a84d4ad86d",
            kind="zip",
        ),
        Archive(
            key="sqljs",
            url="https://registry.npmjs.org/sql.js/-/sql.js-1.14.1.tgz",
            sha256="a82e74c073ad651d20cd361776cc4ffd2863c7f70f7bbcb1740d865714073df1",
            kind="tar.gz",
        ),
        Archive(
            key="world-countries",
            url=(
                "https://registry.npmjs.org/world-countries/-/"
                "world-countries-5.1.0.tgz"
            ),
            sha256="329eb6ef4099ffb590219c9beb634bf489a5e4b10d8ab0ac52a58ebf7b9f8495",
            kind="tar.gz",
        ),
        Archive(
            key="commonmark",
            url=(
                "https://files.pythonhosted.org/packages/3e/e4/"
                "0800832e530c88a8f80cb9e486879ea74257062dfe03a38c1ad535c2860e/"
                "commonmark-0.9.2.tar.gz"
            ),
            sha256="194d693e0c1ac49e83c26455bdeeb2483235e6280313c58b11d0b71c19f58ed1",
            kind="tar.gz",
        ),
    ]
}


SAMPLES: list[Sample] = [
    # --- binaries -------------------------------------------------------
    Sample(
        name="sql-wasm.wasm",
        category="binary",
        archive="sqljs",
        member="package/dist/sql-wasm.wasm",
        origin="SQLite compiled to WebAssembly (npm sql.js 1.14.1)",
    ),
    Sample(
        name="_cffi_backend.so",
        category="binary",
        archive="cffi",
        member="_cffi_backend.cpython-311-x86_64-linux-gnu.so",
        origin="native CPython extension, ELF x86-64 (PyPI cffi 1.17.1)",
    ),
    Sample(
        name="DejaVuSans.ttf",
        category="binary",
        archive="matplotlib",
        member="matplotlib/mpl-data/fonts/ttf/DejaVuSans.ttf",
        origin="DejaVu Sans TrueType font (PyPI matplotlib 3.9.2)",
    ),
    # --- JSON -----------------------------------------------------------
    Sample(
        name="countries.json",
        category="json",
        archive="world-countries",
        member="package/countries.json",
        origin="world-countries 5.1.0 dataset, pretty-printed",
    ),
    Sample(
        name="countries.min.json",
        category="json",
        archive="world-countries",
        member="package/dist/countries.json",
        origin="world-countries 5.1.0 dataset, minified",
    ),
    # --- specification text ---------------------------------------------
    Sample(
        name="commonmark-spec.txt",
        category="spec",
        archive="commonmark",
        member="commonmark-0.9.2/spec.txt",
        origin="the CommonMark Specification (PyPI commonmark 0.9.2)",
    ),
    # --- images ---------------------------------------------------------
    Sample(
        name="grace_hopper.jpg",
        category="image",
        archive="matplotlib",
        member="matplotlib/mpl-data/sample_data/grace_hopper.jpg",
        origin="US Navy photograph, public domain (PyPI matplotlib 3.9.2)",
    ),
    Sample(
        name="minduka_present.png",
        category="image",
        archive="matplotlib",
        member="matplotlib/mpl-data/sample_data/Minduka_Present_Blue_Pack.png",
        origin="Openclipart drawing, public domain (PyPI matplotlib 3.9.2)",
    ),
]


def _download(archive: Archive) -> Path:
    """Fetch an archive into the cache, verifying its SHA-256."""
    CACHE_DIR.mkdir(parents=True, exist_ok=True)
    dest = CACHE_DIR / f"{archive.key}{'.whl' if archive.kind == 'zip' else '.tar.gz'}"

    if dest.exists() and _sha256(dest) == archive.sha256:
        return dest

    print(f"  downloading {archive.key} ...", file=sys.stderr)
    with urllib.request.urlopen(archive.url, timeout=180) as resp:
        blob = resp.read()

    got = hashlib.sha256(blob).hexdigest()
    if got != archive.sha256:
        raise SystemExit(
            f"SHA-256 mismatch for {archive.url}\n"
            f"  expected {archive.sha256}\n  got      {got}"
        )
    dest.write_bytes(blob)
    return dest


def _sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def _extract(archive: Archive, path: Path, member: str) -> bytes:
    if archive.kind == "zip":
        with zipfile.ZipFile(path) as zf:
            return zf.read(member)
    with tarfile.open(path, "r:gz") as tf:
        fh = tf.extractfile(member)
        if fh is None:
            raise SystemExit(f"{member} is not a regular file in {archive.key}")
        return fh.read()


def ensure_corpus(quiet: bool = False) -> list[tuple[Sample, Path]]:
    """Materialise every sample under bench/corpus/ and return the paths."""
    CORPUS_DIR.mkdir(parents=True, exist_ok=True)
    out: list[tuple[Sample, Path]] = []
    archive_paths: dict[str, Path] = {}

    for sample in SAMPLES:
        target = CORPUS_DIR / sample.name
        if not target.exists():
            archive = ARCHIVES[sample.archive]
            if sample.archive not in archive_paths:
                archive_paths[sample.archive] = _download(archive)
            data = _extract(archive, archive_paths[sample.archive], sample.member)
            target.write_bytes(data)
            if not quiet:
                print(f"  extracted {sample.name} ({len(data)} bytes)", file=sys.stderr)
        out.append((sample, target))

    return out


def clean() -> None:
    if CORPUS_DIR.exists():
        shutil.rmtree(CORPUS_DIR)


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "clean":
        clean()
        print("corpus removed")
    else:
        for sample, path in ensure_corpus():
            print(f"{path.stat().st_size:>9}  {sample.category:<7} {sample.name}")
