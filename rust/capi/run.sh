#!/bin/sh
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Build the Rust library as a C library and exercise it from C.
#
#   rust/capi/run.sh
#
# smoke.c is compiled twice against the same libbase85n.a: once with this
# crate's header and once with the C implementation's. Both must pass --
# that is the check that the two libraries are interchangeable at the ABI
# level, and not merely similar.
set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
rust_dir=$(dirname "$here")
repo=$(dirname "$rust_dir")
work=${TMPDIR:-/tmp}/base85n-capi.$$
mkdir -p "$work"
trap 'rm -rf "$work"' EXIT

cc=${CC:-cc}
cflags="-std=c11 -Wall -Wextra -Werror -O2"

echo "building the static library ..."
(cd "$rust_dir" && cargo build --release --quiet)
lib="$rust_dir/target/release/libbase85n.a"
[ -f "$lib" ] || { echo "no $lib -- did cargo build produce a staticlib?" >&2; exit 1; }

# What a Rust staticlib needs from the platform on Linux; harmless elsewhere
# except for -ldl, which macOS folds into libSystem.
syslibs="-lpthread -lm"
case $(uname -s) in
Linux) syslibs="$syslibs -ldl" ;;
esac

status=0
for header in "$rust_dir/include" "$repo/c/include"; do
    name=$(basename "$(dirname "$header")")
    # The C implementation's header is only there in a checkout of the
    # repository; from a published crate tarball this loop has one entry.
    if [ ! -f "$header/base85n.h" ]; then
        echo "skipping the $name header -- not present here"
        continue
    fi
    echo "compiling smoke.c against the $name header ..."
    # shellcheck disable=SC2086 # cflags and syslibs are deliberately split
    $cc $cflags -I"$header" "$here/smoke.c" "$lib" $syslibs -o "$work/smoke"
    if "$work/smoke"; then
        echo "  passed with the $name header"
    else
        echo "  FAILED with the $name header" >&2
        status=1
    fi
done

exit $status
