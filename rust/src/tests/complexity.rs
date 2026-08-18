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
//! Both tests here are timing-based, which means they have to survive
//! interference -- not only from a shared CI runner but from the rest of this
//! suite, which the harness runs alongside them on however many cores the
//! machine has.
//!
//! The growth check therefore does not time a small input against a large one.
//! Comparing a short measurement with a long one is not a fair comparison on a
//! busy machine: a measurement that fits inside a scheduler time slice is often
//! not preempted at all, while one twice as long is preempted nearly every time
//! and then waits for its next turn behind everything else runnable. Measured
//! that way a *linear* encoder came out at 3.8 on a threshold of 3.0, and
//! systematically rather than occasionally -- the longer measurement is never
//! the lucky one, so taking the minimum of several tries does not remove it.
//!
//! What it does instead: two calls over the smaller input against one call over
//! the doubled one -- the same bytes either way, so the same time either way --
//! interleaved, over and over, with the fastest pass on each side taken. Equal
//! length gives both sides the same chance at a pass the scheduler leaves
//! alone; taking the fastest keeps the ones it did not. The result is about 1
//! for a linear encoder and about 2 for a quadratic one, and the check is run
//! against deliberately quadratic work as well, so that it cannot quietly
//! become a check that nothing can fail.
//!
//! Measured on a four-core machine: no failure in 10 runs of the whole suite
//! idle, 10 with four other cores' worth of load, or 10 with eight, in debug
//! and in release. The design before this one failed 2 in 8 runs idle.

use std::time::{Duration, Instant};

use rand::rngs::StdRng;
use rand::{Rng, SeedableRng};

use crate::{decode, encode};

const SCAN_DENSE_SIZE: usize = 128 * 1024;
const TIME_LIMIT: Duration = Duration::from_secs(20);

/// Bytes the growth check compares at: this many against twice this many.
///
/// Small, and deliberately so. The check is about the encoder's cost per byte
/// at two sizes, and inputs large enough to leave cache bring the memory system
/// into the comparison instead -- page faults on a freshly grown output buffer,
/// a working set that fits at one size and not at the other. How *long* each
/// measurement takes is set below by how many times it runs, which is the knob
/// that does not change what is being measured.
const GROWTH_SIZE: usize = 32 * 1024;

/// How long the measurement should take. Not a timer-resolution floor: it is
/// this long because the two sides have to be interleaved many times over for
/// the scheduler to treat them alike (see [`growth_ratio`]).
const BUDGET: Duration = Duration::from_millis(500);

/// However slow one call is, this many interleavings, so that each side gets
/// several chances at an undisturbed sample.
const MIN_PAIRS: usize = 24;

/// However fast one call is, no more than this many, as a guard against a first
/// measurement so short that the count runs away.
const MAX_PAIRS: usize = 1 << 20;

/// The same bytes in one call against two: linear predicts ~1.0, quadratic
/// ~2.0. Halfway between is the decision point.
const MAX_GROWTH: f64 = 1.5;

/// Input on which no prefix ever reaches MIN_PASSTHROUGH_BYTES.
fn scan_dense(n: usize) -> Vec<u8> {
    let mut rng = StdRng::seed_from_u64(0x5CA4_DE45_u64 ^ n as u64);
    (0..n).map(|_| rng.gen::<u8>()).collect()
}

