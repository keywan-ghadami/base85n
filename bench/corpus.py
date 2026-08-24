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
  archive  an uncompressed tar of a real source release: structured
           binary with the long zero runs tar pads its blocks with
  json     one widely used open dataset, shipped both pretty-printed and
           minified, so the cost of structural whitespace is visible
  code     source as it is actually shipped: a large JavaScript library,
           a generated CSS bundle, and a Python module
  spec     the CommonMark Specification: long-form English technical
           prose with code blocks, the closest reachable stand-in for an
           RFC (see README.md for why an actual RFC is not used)
  prose    a real project changelog in Markdown
  image    two public-domain images, one JPEG photograph and one PNG

The corpus comes in two groups. The *core* group is the thirteen files
above: small enough to fetch and measure in seconds, and chosen one
sample per input class. The *silesia* group is the Silesia compression
corpus -- twelve files, 202 MiB, the set compression work has been
reported against since 2003. It is here because thirteen hand-picked
files are a weak basis for a claim about "real data": Silesia was
assembled by somebody else, for somebody else's benchmark, and it
contains input classes the core group has none of (a star catalogue, a
medical image, a chemical database, a dictionary).

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

    name: str  # file name written into bench/corpus/<subdir>/
    category: str  # binary | archive | json | code | spec | prose | image | ...
    archive: str  # Archive.key
    member: str  # path of the file inside the archive, or WHOLE_TAR
    origin: str  # human-readable provenance, shown in the report
    group: str = "core"  # core | silesia
    # Set when `member` is itself a zip holding exactly one file, which is
    # how the Silesia corpus is published: the name of that file.
    inner: str = ""

    @property
    def subdir(self) -> str:
        """Where the sample is materialised, relative to bench/corpus/."""
        return "" if self.group == "core" else self.group


GROUPS = ("core", "silesia")


