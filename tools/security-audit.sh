#!/bin/sh
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# The checks that are too slow to run on every push, in one place, so that they
# are run deliberately rather than not at all: before a release, after anything
# that touches `unsafe`, the parallel encoder or the decoder, and whenever
# somebody wants to know what this project actually verifies.
#
# CI runs the fast half of the story -- the suites in five languages, the shared
# vectors, the C-against-Rust differential in both builds, the packaged crate.
# This runs the half that costs minutes to hours:
#
#   1. Miri            undefined behaviour and provenance, interpreted
#   2. AddressSanitizer out-of-bounds and use-after-free, at native speed
#   3. ThreadSanitizer  data races in the parallel encoder
#   4. cargo-deny       advisories, licences and sources of the build tree
#   5. cargo-fuzz       the Rust API, coverage-guided
#   6. c/fuzz           the C implementation, and the two against each other
#
# Usage:
#   tools/security-audit.sh [--quick|--full] [SECONDS]
#
#   --quick   (default) the scoped Miri set and a minute per fuzz target
#   --full    every test under Miri and the given budget per fuzz target
#   SECONDS   fuzzing budget per target (default 60)
#
# What each step needs, and what to install if it is missing, is printed by the
# step itself -- a missing tool is reported and skipped, not a failure, so that
# a partial run still tells you something.

set -eu

MODE=quick
SECS=60
for arg in "$@"; do
    case $arg in
    --quick) MODE=quick ;;
    --full) MODE=full ;;
    *[!0-9]*) echo "unrecognised argument: $arg" >&2; exit 2 ;;
    *) SECS=$arg ;;
    esac
done

ROOT=$(cd "$(dirname "$0")/.." && pwd)
RUST="$ROOT/rust"
PASSED=''
SKIPPED=''
FAILED=''

have() { command -v "$1" >/dev/null 2>&1; }

step() { printf '\n=== %s ===\n' "$1"; }
pass() { PASSED="$PASSED $1"; printf '  ok: %s\n' "$2"; }
skip() { SKIPPED="$SKIPPED $1"; printf '  skipped: %s\n' "$2"; }
fail() { FAILED="$FAILED $1"; printf '  FAILED: %s\n' "$2"; }

# ---------------------------------------------------------------- 1. Miri ----
# What only Miri sees: undefined behaviour in the four `extern "C"` entry points
# -- the crate's only `unsafe` -- and, with strict provenance, any pointer the
# rest of the crate forms that it has no right to. Isolation is off because the
# vector tests read the shared JSON files.
#
# It interprets rather than executes, at roughly a thousandth of native speed,
# so the default scope is the modules where it earns that, and which together
# take about four minutes: the C ABI, the lane arithmetic, the encoder's edge
# cases, the decoder's error paths, and the golden vectors -- every construct of
# the format, decoded under the interpreter. `--full` adds the randomised sweeps
# and takes hours. Tests whose subject is wall-clock time,
# and the ones that push megabytes through threads, are `#[cfg_attr(miri,
# ignore)]` and stay out either way.
step "1. Miri (undefined behaviour, provenance)"
if have rustup && rustup component list --toolchain nightly 2>/dev/null | grep -q '^miri.*installed'; then
    MIRIFLAGS='-Zmiri-strict-provenance -Zmiri-disable-isolation'
    export MIRIFLAGS
    if [ "$MODE" = full ]; then
        if (cd "$RUST" && cargo +nightly miri test --lib); then
            pass miri "every test"
        else
            fail miri "every test"
        fi
    else
        ok=yes
        for m in 'ffi::' 'encode::lane_tests' 'tests::edge_cases' 'tests::errors' 'tests::vectors'; do
            printf '  %s ... ' "$m"
            if (cd "$RUST" && cargo +nightly miri test --lib "$m" >/dev/null 2>&1); then
                echo ok
            else
                echo FAILED
                ok=no
            fi
        done
        [ "$ok" = yes ] && pass miri "the C ABI, the lane arithmetic, the edge cases, the error paths, the vectors" \
                        || fail miri "see above"
    fi
    unset MIRIFLAGS
else
    skip miri "rustup +nightly component add miri"
fi

