// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Guard against the quadratic encoder of spec Section 6.6.
//!
//! Pass 1 scans to the end of a representable run while the main loop can
//! consume as little as 4 bytes of it, so an encoder that re-runs Pass 1 on
//! every iteration is O(n^2). A buffer of escape characters is the worst
//! case: Pass 2 gives up after 3 bytes every time.
//!
//! Both tests here are timing-based, which on a shared CI runner means they
//! have to be built to tolerate interference. Two things make them stable:
//! every duration is the *minimum* of several runs, since scheduling noise
//! only ever adds time and never removes it, and the thresholds sit far from
//! the values a healthy encoder produces. A linear encoder handles the large
//! case in milliseconds; the quadratic one these tests exist to catch needed
//! minutes.

use std::time::{Duration, Instant};

use crate::{decode, encode};

const ESCAPE_DENSE_SIZE: usize = 128 * 1024;
const TIME_LIMIT: Duration = Duration::from_secs(20);

/// Sizes for the growth check, and how many times each is measured.
const SMALL_SIZE: usize = 32 * 1024;
const LARGE_SIZE: usize = 64 * 1024;
const REPEATS: usize = 5;

/// Below this, a measurement is too short for its ratio to mean anything.
const MEASURABLE: Duration = Duration::from_millis(1);

/// Linear predicts ~2.0, quadratic ~4.0. Halfway between is the decision point.
const MAX_GROWTH: f64 = 3.0;

/// Fastest of `repeats` encodes of `n` escape characters.
fn best_encode_time(n: usize, repeats: usize) -> Duration {
    let data = vec![b'~'; n];
    let mut best = Duration::MAX;
    for _ in 0..repeats {
        let start = Instant::now();
        let _ = encode(&data);
        best = best.min(start.elapsed());
    }
    best
}

#[test]
fn escape_dense_input_encodes_in_linear_time() {
    let data = vec![b'~'; ESCAPE_DENSE_SIZE];

    let start = Instant::now();
    let encoded = encode(&data);
    let elapsed = start.elapsed();

    assert_eq!(decode(&encoded).expect("decode failed"), data);
    assert!(
        elapsed < TIME_LIMIT,
        "encoding {ESCAPE_DENSE_SIZE} escape characters took {elapsed:?}; this is \
         the signature of the quadratic Pass 1 rescan that spec Section 6.6 forbids"
    );
}

#[test]
fn escape_dense_growth_is_not_quadratic() {
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
