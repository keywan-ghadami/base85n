#!/bin/sh
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Times the C and Rust implementations against each other, encoding and
# decoding the same inputs, and prints the ratio.
#
# Why wall clock here and instruction counts in bench/instructions: they answer
# different questions, and for these encoders they disagree. Both spend most of
# a high-entropy encode deciding what mode to use, and the decisions that cost
# are the ones a branch predictor gets wrong -- work an instruction count
# charges nothing for. One step of the Rust encoder's 2026-08 pass *added* 4 %
# to its instruction count on random input and made that encode 2.1 times
# faster. Read this script's numbers for how fast the code is, and
# bench/instructions' for how much work it does.
#
# The two harnesses generate their input the same way, run the same loop and
# report the fastest round, so their numbers divide. Each pair is run
# interleaved, several rounds, and the best of each is taken: on a shared or
# virtualised machine the slow rounds are the interrupted ones.
#
# Usage: bench/throughput/run.sh [bytes] [reps] [rounds]
# Requires a C compiler and cargo. Set RUST_TOOLCHAIN and RUST_FEATURES to
# measure the `simd` feature:
#
#   RUST_TOOLCHAIN=+nightly RUST_FEATURES="--features simd" \
#   RUSTFLAGS="-C target-feature=+avx2" \
#   RUST_STD="-Z build-std=std,panic_abort --target x86_64-unknown-linux-gnu" \
#   bench/throughput/run.sh
#
# -Z build-std is not optional for that measurement; without it the shuffle the
# feature is built on stays a scalar fallback in precompiled core and the
# feature is far slower than the stable build. See rust/README.md.

set -e
N=${1:-4000000}
REPS=${2:-10}
ROUNDS=${3:-3}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
WORK=${TMPDIR:-/tmp}/base85n-throughput.$$
mkdir -p "$WORK"
trap 'rm -rf "$WORK"' EXIT

: "${CC:=cc}"
$CC -O2 -std=gnu11 -I"$ROOT/c/include" \
    "$ROOT/bench/throughput/time.c" "$ROOT/c/src/base85n.c" -o "$WORK/time_c"

( cd "$ROOT/rust" && cargo ${RUST_TOOLCHAIN:-} build --release --quiet \
    --example throughput ${RUST_FEATURES:-} ${RUST_STD:-} )
if [ -n "${RUST_STD:-}" ]; then
    RUST_BIN=$(ls "$ROOT"/rust/target/*/release/examples/throughput | head -1)
else
    RUST_BIN="$ROOT/rust/target/release/examples/throughput"
fi

# The file the `text` input is tiled from. Any text file will do; the README is
# what is always there.
TEXT=${TEXT:-$ROOT/README.md}

best() {
    # Best of $ROUNDS runs of one harness, and a check that both implementations
    # encoded to the same length -- a speed comparison between two different
    # outputs would not mean anything.
    top=0
    for _ in $(seq "$ROUNDS"); do
        v=$("$@" 2>"$WORK/err")
        top=$(awk -v a="$top" -v b="$v" 'BEGIN { print (b > a ? b : a) }')
    done
    grep -o 'encoded [0-9]*' "$WORK/err" | awk '{print $2}' > "$WORK/len"
    echo "$top"
}

printf '%-8s %-7s %12s %12s %8s\n' input phase C Rust Rust/C
for kind in random text mixed; do
    for phase in encode decode; do
        c=$(best "$WORK/time_c" "$kind" "$phase" "$N" "$REPS" "$TEXT")
        clen=$(cat "$WORK/len")
        r=$(best "$RUST_BIN" "$kind" "$phase" "$N" "$REPS" "$TEXT")
        rlen=$(cat "$WORK/len")
        [ "$clen" = "$rlen" ] || {
            echo "encoded lengths differ ($clen vs $rlen) -- not a speed difference" >&2
            exit 1
        }
        awk -v k="$kind" -v p="$phase" -v c="$c" -v r="$r" \
            'BEGIN { printf "%-8s %-7s %9.1f    %9.1f    %7.2f\n", k, p, c, r, r/c }'
    done
done