# ------------------------------------------------------ 2. AddressSanitizer ----
# The whole suite at native speed, with every load and store checked. Rust's
# bounds checks make most of what ASan finds unreachable in safe code; what is
# left is the C boundary, where a pointer from a foreign caller meets a slice.
step "2. AddressSanitizer (whole suite)"
if rustc +nightly --version >/dev/null 2>&1; then
    if (cd "$RUST" && RUSTFLAGS='-Zsanitizer=address' cargo +nightly test --lib \
        --target x86_64-unknown-linux-gnu -Z build-std >/dev/null 2>&1); then
        pass asan "no findings"
    else
        fail asan "see: RUSTFLAGS=-Zsanitizer=address cargo +nightly test --lib --target x86_64-unknown-linux-gnu -Z build-std"
    fi
else
    skip asan "rustup toolchain install nightly --component rust-src"
fi

# ------------------------------------------------------- 3. ThreadSanitizer ----
# `encode_parallel` is the only code in the project that runs input through more
# than one thread. The workers share nothing but an immutable input slice, which
# is an argument; this is the check.
step "3. ThreadSanitizer (the parallel encoder)"
if rustc +nightly --version >/dev/null 2>&1; then
    if (cd "$RUST" && RUSTFLAGS='-Zsanitizer=thread' cargo +nightly test --lib parallel \
        --target x86_64-unknown-linux-gnu -Z build-std >/dev/null 2>&1); then
        pass tsan "no races"
    else
        fail tsan "see: RUSTFLAGS=-Zsanitizer=thread cargo +nightly test --lib parallel --target x86_64-unknown-linux-gnu -Z build-std"
    fi
else
    skip tsan "rustup toolchain install nightly --component rust-src"
fi

# ----------------------------------------------------------- 4. cargo-deny ----
# No implementation here links a third-party crate, module or package at
# runtime. What this checks is the build and test tree -- advisories, licences,
# duplicate versions, and that nothing comes from anywhere but crates.io.
step "4. cargo-deny (advisories, licences, sources)"
if have cargo-deny; then
    if (cd "$ROOT" && cargo deny --manifest-path rust/Cargo.toml check); then
        pass deny "clean"
    else
        fail deny "see above"
    fi
else
    skip deny "cargo install cargo-deny --locked"
fi

# ----------------------------------------------------------- 5. cargo-fuzz ----
# Coverage-guided, against the Rust API: the round trip, the decoder on input it
# did not produce, and the parallel encoder's seams at a chunk size small enough
# for a fuzz case to contain dozens of them.
step "5. cargo-fuzz (${SECS}s per target)"
if have cargo-fuzz; then
    ok=yes
    for target in roundtrip decode parallel_seams; do
        printf '  %s ... ' "$target"
        if (cd "$RUST/fuzz" && cargo +nightly fuzz run "$target" -- \
            -max_total_time="$SECS" -print_final_stats=1 2>&1 |
            grep -q 'DONE'); then
            echo ok
        else
            echo FAILED
            ok=no
        fi
    done
    [ "$ok" = yes ] && pass fuzz "three targets, ${SECS}s each" || fail fuzz "see rust/fuzz/artifacts"
else
    skip fuzz "cargo install cargo-fuzz --locked"
fi

# ------------------------------------------------------------- 6. c/fuzz ----
# The C implementation under ASan and UBSan, and the two implementations against
# each other in one process -- which is the check that the wire format is one
# format and not two that agree on the test vectors.
step "6. c/fuzz (${SECS}s per target)"
if ! have clang; then
    skip cfuzz "clang"
# libFuzzer and the sanitizers ship separately from clang itself, and a missing
# one is a linker error rather than a finding -- so it is asked about directly
# and reported as what it is. `-print-file-name` echoes the name back unchanged
# when it cannot find the file.
elif [ -f "$(clang -print-file-name=libclang_rt.fuzzer-x86_64.a 2>/dev/null)" ]; then
    if (cd "$ROOT/c/fuzz" && make run SECS="$SECS" >/dev/null 2>&1 &&
        make run-differential SECS="$SECS" >/dev/null 2>&1); then
        pass cfuzz "round trip, decoder, and C against Rust"
    else
        fail cfuzz "see: cd c/fuzz && make run run-differential"
    fi
else
    skip cfuzz "the clang sanitizer runtime, e.g. apt install libclang-rt-dev"
fi

printf '\n=== summary ===\n'
[ -n "$PASSED" ]  && printf '  passed: %s\n' "$PASSED"
[ -n "$SKIPPED" ] && printf '  skipped:%s\n' "$SKIPPED"
[ -n "$FAILED" ]  && { printf '  FAILED: %s\n' "$FAILED"; exit 1; }
exit 0
