#!/bin/sh
# Everything that has to be true before the C library is tagged, and on every
# push: that it builds, that its tests pass, that it *installs*, and that what
# it installs can actually be consumed -- by CMake and by pkg-config, the two
# ways a C project finds a dependency.
#
# The last one is the point. A library can build and test perfectly and still
# be unpackageable, which is exactly what this one was: the CMake build had no
# install rules at all, so vcpkg had nothing to install and `find_package` had
# nothing to find. Nothing in the test suite noticed, because none of it is
# about the package.
#
# Usage:
#   tools/c-package-check.sh [expected-version]
#
# With an argument, the version in c/CMakeLists.txt, the vcpkg port and
# clib.json all have to equal it -- the form the release workflow uses. Builds
# into a temporary directory and writes nothing into the working copy.
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
expected=${1:-}
cc=${CC:-cc}

say() { printf '\n=== %s\n' "$*"; }

say "the version is written down three times"
cmake_version=$(sed -n 's/^project(base85n VERSION \([0-9.]*\).*/\1/p' "$root/c/CMakeLists.txt")
port_version=$(python3 -c "import json;print(json.load(open('$root/c/vcpkg/base85n/vcpkg.json'))['version'])")
clib_version=$(python3 -c "import json;print(json.load(open('$root/clib.json'))['version'])")
echo "  c/CMakeLists.txt          $cmake_version"
echo "  c/vcpkg/base85n/vcpkg.json $port_version"
echo "  clib.json                 $clib_version"
if [ "$cmake_version" != "$port_version" ] || [ "$cmake_version" != "$clib_version" ]; then
    echo "these have to agree: the port and the manifest name the version consumers resolve" >&2
    exit 1
fi
if [ -n "$expected" ] && [ "$cmake_version" != "$expected" ]; then
    echo "the manifests say $cmake_version, the release says $expected" >&2
    exit 1
fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
prefix=$work/prefix

say "build and test"
cmake -S "$root/c" -B "$work/build" -DCMAKE_C_COMPILER="$cc" -DCMAKE_INSTALL_PREFIX="$prefix" >/dev/null
cmake --build "$work/build" >/dev/null
ctest --test-dir "$work/build" --output-on-failure

say "install"
cmake --install "$work/build" >/dev/null
for f in include/base85n.h lib/cmake/base85n/base85n-config.cmake lib/pkgconfig/base85n.pc; do
    test -f "$prefix/$f" || { echo "missing from the install: $f" >&2; exit 1; }
done
ls "$prefix"/lib/libbase85n.* >/dev/null
find "$prefix" -type f | sed "s|$prefix/|  |" | sort

# A program that does what a caller does: encode, decode, compare. Built twice
# against the same install, once through each discovery mechanism.
cat > "$work/main.c" <<'EOF'
#include <base85n.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    const char *msg = "hello, world!";
    char *enc;
    size_t enc_len;
    if (base85n_encode((const unsigned char *)msg, strlen(msg), &enc, &enc_len) != BASE85N_OK) {
        return 1;
    }
    unsigned char *dec;
    size_t dec_len;
    if (base85n_decode(enc, enc_len, &dec, &dec_len) != BASE85N_OK) {
        return 1;
    }
    int ok = dec_len == strlen(msg) && memcmp(dec, msg, dec_len) == 0;
    free(enc);
    free(dec);
    return ok ? 0 : 1;
}
EOF

say "a CMake project can find_package() it"
cat > "$work/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.15)
project(consumer C)
find_package(base85n 0.5 CONFIG REQUIRED)
add_executable(consumer main.c)
target_link_libraries(consumer PRIVATE base85n::base85n)
EOF
cmake -S "$work" -B "$work/consumer" -DCMAKE_PREFIX_PATH="$prefix" >/dev/null
cmake --build "$work/consumer" >/dev/null
"$work/consumer/consumer"
echo "  linked base85n::base85n and round-tripped"

say "pkg-config finds it too"
if command -v pkg-config >/dev/null 2>&1; then
    PKG_CONFIG_PATH=$prefix/lib/pkgconfig pkg-config --exists base85n
    # shellcheck disable=SC2046 # the flags are meant to word-split
    "$cc" "$work/main.c" $(PKG_CONFIG_PATH=$prefix/lib/pkgconfig pkg-config --cflags --libs base85n) -o "$work/pkgapp"
    "$work/pkgapp"
    echo "  built with $(PKG_CONFIG_PATH=$prefix/lib/pkgconfig pkg-config --modversion base85n) from pkg-config and round-tripped"
else
    echo "  pkg-config not installed; skipped"
fi

# clib does not build anything: it copies the files named in clib.json into
# deps/<name>/, flattened. That flattening is the part that can break -- a
# source that reached its header through a relative path would stop compiling
# -- so it is done here and compiled.
say "the files clib installs compile on their own"
mkdir -p "$work/deps/base85n"
python3 - "$root" "$work/deps/base85n" <<'EOF'
import json, pathlib, shutil, sys

root, dest = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
for entry in json.loads((root / "clib.json").read_text())["src"]:
    source = root / entry
    if not source.is_file():
        raise SystemExit(f"clib.json names a file that is not there: {entry}")
    shutil.copy(source, dest / source.name)
    print(f"  deps/base85n/{source.name} <- {entry}")
EOF
"$cc" -I"$work/deps/base85n" "$work/main.c" "$work/deps/base85n/base85n.c" -o "$work/clibapp"
"$work/clibapp"
echo "  compiled flattened and round-tripped"

printf '\nthe C library is ready to tag\n'