/// Calls over `2 * GROWTH_SIZE` scan-dense bytes against twice as many calls
/// over `GROWTH_SIZE` of them, as a ratio: about 1 for work that is linear in
/// its input, about 2 for work that is quadratic.
///
/// The two sides cover the same number of bytes and take about the same time,
/// and they are interleaved -- two small calls, then one large one, over and
/// over -- with the fastest pass on each side kept. That is the whole design,
/// and it is what makes the ratio survive a busy machine.
///
/// Timing one long call against one short one does not survive it. The short
/// measurement often fits inside a scheduler time slice and is not preempted at
/// all, while the long one is preempted and then waits for its turn behind
/// everything else runnable -- a factor of nearly two between two halves of a
/// *linear* encoder. That is systematic rather than noise, so taking the
/// minimum of several tries does not remove it: the longer measurement is never
/// the lucky one. Interleaved, the two sides meet the same scheduler over the
/// same stretch of wall clock, and each side's fastest pass is taken -- the
/// sample the scheduler happened to leave alone, which both sides have the same
/// chance at because their passes are the same length.
///
/// The number of pairs comes from one timing, so that the measurement takes
/// about [`BUDGET`] in a release build as well as a debug one, where the same
/// work is nearly two orders of magnitude apart.
fn growth_ratio(work: &mut dyn FnMut(&[u8])) -> f64 {
    let small = scan_dense(GROWTH_SIZE);
    let large = scan_dense(2 * GROWTH_SIZE);

    work(&large); // warm up: the first touch of a fresh buffer is not typical
    let start = Instant::now();
    work(&large);
    // A pair is one call over the larger input and two over the smaller, so it
    // costs about twice what was just measured.
    let pair = 2 * start.elapsed().as_nanos().max(1);
    let pairs = ((BUDGET.as_nanos() / pair) as usize).clamp(MIN_PAIRS, MAX_PAIRS);

    let mut halves = Duration::MAX;
    let mut whole = Duration::MAX;
    for _ in 0..pairs {
        let start = Instant::now();
        work(&small);
        work(&small);
        let between = Instant::now();
        work(&large);
        let end = Instant::now();

        halves = halves.min(between - start);
        whole = whole.min(end - between);
    }

    whole.as_secs_f64() / halves.as_secs_f64()
}

#[test]
#[cfg_attr(miri, ignore)] // wall-clock, and an interpreter has none
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
#[cfg_attr(miri, ignore)] // wall-clock, and an interpreter has none
fn scan_dense_growth_is_not_quadratic() {
    let growth = growth_ratio(&mut |data| drop(encode(data)));
    assert!(
        growth < MAX_GROWTH,
        "encoding {} bytes at a time took {growth:.1} times as long as encoding \
         the same bytes {GROWTH_SIZE} at a time; expected about 1 for a linear \
         encoder",
        2 * GROWTH_SIZE
    );
}

/// Deliberately quadratic: it walks the buffer again every [`RESCAN_STRIDE`]
/// bytes, which is the shape of the rescanning encoder spec section 6.6
/// forbids, at a scale that runs in milliseconds.
fn rescanning_workload(data: &[u8]) {
    const RESCAN_STRIDE: usize = 1024;
    let mut acc = 0u64;
    let mut i = 0;
    while i < data.len() {
        for &b in &data[..i] {
            acc = acc.wrapping_add(b as u64);
        }
        i += RESCAN_STRIDE;
    }
    assert_ne!(acc, u64::MAX, "the sum is here so the walk cannot be optimised away");
}

/// The check above guards the encoder only for as long as it can still fail,
/// and a timing check on a moving target is exactly the kind that stops being
/// able to: the code gets faster, or the measurement gets looser to keep a busy
/// machine happy, and what is left is a check nothing can trip. So the same
/// measurement is run against work that *is* quadratic, and has to come out on
/// the other side of the same threshold.
#[test]
#[cfg_attr(miri, ignore)] // wall-clock, and an interpreter has none
fn the_growth_check_can_still_fail() {
    let ratio = growth_ratio(&mut rescanning_workload);
    assert!(
        ratio >= MAX_GROWTH,
        "quadratic work measured {ratio:.1} against a threshold of \
         {MAX_GROWTH}; the growth check above cannot be failing anything"
    );
}

/// The other direction: one long representable run, which Dynamic Passthrough
/// takes MAX_DP_ANALYSIS_BYTES at a time.
#[test]
#[cfg_attr(miri, ignore)] // wall-clock, and an interpreter has none
fn long_representable_run_encodes_in_linear_time() {
    let data = b"the quick brown fox jumps over the lazy dog. ".repeat(4000);

    let start = Instant::now();
    let encoded = encode(&data);
    let elapsed = start.elapsed();

    assert_eq!(decode(&encoded).expect("decode failed"), data);
    assert!(elapsed < TIME_LIMIT, "took {elapsed:?}");
}



