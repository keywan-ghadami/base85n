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

# Symbol parity, before anything is compiled against a header: every
# `base85n_*` function either header declares has to be exported by the library,
# and the library must export no others. Without this, "drop-in" holds only for
# as long as the smoke test happens to call every function -- a header that
# declares a fourth entry point the Rust build does not have would link fine
# here and fail in someone else's program.
echo "checking symbol parity ..."
exported=$(nm --defined-only "$lib" | awk '$2 == "T" { print $3 }' \
    | grep '^base85n_' | sort -u)
for header in "$rust_dir/include/base85n.h" "$repo/c/include/base85n.h"; do
    [ -f "$header" ] || continue
    declared=$(sed -n 's/^.*[ *]\(base85n_[a-z_]*\)(.*/\1/p' "$header" | sort -u)
    if [ "$declared" != "$exported" ]; then
        echo "  FAILED: $header declares" >&2
        echo "$declared" | sed 's/^/    /' >&2
        echo "  but the library exports" >&2
        echo "$exported" | sed 's/^/    /' >&2
        exit 1
    fi
    echo "  $(echo "$declared" | wc -l) functions, matching $(basename "$(dirname "$(dirname "$header")")")"
done

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
