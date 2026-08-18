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
use std::simd::{simd_swizzle, u8x16, u8x32, u8x8, Select, Simd};

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

/// How many bytes one step of the block-mode skip settles: eight groups.
pub const SKIP_BYTES: usize = 32;

/// What [`groups_may_hold`] reads: the eight groups, and the lookahead the last
/// one's passthrough gate needs.
pub const SKIP_WINDOW: usize = SKIP_BYTES + 8;

/// Whether any of the eight 4-byte groups starting at `w[0]` could begin a
/// construct other than block mode. `false` means none of them can, and the
/// caller may step over all thirty-two bytes.
///
/// This is the scalar skip's question asked of eight groups at once, and each of
/// the three parts is the same test the scalar path makes, not a weaker one:
///
/// - Fill with a tail is gated on a zero at `q + 2`, which for the eight groups
///   is lanes 2, 6, ... 30. One shuffle collects them.
/// - Solid Fill is gated on `q` and `q + 1` being equal: lanes 0, 4, ... 28
///   against 1, 5, ... 29, one shuffle each.
/// - A passthrough segment needs [`MIN_PASSTHROUGH_BYTES`] representable bytes
///   from its start, so eight in a row is a necessary condition, and the range
///   `[0x09, 0x7E]` a superset of representable. The in-range bits of all forty
///   bytes fold into "eight in a row start here" in three shifts, and the eight
///   group positions are then one mask.
///
/// Both Fill gates are exact and the passthrough one may only fail to rule a
/// group out, never rule out one a segment could start at -- so a `true` costs
/// the caller the exact per-group walk it would have done anyway, and a `false`
/// is a fact.
///
/// (`MIN_PASSTHROUGH_BYTES` is 20 and this asks for 8: the scalar gate asks for
/// the same 8, for the same reason -- eight bytes are one word, or here a third
/// of a vector, and the walk behind the gate settles the rest.)
#[inline]
pub fn groups_may_hold(w: &[u8; SKIP_WINDOW]) -> bool {
    let v = u8x32::from_slice(&w[..SKIP_BYTES]);

    // A zero where a Fill-with-tail would need one.
    let tails: u8x8 = simd_swizzle!(v, [2, 6, 10, 14, 18, 22, 26, 30]);
    if tails.simd_eq(Simd::splat(0)).any() {
        return true;
    }

    // A pair of equal bytes where a solid Fill would need one.
    let firsts: u8x8 = simd_swizzle!(v, [0, 4, 8, 12, 16, 20, 24, 28]);
    let seconds: u8x8 = simd_swizzle!(v, [1, 5, 9, 13, 17, 21, 25, 29]);
    if firsts.simd_eq(seconds).any() {
        return true;
    }

    // Eight bytes in range from a group start, which is what a passthrough
    // segment beginning there must carry among its first twenty.
    let in_range = |x: u8x32| {
        (x.simd_ge(Simd::splat(0x09)) & x.simd_le(Simd::splat(0x7E))).to_bitmask()
    };
    // The window is forty bytes and the vector thirty-two, so the last eight
    // come from a second load eight bytes along -- overlapping, and cheaper than
    // asking about them one at a time, which measured as most of this function.
    let bits = in_range(v) | (in_range(u8x32::from_slice(&w[8..8 + SKIP_BYTES])) << 8);
    // Bit i survives iff bits i..i+8 were all set.
    let runs = {
        let two = bits & (bits >> 1);
        let four = two & (two >> 2);
        four & (four >> 4)
    };
    // The eight group starts: bits 0, 4, ... 28.
    runs & 0x1111_1111 != 0
}

