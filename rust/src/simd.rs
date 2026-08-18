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
