// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The `simd` feature: a vector fast-forward for the Dynamic Passthrough
//! prefix scan (spec section 6.2), behind `#![feature(portable_simd)]`.
//!
//! **This is an acceleration, never a decision.** [`SkipSet::can_skip`] answers
//! one question -- "do the next sixteen bytes leave the scan's state alone and
//! open no run?" -- and the scalar loop in [`crate::encode`] does everything
//! else. A `false` costs a scalar step and nothing more, so the two paths
//! cannot disagree about the encoding: the vector step may only skip bytes it
//! has proved the scalar loop would have walked past without doing anything.
//!
//! What it accelerates is the loop that dominates encoding text. The scan asks
//! two questions per byte -- does this byte equal its predecessor, and is its
//! [`DP_CLASS`](crate::alphabet::DP_CLASS) already accounted for -- and for
//! almost every byte of almost every segment both answers are "nothing
//! changes". Sixteen bytes at a time is one load, two shuffles and two
//! comparisons.
//!
//! The membership test is the nibble-pair lookup simdjson uses. A byte is
//! "nothing changes" iff bit `b >> 4` of `lo[b & 15]` is set, which covers
//! every byte below 128; bytes at or above 128 are not representable at all, so
//! they must always stop the skip, and [`NIBBLE_BITS`] returns zero for the
//! high nibbles that only they can have.
//!
//! Keeping `lo` current costs nothing per byte. Each class the scan accounts
//! for is carried by exactly one byte value -- one R-Set character, or one
//! donor -- so accounting for it clears exactly one bit.

use std::simd::cmp::{SimdPartialEq, SimdPartialOrd};
use std::simd::{u8x16, Simd};

use crate::alphabet::{DP_CLASS, DP_PLAIN};

/// How many bytes one step of the fast-forward settles.
pub const LANES: usize = 16;

/// `1 << n` for the high nibbles a byte below 128 can have, and zero for the
/// rest: a byte at or above 128 indexes a zero here and is therefore never
/// skipped, which is what it has to be -- none of them is representable.
const NIBBLE_BITS: [u8; 16] = [1, 2, 4, 8, 16, 32, 64, 128, 0, 0, 0, 0, 0, 0, 0, 0];

/// The bytes the scan can pass over, as the nibble-pair table described in the
/// module comment.
#[derive(Clone, Copy)]
pub struct SkipSet {
    lo: u8x16,
}

impl SkipSet {
    /// The set as it stands before the scan has accounted for anything: the
    /// plain Alphabet-N characters, which no profile spends and which therefore
    /// never change the state.
    pub fn new() -> Self {
        const PLAIN_LO: [u8; 16] = {
            let mut lo = [0u8; 16];
            let mut b = 0usize;
            while b < 128 {
                if DP_CLASS[b] == DP_PLAIN {
                    lo[b & 15] |= 1 << (b >> 4);
                }
                b += 1;
            }
            lo
        };
        SkipSet { lo: u8x16::from_array(PLAIN_LO) }
    }

    /// Records that `b`'s class has been accounted for, so that further
    /// occurrences of `b` change nothing and can be skipped.
    #[inline]
    pub fn account(&mut self, b: u8) {
        let mut lo = self.lo.to_array();
        lo[(b & 15) as usize] |= 1 << (b >> 4);
        self.lo = u8x16::from_array(lo);
    }

