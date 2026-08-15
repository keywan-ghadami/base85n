// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Guard against the rescanning encoder of spec Section 6.6.
//!
//! The prefix scan may look ahead MAX_DP_ANALYSIS_BYTES bytes while step 4
//! consumes as few as 4, so an encoder that redoes the scan from scratch on
//! every iteration performs up to 512 byte inspections per input byte. Bounded
//! lookahead keeps that linear rather than quadratic, but the constant factor
//! is still what this section exists to prevent.
//!
//! Pseudorandom bytes are the worst case: no prefix reaches
//! MIN_PASSTHROUGH_BYTES and no run reaches MIN_FILL_BYTES, so every iteration
//! takes the block-mode branch and advances 4 bytes, while a naive
//! implementation rescans the full window each time.
//!
//! Both tests here are timing-based, which on a shared CI runner means they
//! have to be built to tolerate interference. Two things make them stable:
//! every duration is the *minimum* of several runs, since scheduling noise
//! only ever adds time and never removes it, and the thresholds sit far from
//! the values a healthy encoder produces.

use std::time::{Duration, Instant};

use rand::rngs::StdRng;
use rand::{Rng, SeedableRng};

use crate::{decode, encode};

const SCAN_DENSE_SIZE: usize = 128 * 1024;
const TIME_LIMIT: Duration = Duration::from_secs(20);

/// Sizes for the growth check, and how many times each is measured.
const SMALL_SIZE: usize = 32 * 1024;
const LARGE_SIZE: usize = 64 * 1024;
const REPEATS: usize = 5;

/// Below this, a measurement is too short for its ratio to mean anything.
const MEASURABLE: Duration = Duration::from_millis(1);

/// Linear predicts ~2.0, quadratic ~4.0. Halfway between is the decision point.
const MAX_GROWTH: f64 = 3.0;

/// Input on which no prefix ever reaches MIN_PASSTHROUGH_BYTES.
fn scan_dense(n: usize) -> Vec<u8> {
    let mut rng = StdRng::seed_from_u64(0x5CA4_DE45_u64 ^ n as u64);
    (0..n).map(|_| rng.gen::<u8>()).collect()
}

/// Fastest of `repeats` encodes of `n` scan-dense bytes.
fn best_encode_time(n: usize, repeats: usize) -> Duration {
    let data = scan_dense(n);
    let mut best = Duration::MAX;
    for _ in 0..repeats {
        let start = Instant::now();
        let _ = encode(&data);
        best = best.min(start.elapsed());
    }
    best
}

#[test]
fn scan_dense_input_encodes_in_linear_time() {
    let data = scan_dense(SCAN_DENSE_SIZE);

    let start = Instant::now();
    let encoded = encode(&data);
    let elapsed = start.elapsed();

    assert_eq!(decode(&encoded).expect("decode failed"), data);
    assert!(
        elapsed < TIME_LIMIT,
        "encoding {SCAN_DENSE_SIZE} scan-dense bytes took {elapsed:?}; this is \
         the signature of the per-iteration rescan that spec Section 6.6 forbids"
    );
}

#[test]
fn scan_dense_growth_is_not_quadratic() {
    best_encode_time(4096, 1); // warm up

    let small = best_encode_time(SMALL_SIZE, REPEATS);
    let large = best_encode_time(LARGE_SIZE, REPEATS);

    if small < MEASURABLE {
        return; // too fast to time meaningfully; the ceiling test still applies
    }

    let growth = large.as_secs_f64() / small.as_secs_f64();
    assert!(
        growth < MAX_GROWTH,
        "doubling the input multiplied encoding time by {growth:.1} \
         ({small:?} -> {large:?}); expected about 2 for a linear encoder"
    );
}

/// The other direction: one long representable run, which Dynamic Passthrough
/// takes MAX_DP_ANALYSIS_BYTES at a time.
#[test]
fn long_representable_run_encodes_in_linear_time() {
    let data = b"the quick brown fox jumps over the lazy dog. ".repeat(4000);

    let start = Instant::now();
    let encoded = encode(&data);
    let elapsed = start.elapsed();

    assert_eq!(decode(&encoded).expect("decode failed"), data);
    assert!(elapsed < TIME_LIMIT, "took {elapsed:?}");
}