# A sample that is the archive itself, decompressed: the tar stream inside a
# .tar.gz. Real, deterministic, and the only member of the corpus with the
# long zero runs a block-padded container format produces.
WHOLE_TAR = "@tar"


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
            key="lodash",
            url="https://registry.npmjs.org/lodash/-/lodash-4.17.21.tgz",
            sha256="6a087ac9e5702a0c9d60fbcd48696012646ec8df1491dea472b150e79fcaf804",
            kind="tar.gz",
        ),
        Archive(
            key="bootstrap",
            url="https://registry.npmjs.org/bootstrap/-/bootstrap-5.3.3.tgz",
            sha256="38cee936dbd80138de6775683149f22e9226fc2d654392337a921f53000c789e",
            kind="tar.gz",
        ),
        Archive(
            key="requests",
            url=(
                "https://files.pythonhosted.org/packages/63/70/"
                "2bf7780ad2d390a8d301ad0b550f1581eadbd9a20f896afe06353c2a2913/"
                "requests-2.32.3.tar.gz"
            ),
            sha256="55365417734eb18255590a9ff9eb97e9e1da868d4ccd6402399eaf68af20a760",
            kind="tar.gz",
        ),
        # The Silesia corpus is not published on a package registry. It is
        # taken from the Go module proxy's immutable snapshot of the
        # SilesiaCorpus repository -- module zips are content-addressed and
        # never rewritten once served, which is the same guarantee a pinned
        # release artefact gives, and the pseudo-version names the commit.
        # The SHA-256 below is of that zip; the twelve files inside it are
        # each a single-file zip, and their lengths match the corpus as
        # published (Section "The corpus" in README.md lists them).
        Archive(
            key="silesia",
            url=(
                "https://proxy.golang.org/github.com/!milosz!krajewski/"
                "!silesia!corpus/@v/v0.0.0-20180902151707-3f3fa2cdbbb3.zip"
            ),
            sha256="25597f3a14e8655703b427df933c2bed58102c199bbfd0ba11074ca7a889d53c",
            kind="zip",
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
    # --- archives -------------------------------------------------------
    Sample(
        name="requests-2.32.3.tar",
        category="archive",
        archive="requests",
        member=WHOLE_TAR,
        origin="the requests 2.32.3 source release, gzip removed (PyPI)",
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
    # --- source code ----------------------------------------------------
    Sample(
        name="lodash.js",
        category="code",
        archive="lodash",
        member="package/lodash.js",
        origin="the lodash 4.17.21 library, unminified (npm)",
    ),
    Sample(
        name="bootstrap.css",
        category="code",
        archive="bootstrap",
        member="package/dist/css/bootstrap.css",
        origin="the Bootstrap 5.3.3 CSS bundle (npm)",
    ),
    Sample(
        name="requests-models.py",
        category="code",
        archive="requests",
        member="requests-2.32.3/src/requests/models.py",
        origin="requests 2.32.3, src/requests/models.py (PyPI)",
    ),
    # --- specification text ---------------------------------------------
    Sample(
        name="commonmark-spec.txt",
        category="spec",
        archive="commonmark",
        member="commonmark-0.9.2/spec.txt",
        origin="the CommonMark Specification (PyPI commonmark 0.9.2)",
    ),
    # --- prose ----------------------------------------------------------
    Sample(
        name="requests-history.md",
        category="prose",
        archive="requests",
        member="requests-2.32.3/HISTORY.md",
        origin="the requests 2.32.3 changelog, Markdown (PyPI)",
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


# The Silesia corpus: twelve files, 202 MiB, unchanged since 2003 and
# reported against by most of the compression literature. Every member of
# the pinned archive is a single-file zip named after the sample, so the
# twelve entries differ only in the three columns below.
SILESIA_ROOT = ("github.com/MiloszKrajewski/SilesiaCorpus"
                "@v0.0.0-20180902151707-3f3fa2cdbbb3/")

SILESIA: list[tuple[str, str, str]] = [
    ("dickens", "prose", "the collected works of Charles Dickens, plain text"),
    ("mozilla", "archive",
     "tarred executables of Mozilla 1.0, Tru64 UNIX edition"),
    ("mr", "image", "a medical magnetic-resonance image"),
    ("nci", "data", "a chemical database of structures, text records"),
    ("ooffice", "binary", "a shared library from OpenOffice.org 1.01"),
    ("osdb", "data",
     "a MySQL sample database from the Open Source Database Benchmark"),
    ("reymont", "document",
     "the book Chłopi by Władysław Reymont, as a PDF"),
    ("samba", "archive", "tarred source code of Samba 2-2.3"),
    ("sao", "binary", "the SAO star catalogue, fixed-width binary records"),
    ("webster", "prose", "the 1913 Webster Unabridged Dictionary, HTML"),
    ("x-ray", "image", "an X-ray medical image"),
    ("xml", "code", "collected XML files"),
]

SAMPLES += [
    Sample(
        name=name,
        category=category,
        archive="silesia",
        member=f"{SILESIA_ROOT}{name}.zip",
        inner=name,
        origin=f"Silesia corpus: {what}",
        group="silesia",
    )
    for name, category, what in SILESIA
]


def _download(archive: Archive) -> Path:
    """Fetch an archive into the cache, verifying its SHA-256."""
    CACHE_DIR.mkdir(parents=True, exist_ok=True)
    dest = CACHE_DIR / f"{archive.key}{'.zip' if archive.kind == 'zip' else '.tar.gz'}"

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


def _extract(archive: Archive, path: Path, sample: Sample) -> bytes:
    member = sample.member
    if member == WHOLE_TAR:
        if archive.kind != "tar.gz":
            raise SystemExit(f"{archive.key} is not a tar.gz")
        # gzip is deterministic in reverse: the tar inside a pinned .tar.gz is
        # itself pinned, so this stays reproducible without vendoring it.
        import gzip

        with gzip.open(path, "rb") as fh:
            return fh.read()
    if archive.kind == "zip":
        with zipfile.ZipFile(path) as zf:
            blob = zf.read(member)
        if not sample.inner:
            return blob
        # The member is itself a zip holding one file: how Silesia is
        # published, one archive per sample.
        with zipfile.ZipFile(io.BytesIO(blob)) as inner:
            return inner.read(sample.inner)
    with tarfile.open(path, "r:gz") as tf:
        fh = tf.extractfile(member)
        if fh is None:
            raise SystemExit(f"{member} is not a regular file in {archive.key}")
        return fh.read()


def path_of(sample: Sample) -> Path:
    """Where a sample is materialised. Groups other than core get a subdir."""
    return CORPUS_DIR / sample.subdir / sample.name if sample.subdir \
        else CORPUS_DIR / sample.name


def ensure_corpus(quiet: bool = False,
                  groups: tuple[str, ...] = GROUPS) -> list[tuple[Sample, Path]]:
    """Materialise the requested groups under bench/corpus/, return the paths."""
    out: list[tuple[Sample, Path]] = []
    archive_paths: dict[str, Path] = {}

    for sample in SAMPLES:
        if sample.group not in groups:
            continue
        target = path_of(sample)
        if not target.exists():
            target.parent.mkdir(parents=True, exist_ok=True)
            archive = ARCHIVES[sample.archive]
            if sample.archive not in archive_paths:
                archive_paths[sample.archive] = _download(archive)
            data = _extract(archive, archive_paths[sample.archive], sample)
            target.write_bytes(data)
            if not quiet:
                print(f"  extracted {sample.name} ({len(data)} bytes)", file=sys.stderr)
        out.append((sample, target))

    return out


def clean() -> None:
    if CORPUS_DIR.exists():
        shutil.rmtree(CORPUS_DIR)


def _cli() -> None:
    args = sys.argv[1:]
    if args and args[0] == "clean":
        clean()
        print("corpus removed")
        return
    groups = GROUPS
    if "--core" in args:
        groups = ("core",)
    elif "--silesia" in args:
        groups = ("silesia",)
    for sample, path in ensure_corpus(groups=groups):
        print(f"{path.stat().st_size:>10}  {sample.group:<7} {sample.category:<8} "
              f"{sample.name}")


if __name__ == "__main__":
    _cli()