    /// How many of the next [`LANES`] bytes the scan can advance over without
    /// doing anything: 0 when the first of them already needs the scalar loop,
    /// [`LANES`] when none of them does.
    ///
    /// Answering with the run rather than with a yes or no is what keeps this
    /// worth doing on ordinary text. A byte that stops the skip is one the
    /// scalar loop has to see anyway, and the bytes before it in the same
    /// window are settled by the same two comparisons -- so a window is never
    /// tested for nothing, and the loop is entered again only after the byte
    /// that stopped it has been dealt with.
    ///
    /// `w` is the window from the byte *before* the sixteen in question, so
    /// that the run test has each byte's predecessor: `w[0]` is that
    /// predecessor and `w[1..17]` the bytes to settle.
    #[inline]
    pub fn skippable(&self, w: &[u8; LANES + 1]) -> usize {
        let cur = u8x16::from_slice(&w[1..]);
        let prev = u8x16::from_slice(&w[..LANES]);

        // Nothing changes for a byte whose class is already accounted for ...
        let lo = self.lo.swizzle_dyn(cur & Simd::splat(15));
        let bits = Simd::from_array(NIBBLE_BITS).swizzle_dyn(cur >> Simd::splat(4));
        let accounted = (lo & bits).simd_gt(Simd::splat(0));
        // ... and only if it does not open a run either.
        let opens_run = cur.simd_eq(prev);

        let stops = !(accounted & !opens_run);
        (stops.to_bitmask().trailing_zeros() as usize).min(LANES)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::alphabet::DP_STOP;

    /// The question [`SkipSet::skippable`] answers, one byte at a time: how many
    /// of the sixteen bytes after `w[0]` change nothing and open no run.
    ///
    /// This is the scalar loop's test, written out. It is the contract, and the
    /// vector step may not differ from it by one byte in either direction: one
    /// byte too few costs a step and nothing else, but one byte too many skips
    /// a byte the scan had to see, which changes the segment and therefore the
    /// output.
    fn scalar_skippable(uninteresting: &[bool; 256], w: &[u8; LANES + 1]) -> usize {
        let mut n = 0;
        while n < LANES {
            let b = w[n + 1];
            if !uninteresting[b as usize] || b == w[n] {
                break;
            }
            n += 1;
        }
        n
    }

    /// A deterministic byte source, so a failure names a case that can be rerun.
    struct Rng(u64);

    impl Rng {
        fn next(&mut self) -> u64 {
            self.0 ^= self.0 << 13;
            self.0 ^= self.0 >> 7;
            self.0 ^= self.0 << 17;
            self.0
        }
    }

    /// The set as the scan builds it: the plain characters from the start, plus
    /// whatever has been accounted for since.
    fn reference(accounted: &[u8]) -> [bool; 256] {
        let mut table = [false; 256];
        for b in 0..=255u8 {
            table[b as usize] = DP_CLASS[b as usize] == DP_PLAIN;
        }
        for &b in accounted {
            table[b as usize] = true;
        }
        table
    }

    #[test]
    fn skippable_answers_what_the_scalar_loop_would() {
        let mut rng = Rng(0x2545_F491_4F6C_DD1D);
        let interesting: Vec<u8> = (0..=255u8)
            .filter(|&b| DP_CLASS[b as usize] != DP_PLAIN && DP_CLASS[b as usize] != DP_STOP)
            .collect();

        for case in 0..20_000 {
            // A set with a random handful of classes accounted for, which is
            // what a segment looks like a few bytes in.
            let mut accounted = Vec::new();
            let mut set = SkipSet::new();
            for _ in 0..(rng.next() % 6) {
                let b = interesting[(rng.next() % interesting.len() as u64) as usize];
                accounted.push(b);
                set.account(b);
            }
            let table = reference(&accounted);

            // Windows drawn from the alphabet the scan meets: plain characters
            // mostly, with the odd R-Set character, donor, run or stop byte.
            let mut w = [0u8; LANES + 1];
            for slot in w.iter_mut() {
                *slot = match rng.next() % 8 {
                    0 => (rng.next() % 256) as u8,
                    1 => interesting[(rng.next() % interesting.len() as u64) as usize],
                    2 => 0x80u8.wrapping_add((rng.next() % 128) as u8),
                    _ => b"abcdefghijklmnopqrstuvwxyz0123456789"
                        [(rng.next() % 36) as usize],
                };
            }
            // Runs matter more than anything else here, so plant one often.
            if rng.next().is_multiple_of(3) {
                let at = (rng.next() % LANES as u64) as usize;
                w[at + 1] = w[at];
            }

            assert_eq!(
                set.skippable(&w),
                scalar_skippable(&table, &w),
                "case {case}: window {w:?}, accounted {accounted:?}"
            );
        }
    }

    /// The two ends of the set, exhaustively: every byte value as the single
    /// byte in question, against every state of "has its class been accounted
    /// for". Random windows reach these too, but not with the certainty a
    /// membership test deserves.
    #[test]
    fn every_byte_is_classified_as_the_scalar_loop_classifies_it() {
        for b in 0..=255u8 {
            for account_it in [false, true] {
                let mut set = SkipSet::new();
                let mut accounted = Vec::new();
                if account_it && DP_CLASS[b as usize] != DP_STOP {
                    set.account(b);
                    accounted.push(b);
                }
                let table = reference(&accounted);

                // `b` alone, with a predecessor it does not equal and a tail of
                // plain characters, so the answer is about `b` and nothing else.
                let mut w = [b'a'; LANES + 1];
                w[0] = if b == b'b' { b'c' } else { b'b' };
                w[1] = b;
                assert_eq!(
                    set.skippable(&w),
                    scalar_skippable(&table, &w),
                    "{b:#04x}, accounted {account_it}"
                );

                // And as the second byte of a run, which no state makes
                // skippable.
                let mut w = [b'a'; LANES + 1];
                w[0] = b;
                w[1] = b;
                assert_eq!(set.skippable(&w), 0, "{b:#04x} opened a run and was skipped");
            }
        }
    }
}
