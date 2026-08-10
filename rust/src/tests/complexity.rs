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
//! The time limits are deliberately loose. A linear encoder handles this
//! input in milliseconds; the quadratic one these tests exist to catch
//! needed minutes, so any bound in between works and a generous one does not
//! go flaky on a slow or loaded machine.

use std::time::{Duration, Instant};

use crate::{decode, encode};

const ESCAPE_DENSE_SIZE: usize = 128 * 1024;
const TIME_LIMIT: Duration = Duration::from_secs(20);

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
    fn timed(n: usize) -> Duration {
        let data = vec![b'~'; n];
        let start = Instant::now();
        let _ = encode(&data);
        start.elapsed()
    }

    timed(4096); // warm up

    let small = timed(32 * 1024);
    let large = timed(64 * 1024);

    // Linear predicts ~2x, quadratic predicts ~4x. A 3x ceiling rules out
    // quadratic growth without being sensitive to ordinary timing noise.
    assert!(
        large < small * 3,
        "doubling the input multiplied encoding time by {:.1} ({small:?} -> {large:?}); \
         expected about 2 for a linear encoder",
        large.as_secs_f64() / small.as_secs_f64()
    );
}
