#!/bin/sh
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Counts the instructions the C and Rust implementations execute for one encode
# and one decode of the same input, and prints the ratio.
#
# Why instruction counts rather than throughput: on a shared or virtualised
# machine wall-clock numbers move by more than the differences being measured,
# and a run long enough to average that out is expensive. Instruction counts are
# deterministic and reproducible on any machine. They are not a substitute for
# throughput -- they ignore cache behaviour, branch prediction and memory
# bandwidth, and they charge `rep stosb` one instruction per byte, which makes a
# large memset look far more expensive than it is. Read a ratio near 1.0 as "the
# same amount of work", not as a timing result.
#
# Usage: bench/instructions/run.sh [bytes]   (default 200000)
# Requires valgrind, a C compiler and cargo.

set -e
N=${1:-200000}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
WORK=${TMPDIR:-/tmp}/base85n-instructions.$$
mkdir -p "$WORK"
trap 'rm -rf "$WORK"' EXIT

: "${CC:=cc}"
$CC -O2 -std=c11 -I"$ROOT/c/include" \
    "$ROOT/bench/instructions/count.c" "$ROOT/c/src/base85n.c" -o "$WORK/count_c"
( cd "$ROOT/rust" && cargo build --release --quiet --example count )
RUST_BIN="$ROOT/rust/target/release/examples/count"

ir() {
    valgrind --tool=callgrind --callgrind-out-file=/dev/null "$@" 2>&1 \
        | grep 'refs:' | tr -d ',' | awk '{print $NF}'
}

printf '%-8s %-7s %12s %12s %8s\n' input phase C Rust Rust/C
for kind in random text mixed; do
    cb=$(ir "$WORK/count_c"  "$kind" none   "$N" "$ROOT/README.md")
    rb=$(ir "$RUST_BIN"      "$kind" none   "$N" "$ROOT/README.md")
    for phase in encode decode; do
        c=$(ir "$WORK/count_c" "$kind" "$phase" "$N" "$ROOT/README.md")
        r=$(ir "$RUST_BIN"     "$kind" "$phase" "$N" "$ROOT/README.md")
        # The "none" run does the same setup without the measured call, so
        # subtracting it leaves the encode or decode alone.
        awk -v k="$kind" -v p="$phase" -v c="$c" -v cb="$cb" -v r="$r" -v rb="$rb" \
            'BEGIN { printf "%-8s %-7s %12d %12d %7.2f\n", k, p, c-cb, r-rb, (r-rb)/(c-cb) }'
    done
done
