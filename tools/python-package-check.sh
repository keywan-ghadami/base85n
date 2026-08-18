#!/bin/sh
# Everything that has to be true before the Python distribution goes to an
# index -- run here, from a checkout, by whoever is about to release it, and
# run again by CI on every push and by the release workflow on the exact tree
# it is about to upload.
#
# The questions it answers are the ones a compiled extension makes harder than
# a pure-Python package would:
#
#   * do the three places the version is written down still agree?
#   * does the wheel carry the type stubs and the PEP 561 marker, and can a
#     type checker actually find them?
#   * does the source distribution stand on its own -- it has to carry the Rust
#     crate this binding depends on, and maturin moves the manifest around
#     while building it -- and does its own test suite pass from inside it?
#
# Usage:
#   tools/python-package-check.sh [expected-version]
#
# With an argument, the version in the manifests has to equal it; that is the
# form the release workflow uses, so a release cannot go out under a number
# nobody chose. Needs python3 and a Rust toolchain, and installs its build
# tools into throwaway virtual environments -- nothing is written outside a
# temporary directory and the crate's own target/.
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
expected=${1:-}

say() { printf '\n=== %s\n' "$*"; }

say "the version is written down three times"
dist=$(sed -n 's/^version = "\(.*\)"/\1/p' "$root/python/pyproject.toml" | head -1)
binding=$(sed -n 's/^version = "\(.*\)"/\1/p' "$root/python/Cargo.toml" | head -1)
crate=$(sed -n 's/^version = "\(.*\)"/\1/p' "$root/rust/Cargo.toml" | head -1)
echo "  pyproject.toml    $dist"
echo "  python/Cargo.toml $binding"
echo "  rust/Cargo.toml   $crate"
if [ "$dist" != "$binding" ] || [ "$dist" != "$crate" ]; then
    echo "these have to agree" >&2
    exit 1
fi
if [ -n "$expected" ] && [ "$dist" != "$expected" ]; then
    echo "the manifests say $dist, the release says $expected" >&2
    exit 1
fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
out=$work/dist

say "building the wheel and the source distribution"
python3 -m venv "$work/build"
"$work/build/bin/pip" install --quiet --upgrade pip
"$work/build/bin/pip" install --quiet maturin twine mypy pytest
cd "$root/python"
"$work/build/bin/maturin" build --release --out "$out"
"$work/build/bin/maturin" sdist --out "$out"

say "metadata an index will accept"
"$work/build/bin/twine" check "$out"/*

# The stubs are the only interface a type checker has to a compiled module,
# and whether they reach the wheel depends on where maturin decides the
# project root is -- which differs between a checkout and a source
# distribution. Checked, therefore, rather than assumed; and checked again
# below for the wheel pip builds out of the sdist.
say "the wheel carries the type information"
"$work/build/bin/python" - "$out" <<'PY'
import glob, sys, zipfile

names = set(zipfile.ZipFile(glob.glob(f"{sys.argv[1]}/*.whl")[0]).namelist())
missing = {"base85n/__init__.py", "base85n/__init__.pyi", "base85n/py.typed"} - names
assert not missing, f"missing from the wheel: {sorted(missing)}"
assert any(n.endswith((".so", ".pyd", ".dylib")) for n in names), "no extension module"
print("  " + "\n  ".join(sorted(names)))
PY

say "the suite, against the built wheel"
"$work/build/bin/pip" install --quiet --force-reinstall "$out"/*.whl
"$work/build/bin/python" -m pytest -q

# `tests/test_stubs.py` checks that the stubs describe the module. Whether a
# checker finds them at all is a different question -- it turns on the marker
# file and the package layout -- and this is what answers it.
say "a type checker can use the stubs"
mkdir -p "$work/typed"
cat > "$work/typed/good.py" <<'PY'
import base85n

text: str = base85n.encode(b"x", threads=2)
data: bytes = base85n.decode(text)
size: int = base85n.MIN_PASSTHROUGH_BYTES
PY
cat > "$work/typed/bad.py" <<'PY'
import base85n

base85n.encode("not bytes")
PY
"$work/build/bin/mypy" "$work/typed/good.py"
if "$work/build/bin/mypy" "$work/typed/bad.py"; then
    echo "mypy accepted a call the stubs forbid -- are the stubs being found?" >&2
    exit 1
fi
echo "  mypy reads the stubs and rejects what they forbid"

# What pip builds when no wheel matches the platform. It is a different tree
# from this one: maturin vendors the Rust crate into it and moves the manifest
# up a directory, so a path that works here can be wrong there. Building it
# outside the repository, in an interpreter that has never seen this project,
# is what shows it stands alone.
say "the source distribution, built and tested outside the repository"
tar xzf "$out"/*.tar.gz -C "$work"
python3 -m venv "$work/sdist"
"$work/sdist/bin/pip" install --quiet --upgrade pip
"$work/sdist/bin/pip" install --quiet "$(echo "$out"/*.tar.gz)[test]"
"$work/sdist/bin/python" - <<'PY'
import pathlib

import base85n

package = pathlib.Path(base85n.__file__).parent
for name in ("__init__.pyi", "py.typed"):
    assert (package / name).is_file(), f"the sdist build dropped {name}"
assert base85n.decode(base85n.encode(b"round trip")) == b"round trip"
print("  built from the sdist, typed, and round-trips")
PY
cd "$work"/base85n-*/python
"$work/sdist/bin/python" -m pytest -q

say "what would be uploaded"
cd "$out"
sha256sum ./* 2>/dev/null || shasum -a 256 ./*

printf '\nthe Python distribution is ready to publish\n'
