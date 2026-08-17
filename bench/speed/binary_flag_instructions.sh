#!/bin/sh
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Instruction counts for the encoder variants of bench_binary_flag.c, as a
# check on its timings that does not depend on the machine.
#
# The timings are the result; this is the control. Wall-clock numbers on a
# shared or virtualised machine move by more than some of the differences
# being measured, so a conclusion that rests on them alone is worth less
# than one that two independent measurements agree on. Instruction counts
# are deterministic and reproduce anywhere.
#
# They are a proxy and they distort in a known direction here: they charge
# every instruction the same, so a branch nothing can predict -- which is
# most of what separates these variants -- costs the same as one that is
# always right. Expect the instruction ratios to be *smaller* than the
# timing ratios, and read the two together: the counts confirm which
# variant does less work, the timings say how much that is worth.
#
# Usage: bench/speed/binary_flag_instructions.sh [file ...]
#        defaults to the binary half of the corpus.
# Requires valgrind and a C compiler.

set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
WORK=${TMPDIR:-/tmp}/base85n-binflag.$$
mkdir -p "$WORK"
trap 'rm -rf "$WORK"' EXIT

: "${CC:=cc}"
$CC -O2 -std=c11 -I"$ROOT/c/include" -DBASE85N_BENCH_ENCODERS \
    "$ROOT/bench/speed/bench_binary_flag.c" "$ROOT/c/src/base85n.c" \
    -o "$WORK/bin"

if [ $# -gt 0 ]; then
    FILES="$*"
else
    FILES=$(ls "$ROOT"/bench/corpus/*.wasm "$ROOT"/bench/corpus/*.so \
               "$ROOT"/bench/corpus/*.ttf "$ROOT"/bench/corpus/*.tar \
               "$ROOT"/bench/corpus/*.jpg "$ROOT"/bench/corpus/*.png \
            2>/dev/null || true)
fi
if [ -z "$FILES" ]; then
    echo "no input files; run 'python3 bench/corpus.py' first" >&2
    exit 1
fi

ir() {
    valgrind --tool=callgrind --callgrind-out-file=/dev/null "$@" 2>&1 \
        | grep 'refs:' | tr -d ',' | awk '{print $NF}'
}

VARIANTS="default binary default-nofill binary-nofill narrow-gate gate4-only word-gate"

printf '%-24s %-16s %14s %10s\n' input variant instructions 'vs def'
for f in $FILES; do
    name=$(basename "$f")
    # The `none` run reads the file and does everything but the encode, so
    # subtracting it leaves the encode alone.
    base=$(ir "$WORK/bin" --count none "$f")
    ref=""
    for v in $VARIANTS; do
        n=$(ir "$WORK/bin" --count "$v" "$f")
        [ -z "$ref" ] && ref=$((n - base))
        awk -v i="$name" -v v="$v" -v n="$((n - base))" -v r="$ref" \
            'BEGIN { printf "%-24s %-16s %14d %9.2fx\n", i, v, n, r/n }'
    done
done