/// Section 4.3's substitution, applied sixteen bytes at a time.
///
/// The table the scalar loop reads is the identity over ASCII with the
/// segment's `k` donors patched in, and `k` is at most 13 -- on ordinary text
/// it is one to three, because a segment spends a donor only for each *distinct*
/// R-Set character in it. So the substitution is not a table lookup that has to
/// be vectorised; it is a handful of "replace this byte with that one", which is
/// a comparison and a blend each.
///
/// That is what makes this worth doing where a shuffle-based table would not be.
/// A general 128-entry lookup costs eight shuffles per sixteen bytes whatever
/// the segment contains; this costs two operations per donor, and most segments
/// have one or two.
///
/// `pairs` are the `(R-Set character, donor)` substitutions in effect, `src` the
/// segment's bytes and `dst` the characters to write. The masking mirrors the
/// scalar loop's `b & 0x7f` exactly: the scan accepts only ASCII, so it changes
/// nothing, and where it would the two paths still agree.
#[inline]
pub fn translate(pairs: &[(u8, u8)], src: &[u8], dst: &mut [u8]) {
    debug_assert_eq!(src.len(), dst.len());

    let (s_chunks, s_rest) = src.as_chunks::<LANES>();
    let (d_chunks, d_rest) = dst.as_chunks_mut::<LANES>();
    for (s, d) in s_chunks.iter().zip(d_chunks) {
        let mut v = u8x16::from_array(*s) & Simd::splat(0x7f);
        for &(rset, donor) in pairs {
            let hit = v.simd_eq(Simd::splat(rset));
            v = hit.select(Simd::splat(donor), v);
        }
        *d = v.to_array();
    }

    for (o, &b) in d_rest.iter_mut().zip(s_rest.iter()) {
        let b = b & 0x7f;
        *o = pairs.iter().find(|&&(rset, _)| rset == b).map_or(b, |&(_, donor)| donor);
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

    /// The question [`groups_may_hold`] answers, one group at a time: could any
    /// of the eight begin a Fill, a Fill with a tail, or a passthrough segment?
    ///
    /// The two Fill gates are exact and the passthrough one is the same
    /// eight-byte necessary condition the scalar gate uses, so this reference is
    /// the vector function's contract and not a weaker version of it: a `false`
    /// where the reference says `true` would let the encoder step over a
    /// decision point, which changes the output.
    fn scalar_groups_may_hold(w: &[u8; SKIP_WINDOW]) -> bool {
        (0..8).any(|g| {
            let q = 4 * g;
            w[q + 2] == 0
                || w[q + 1] == w[q]
                || (0..8).all(|i| (0x09..=0x7E).contains(&w[q + i]))
        })
    }

    #[test]
    fn groups_may_hold_never_rules_out_a_group_the_scalar_test_keeps() {
        let mut rng = Rng(0x1234_5678_9ABC_DEF1);
        for case in 0..50_000 {
            let mut w = [0u8; SKIP_WINDOW];
            for slot in w.iter_mut() {
                *slot = match rng.next() % 6 {
                    // Binary, where the answer is usually "no" ...
                    0..=2 => (rng.next() % 256) as u8,
                    // ... text, where it is usually "yes" ...
                    3 | 4 => b"abcdefghijklmnopqrstuvwxyz .,"[(rng.next() % 29) as usize],
                    // ... and the bytes the Fill gates are about.
                    _ => [0u8, 0, 0xFF, b'a'][(rng.next() % 4) as usize],
                };
            }
            if rng.next().is_multiple_of(4) {
                // A run, planted where a group boundary can see it.
                let at = 4 * ((rng.next() % 8) as usize);
                let b = w[at];
                for i in 0..1 + (rng.next() % 6) as usize {
                    if at + i < SKIP_WINDOW {
                        w[at + i] = b;
                    }
                }
            }
            assert_eq!(
                groups_may_hold(&w),
                scalar_groups_may_hold(&w),
                "case {case}: {w:?}"
            );
        }
    }

    /// Each gate on its own, planted at each of the eight group positions: the
    /// random windows above reach these, but not with the certainty a test of a
    /// shuffle's lane order deserves.
    #[test]
    fn every_gate_is_seen_at_every_group_position() {
        // A background no gate fires on: no zeros, no adjacent equals, and
        // outside the passthrough range.
        let background: Vec<u8> = (0..SKIP_WINDOW).map(|i| 0x81 + (i % 7) as u8).collect();
        let mut base = [0u8; SKIP_WINDOW];
        base.copy_from_slice(&background);
        assert!(!groups_may_hold(&base), "the background itself wakes a gate");

        for g in 0..8 {
            let q = 4 * g;

            let mut zero_tail = base;
            zero_tail[q + 2] = 0;
            assert!(groups_may_hold(&zero_tail), "tail gate missed at group {g}");

            let mut pair = base;
            pair[q + 1] = pair[q];
            assert!(groups_may_hold(&pair), "solid Fill gate missed at group {g}");

            let mut run = base;
            for i in 0..8 {
                run[q + i] = b'a';
            }
            assert!(groups_may_hold(&run), "passthrough gate missed at group {g}");
        }
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
