// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Encoding algorithm (spec section 6): the Fill scan of step 1, the DP prefix
//! scan of step 2, and the one-group Block Mode fallback of step 4.
//!
//! The interesting part is the prefix scan. It does not choose between eight
//! fixed alphabets; it *builds* the substitution for each segment out of two
//! fields, and has to keep both viable as it walks forward:
//!
//! - `mask` grows by one bit the first time each R-Set character occurs, and
//!   `k = popcount(mask)` is how many donors the segment is spending.
//! - Every literal Alphabet-N character rules out the profiles that would spend
//!   *it* as one of those `k` donors.
//!
//! A profile stays viable exactly while the lowest rank any literal character
//! holds in it is at least `k`, so the scan carries one number per profile --
//! that lowest rank -- and re-picks the smallest viable profile whenever either
//! side moves. Eight of those numbers fit in a `u64`, one per byte lane, which
//! is why [`lane_min`] and [`lane_ge`] exist: the per-byte update is then two
//! table lookups and a handful of ALU operations, independent of how many
//! profiles are still in play.
//!
//! Both scans are bounded by 2048 bytes and both consume what they scan, so
//! spec section 6.6's linear bound holds without any state carried between
//! iterations: the block-mode fallback inspects at most `MIN_PASSTHROUGH_BYTES`
//! bytes before consuming its four.

use crate::alphabet::{
    donors, DP_CLASS, DP_DONOR_BASE, DP_PLAIN, DP_RSET_BASE, DP_STOP, IS_REPRESENTABLE,
    RANK_ABSENT_ALL, RANK_PACKED,
};
#[cfg(not(feature = "simd"))]
use crate::alphabet::IDENTITY_ASCII;
use crate::constants::{
    DP_SIGNAL_BASE, FILL_SIGNAL_BASE, MAX_DP_ANALYSIS_BYTES, MAX_DP_SEGMENT_CHARS, MAX_FILL_BYTES,
    MAX_TAIL_ZEROS, MIN_FILL_BYTES, MIN_FILL_IN_SEGMENT_BYTES, MIN_PASSTHROUGH_BYTES,
    MIN_TAIL_ZEROS, TAIL_SIGNAL_BASE,
};
use crate::digits::{value_to_5chars_32, value_to_5chars_64};

/// What the output buffer is sized at up front, excluding growth.
///
/// Block mode emits exactly 1.25 characters per byte (plus at most 2 for a
/// partial final group), so this is the exact size an input with no Dynamic
/// Passthrough and no Fill in it needs -- which is every high-entropy input,
/// the case where the buffer is large enough for its size to matter. Neither
/// other mode exceeds it: DP spends one character per byte plus a 5-character
/// signal per 2048 bytes, and Fill spends five characters per five bytes or
/// more. The reserve below is kept for the short trailing cases and as the
/// single place every write is checked.
fn encode_capacity(n: usize) -> usize {
    n + n / 4 + 16
}

/// The encoder's only capacity test: once per emit rather than once per
/// character.
#[inline]
fn reserve(out: &mut Vec<u8>, w: usize, need: usize) {
    debug_assert!(w <= out.len(), "the cursor left the buffer");
    if need > out.len() - w {
        // A quarter of headroom, so repeated growth stays amortised without
        // overshooting far past what the input needs.
        let want = w + need;
        out.resize(want + want / 4, 0);
    }
}

/// All eight lanes' high bits, the carry-catchers of the two lane operations.
const LANE_HI: u64 = 0x8080_8080_8080_8080;

/// One in every lane, for broadcasting a scalar across all eight.
const LANE_ONES: u64 = 0x0101_0101_0101_0101;

/// Bit 7 of lane `p` set iff `x`'s lane `p` is `>=` `y`'s.
///
/// Setting each lane's high bit before subtracting keeps the lane difference in
/// `1..=255` whenever both operands are below 128, so no lane can borrow from
/// the next and the high bit is left holding the comparison.
#[inline]
fn lane_ge(x: u64, y: u64) -> u64 {
    ((x | LANE_HI) - y) & LANE_HI
}

/// Lane-wise minimum of two packings of values below 128.
#[inline]
fn lane_min(x: u64, y: u64) -> u64 {
    // 0xFF in each lane where x is the larger, 0 elsewhere: the shifted
    // comparison bit sits at the lane's low bit, and multiplying by 0xFF fills
    // that lane and only that lane.
    let m = (lane_ge(x, y) >> 7) * 0xFF;
    (x & !m) | (y & m)
}

/// The result of a Dynamic Passthrough prefix scan (spec section 6.2).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct DpPrefix {
    /// Length of the accepted prefix, in bytes.
    pub len: usize,
    /// Which R-Set characters occur in it.
    pub mask: u16,
    /// The smallest profile identifier that can carry them.
    pub profile: u8,
}

/// Everything the scan carries from one byte to the next. A struct so that the
/// loop below can hand it to one inlined step rather than repeat that step; all
/// of it lives in registers.
struct DpScan {
    mask: u16,
    profile: u8,
    /// How many R-Set characters `mask` names.
    k: u64,
    /// Per profile, the lowest rank a literal character has held in it.
    min_donor: u64,
    /// The state as it stood before the most recent change, and where that
    /// change happened. At most 35 changes can occur in a segment, so this
    /// costs nothing per byte.
    prev: (u16, u8),
    prev_pos: usize,
}

impl DpScan {
    fn new() -> Self {
        DpScan {
            mask: 0,
            profile: 0,
            k: 0,
            min_donor: RANK_ABSENT_ALL,
            prev: (0, 0),
            prev_pos: usize::MAX,
        }
    }

    /// Accounts for the first occurrence of `cls` in the segment, `b` being the
    /// byte it classifies. `false` means no profile can carry it and the
    /// segment has to end before it.
    ///
    /// This runs at most 35 times per segment -- once per R-Set character and
    /// once per donor -- so everything it costs is amortised over the bytes the
    /// caller's bit test retires. Which is why it is a call and not part of the
    /// loop: keeping it out leaves the loop holding one table load, one shift
    /// and one test.
    #[inline]
    fn account(&mut self, cls: u8, b: u8, pos: usize) -> bool {
        if cls < DP_DONOR_BASE {
            // One more donor to spend: every profile whose lowest literal rank
            // has been reached now drops out.
            let viable = lane_ge(self.min_donor, (self.k + 1) * LANE_ONES);
            if viable == 0 {
                return false;
            }
            self.prev = (self.mask, self.profile);
            self.prev_pos = pos;
            self.profile = (viable.trailing_zeros() >> 3) as u8;
            self.mask |= 1u16 << (cls - DP_RSET_BASE);
            self.k += 1;
        } else {
            let new_min = lane_min(self.min_donor, RANK_PACKED[b as usize]);
            if new_min == self.min_donor {
                // This character ranks below nothing already seen; the set of
                // viable profiles cannot have changed.
                return true;
            }
            let viable = lane_ge(new_min, self.k * LANE_ONES);
            if viable == 0 {
                return false;
            }
            self.prev = (self.mask, self.profile);
            self.prev_pos = pos;
            self.profile = (viable.trailing_zeros() >> 3) as u8;
            self.min_donor = new_min;
        }
        true
    }
}

/// The first byte, which has no predecessor to compare against and so is folded
/// in before the loop rather than making every byte that follows pay for a
/// bounds test. `false` means no profile can carry it and the prefix is empty.
fn fold_first(st: &mut DpScan, seen: &mut u64, b: u8) -> bool {
    let cls = DP_CLASS[b as usize];
    let bit = 1u64 << cls;
    if *seen & bit != 0 {
        return true;
    }
    if cls == DP_STOP {
        return false; // not representable under any mask or profile
    }
    *seen |= bit;
    st.account(cls, b, 0)
}

/// Step 2: the longest prefix of `window` that one profile can carry, with the
/// mask and profile in effect for it.
///
/// Per spec section 6.2 the state committed is the one in effect *before* the
/// byte that ended the scan, and the length is capped at
/// [`MAX_DP_ANALYSIS_BYTES`].
///
/// The scan also stops where a run of [`MIN_FILL_IN_SEGMENT_BYTES`] identical
/// bytes begins, which is what lets Fill reach runs *inside* passthrough text
/// (spec section 6.5, rule 1) and not only runs at a segment boundary. The
/// rolled-back state in [`DpScan`] is what that costs: a run's first byte may
/// have widened the mask or narrowed the profile choice, and those effects have
/// to be undone when the prefix ends before it. The bytes after the first
/// cannot have changed anything, since they are equal to a byte the scan has
/// already accounted for.
pub fn scan_dp(window: &[u8]) -> DpPrefix {
    let limit = window.len().min(MAX_DP_ANALYSIS_BYTES);
    let mut st = DpScan::new();
    // The one piece of state every byte touches, kept in a local of its own so
    // that the loop below holds it in a register and the struct is written only
    // where something changes.
    let mut seen = 1u64 << DP_PLAIN;
    // The same set the loop's bit test asks about, in the shape sixteen bytes
    // can be asked at once. It is an acceleration and never a decision; see
    // `crate::simd`.
    #[cfg(feature = "simd")]
    let mut skip = crate::simd::SkipSet::new();
    let mut i = 0usize;

    // The loop never steps into the middle of a run: where it meets a byte
    // equal to its predecessor it measures that run whole and jumps past it. So
    // a byte equal to its predecessor is always the *second* byte of its run,
    // and the run always begins exactly one byte back -- which is why nothing
    // here counts a run length per byte. The comparison is not bookkeeping but
    // the one test that says whether this byte opens a run at all, and the
    // predecessor is carried in a register rather than loaded twice.
    //
    // The first byte has no predecessor, so it is folded in before the loop
    // rather than paying for a bounds test on every byte that follows.
    if limit > 0 && fold_first(&mut st, &mut seen, window[0]) {
        #[cfg(feature = "simd")]
        skip.account(window[0]);
        i = 1;
        let mut prev = window[0];
        while i < limit {
            // Bytes that change nothing and open no run are steps of the loop
            // below with nothing in them, and sixteen of them are settled at
            // once. The scalar loop then takes the byte that stopped it.
            #[cfg(feature = "simd")]
            {
                while i + crate::simd::LANES <= limit {
                    let w: &[u8; crate::simd::LANES + 1] = window[i - 1..i + crate::simd::LANES]
                        .try_into()
                        .expect("the loop bound keeps this window inside the scan");
                    let run = skip.skippable(w);
                    if run == 0 {
                        break;
                    }
                    i += run;
                    prev = window[i - 1];
                    if run < crate::simd::LANES {
                        break;
                    }
                }
                if i >= limit {
                    break;
                }
            }

            let b = window[i];
            if b == prev {
                // Only whether the run reaches MIN_FILL_IN_SEGMENT_BYTES
                // matters, so the walk stops there -- and it walks bytes rather
                // than words. Text is full of two-byte repeats, `ll` and `==`
                // and a double space, and a word-wide scan would read eight
                // bytes to answer what the next byte settles.
                let start = i - 1;
                let stop = limit.min(start + MIN_FILL_IN_SEGMENT_BYTES);
                let mut e = i + 1;
                while e < stop && window[e] == b {
                    e += 1;
                }
                if e - start >= MIN_FILL_IN_SEGMENT_BYTES {
                    // `window[start..e]` are identical and long enough to be a
                    // Fill segment of their own, so this prefix ends at `start`.
                    let (mask, profile) = if st.prev_pos == start {
                        st.prev
                    } else {
                        (st.mask, st.profile)
                    };
                    return DpPrefix { len: start, mask, profile };
                }
                // Too short to hand to Fill, and every byte of it repeats one
                // already accounted for, so the run changes nothing the scan
                // tracks and can be stepped over whole. `prev` is already this
                // byte, which is what the run is made of.
                i = e;
                continue;
            }
            // The one test that decides for nearly every byte, and it is one
            // rather than two on purpose. A byte leaves the state alone for
            // either of two reasons -- no profile spends its character, or its
            // kind is already accounted for -- and asking those separately
            // means a branch that goes both ways on ordinary text, where about
            // a third of the bytes are punctuation some profile spends.
            // Nothing predicts such a branch. Since both reasons have the same
            // consequence, DP_PLAIN's bit is set in `seen` before the scan
            // starts and never cleared, and the two become one bit test that
            // holds for every byte but the at most 35 in a segment that change
            // something.
            //
            // It is spelled out here rather than behind a call because `seen`
            // has to stay in a register across the whole loop: reached through
            // a `&mut` it is a load and a store per byte, and the loop then
            // runs at the speed of that store-to-load dependency -- which
            // measured at two thirds of this encoder's text throughput.
            let cls = DP_CLASS[b as usize];
            let bit = 1u64 << cls;
            if seen & bit == 0 {
                if cls == DP_STOP {
                    break; // not representable under any mask or profile
                }
                if !st.account(cls, b, i) {
                    break;
                }
                seen |= bit;
                #[cfg(feature = "simd")]
                skip.account(b);
            }
            prev = b;
            i += 1;
        }
    }

    DpPrefix { len: i, mask: st.mask, profile: st.profile }
}

/// Step 1: the length of the run of identical bytes starting at `window[0]`,
/// capped at [`MAX_FILL_BYTES`].
#[inline]
fn fill_run(window: &[u8]) -> usize {
    let b = window[0];
    let limit = window.len().min(MAX_FILL_BYTES);
    let mut i = 1usize;
    while i < limit && window[i] == b {
        i += 1;
    }
    i
}

/// Whether all eight bytes of `word` lie in `[lo, hi]`. Both bounds must be at
/// most 127, which every bound used here is.
///
/// A lane below `lo` borrows into its own high bit while its own top bit is
/// clear; a lane above `hi` either has its top bit set already or carries into
/// it when `127 - hi` is added. Neither test needs to know which lane is which,
/// so neither depends on the endianness the word was loaded with.
#[inline]
fn lanes_within(word: u64, lo: u64, hi: u64) -> bool {
    let below = word.wrapping_sub(LANE_ONES * lo) & !word;
    let above = word.wrapping_add(LANE_ONES * (127 - hi)) | word;
    (below | above) & LANE_HI == 0
}

/// Whether a Dynamic Passthrough segment can begin at `data[q]`, as far as one
/// word and four table loads can tell.
///
/// A segment needs [`MIN_PASSTHROUGH_BYTES`] representable bytes from `q`, so
/// both halves of this are *necessary* conditions: neither may rule out a
/// position a segment could begin at, and both may fail to rule one out. The
/// scan behind it decides for real.
///
/// The word test goes first, because it is the one that rejects. It clears the
/// wider range `[0x09, 0x7E]` -- every representable byte lies in it, and 0x0B,
/// 0x0C and 0x0E to 0x1F additionally do -- because that is the range whole-word
/// arithmetic can settle without knowing which lane is which. Eight bytes are
/// one load and half a dozen register operations, against one table load per
/// byte, and on high-entropy binary they all fall in that range about one time
/// in five hundred: the branch under it predicts, where a single byte's
/// "representable?" is a coin flip resolved once per four bytes of input.
///
/// The four table loads then settle the four bytes exactly, folded with `&`
/// rather than `&&` so they contribute no branch of their own, and a walk that
/// continues from here may start past them.
///
/// Reads eight bytes from `q`; where fewer remain, `data.get` fails and the
/// answer is the conservative one.
#[inline]
fn dp_gate(data: &[u8], q: usize) -> bool {
    if let Some(w) = data.get(q..q + 8) {
        let word = u64::from_le_bytes(w.try_into().expect("an eight-byte slice"));
        if !lanes_within(word, 0x09, 0x7E) {
            return false;
        }
    }
    // Too near the end for the word test to say anything leaves the table to
    // decide; a stretch that short cannot reach the threshold anyway.
    match data.get(q..q + 4) {
        Some(g) => dp_table_gate(g),
        None => false,
    }
}

/// The table half of [`dp_gate`]: whether the first four bytes of `w` are all
/// representable, exactly. Folded with `&` rather than `&&` so the four loads
/// are unconditional and the test contributes no branch of its own.
#[inline]
fn dp_table_gate(w: &[u8]) -> bool {
    (IS_REPRESENTABLE[w[0] as usize]
        & IS_REPRESENTABLE[w[1] as usize]
        & IS_REPRESENTABLE[w[2] as usize]
        & IS_REPRESENTABLE[w[3] as usize])
        != 0
}

/// Whether the block-mode skip of spec section 11.1 runs. Always, outside a
/// test build.
#[cfg(not(test))]
#[inline(always)]
fn skip_enabled() -> bool {
    true
}

#[cfg(test)]
thread_local! {
    static SKIP_ENABLED: std::cell::Cell<bool> = const { std::cell::Cell::new(true) };
}

#[cfg(test)]
#[inline]
fn skip_enabled() -> bool {
    SKIP_ENABLED.with(|on| on.get())
}

/// Turns the skip off for this thread and returns what it was, so that a test
/// can encode the same input both ways. Thread-local because the test harness
/// runs tests in parallel and one of them must not decide for the others.
#[cfg(test)]
pub(crate) fn set_skip_enabled(on: bool) -> bool {
    SKIP_ENABLED.with(|flag| flag.replace(on))
}

/// What the two-group window test reads: both groups' own
/// [`MIN_PASSTHROUGH_BYTES`] windows, which is the second group's.
const WINDOW_BYTES: usize = 4 + MIN_PASSTHROUGH_BYTES;

/// Whether a decision point can begin at `win[0]` or at `win[4]`, as
/// `(passthrough, fill)`. Both clear means neither group can hold one and the
/// caller may skip both without testing either.
///
/// Widening the gate (see [`dp_gate`]) made the skip's branches predictable;
/// this is the same idea applied to the loop rather than to one test in it.
/// Each of the three steps has a gate that decides it, every one of those gates
/// reads inside the sixteen bytes from `win[0]`, and each becomes a question
/// about a whole word rather than about a named byte:
///
/// - Fill's zero variants are gated on `data[q + 2]`, which for the two groups
///   is `win[2]` and `win[6]`.
/// - Solid Fill is gated on a pair of equal adjacent bytes, `(win[0], win[1])`
///   and `(win[4], win[5])`.
/// - Dynamic Passthrough needs [`MIN_PASSTHROUGH_BYTES`] representable bytes
///   from its start, so a segment beginning at either group must carry `win[4]`
///   through `win[11]` among them. One range test over that word rules out both.
///
/// The two Fill gates are exact; the passthrough one is a necessary condition
/// and nothing more. None of the three may rule out a group a step applies at
/// -- they may only fail to rule one out, and [`decision_at`] then decides for
/// real.
///
/// A word-level stand-in for the Fill gates is worse and was not taken: "some
/// lane is zero" and "some adjacent pair is equal" hold constantly on the
/// zero-padded and run-heavy files Fill exists for, so the weaker test passes
/// where the exact one rejects and the work behind it is paid for nothing.
#[inline]
fn window_may_hold(win: &[u8; WINDOW_BYTES]) -> (bool, bool) {
    // Folded with `|` rather than `||` so the gates contribute one branch in the
    // caller rather than four.
    let fill = (win[2] == 0) | (win[6] == 0) | (win[0] == win[1]) | (win[4] == win[5]);
    let word = u64::from_le_bytes(win[4..12].try_into().expect("an eight-byte slice"));
    (lanes_within(word, 0x09, 0x7E), fill)
}

/// Whether any of the encoder's three steps can begin at `data[q]`.
///
/// `dp_maybe` is what the caller's window test already settled. Where it is
/// clear, no passthrough segment can begin here and the whole step is dropped
/// -- which is most of what this costs on zero-padded binary, where a Fill gate
/// wakes it at nearly every position and passthrough then fails at nearly every
/// one.
///
/// Each walk starts past the bytes its gate has already settled: the gate is
/// the walk's first step, and repeating it is most of what the walk does before
/// the byte after it ends the walk.
///
/// The caller guarantees `MIN_PASSTHROUGH_BYTES` bytes from `q` are inside the
/// input, which is more than any test here reads.
#[inline]
fn decision_at(data: &[u8], q: usize, dp_maybe: bool) -> bool {
    let w: &[u8; MIN_PASSTHROUGH_BYTES] = data[q..q + MIN_PASSTHROUGH_BYTES]
        .try_into()
        .expect("the caller keeps this window inside the input");

    // A Fill with a tail, in either order. Both need a zero at `q + 2` -- three
    // zeros have to reach it whether they start at `q` or at `q + 2` -- so one
    // load gates both scans.
    if w[2] == 0 {
        if w[..MIN_TAIL_ZEROS].iter().all(|&b| b == 0) {
            return true;
        }
        if w[2..2 + MIN_TAIL_ZEROS].iter().all(|&b| b == 0) {
            return true;
        }
    }
    if w[1] == w[0] {
        let mut e = 2;
        while e < MIN_FILL_BYTES && w[e] == w[0] {
            e += 1;
        }
        if e >= MIN_FILL_BYTES {
            return true;
        }
    }
    if dp_maybe && dp_table_gate(w) {
        let mut e = 4;
        while e < MIN_PASSTHROUGH_BYTES && IS_REPRESENTABLE[w[e] as usize] != 0 {
            e += 1;
        }
        if e >= MIN_PASSTHROUGH_BYTES {
            return true;
        }
    }
    false
}

/// The next position at or after `from` where the main loop could take a
/// branch other than block mode, given that it is inside a block-mode run and
/// therefore only ever *visits* positions `from`, `from + 4`, `from + 8`, ...
///
/// Only those positions have to be tested, and at each of them the three tests
/// are exact rather than heuristic: a Fill segment starts there iff
/// [`MIN_FILL_BYTES`] equal bytes do or [`MIN_TAIL_ZEROS`] zeros do (at the
/// position itself or two bytes in), and a DP segment can only start there if
/// [`MIN_PASSTHROUGH_BYTES`] representable bytes do. All bail out on their
/// first counterexample, which on high-entropy input is the second byte they
/// read -- so the whole test costs a handful of loads per 4 bytes consumed,
/// where running the real scans costs an order of magnitude more.
///
/// The caller may jump straight to the returned position: every position it
/// passes over would have taken step 4 and consumed exactly 4 bytes, and block
/// mode over a whole number of groups is the concatenation of the per-group
/// results, so the output is unchanged. Returning early is always sound; it
/// only costs the caller a decision it could have skipped.
///
/// `limit` bounds which positions are *tested*, not how far a test may read:
/// truncating the input instead would hide a decision point from the tests
/// near the bound and let the caller skip over it, which changes the output.
/// Returning `limit` early never does -- the caller simply re-decides there.
fn next_decision_point(data: &[u8], from: usize, limit: usize) -> usize {
    let n = data.len();
    let mut q = from;

    // Every test below reads at most `MIN_PASSTHROUGH_BYTES` bytes from `q`, so
    // while that window is inside the input, none of them needs a bound test of
    // its own -- the window is taken as a fixed-size array once per position and
    // indexed with constants after that. That is what keeps the tail scan off
    // the cost of high-entropy input, where it can never fire.
    let fast_end = limit.min(n.saturating_sub(MIN_PASSTHROUGH_BYTES));

    // Eight groups at a time where the `simd` feature has the vector to ask
    // with, two at a time after that, one at a time after that. Every loop asks
    // the same question of the same positions; the wider ones get to ask it of
    // more bytes at once, and hand over to the exact tests the moment one of
    // them cannot answer.
    //
    // The window this reads is inside the input by the loop's own bound:
    // `q + SKIP_BYTES <= fast_end <= n - MIN_PASSTHROUGH_BYTES` leaves twenty
    // bytes past the last group, and the window needs eight.
    #[cfg(feature = "simd")]
    while q + crate::simd::SKIP_BYTES <= fast_end {
        let w: &[u8; crate::simd::SKIP_WINDOW] = data[q..q + crate::simd::SKIP_WINDOW]
            .try_into()
            .expect("the loop bound keeps this window inside the input");
        if !crate::simd::groups_may_hold(w) {
            q += crate::simd::SKIP_BYTES;
            continue;
        }
        // One of the eight could hold something, and which one is for the exact
        // test to say. It settles these eight and the vector takes over again --
        // leaving the loop here instead would hand the whole rest of the input
        // to the narrow path, and on binary something wakes this roughly once in
        // five windows.
        let settled = q + crate::simd::SKIP_BYTES;
        while q < settled {
            if decision_at(data, q, true) {
                return q;
            }
            q += 4;
        }
    }

    while q + 4 <= fast_end {
        let win: &[u8; WINDOW_BYTES] = data[q..q + WINDOW_BYTES]
            .try_into()
            .expect("the loop bound keeps this window inside the input");
        let (dp, fill) = window_may_hold(win);
        if !(dp | fill) {
            q += 8;
            continue;
        }
        if decision_at(data, q, dp) {
            return q;
        }
        if decision_at(data, q + 4, dp) {
            return q + 4;
        }
        q += 8;
    }

    // No window test ran for these, so nothing is settled and every step is
    // still in play.
    while q < fast_end {
        if decision_at(data, q, true) {
            return q;
        }
        q += 4;
    }

    while q < limit {
        if q + 2 < n && data[q + 2] == 0 {
            let zeros_at = |s: usize| {
                let limit = n.min(s + MIN_TAIL_ZEROS);
                let mut e = s;
                while e < limit && data[e] == 0 {
                    e += 1;
                }
                e - s >= MIN_TAIL_ZEROS
            };
            if zeros_at(q) && q + MIN_TAIL_ZEROS + 2 <= n {
                return q;
            }
            if zeros_at(q + 2) {
                return q;
            }
        }
        if q + 1 < n && data[q + 1] == data[q] {
            let limit = n.min(q + MIN_FILL_BYTES);
            let mut e = q + 1;
            while e < limit && data[e] == data[q] {
                e += 1;
            }
            if e - q >= MIN_FILL_BYTES {
                return q;
            }
        }
        if IS_REPRESENTABLE[data[q] as usize] != 0 {
            let limit = n.min(q + MIN_PASSTHROUGH_BYTES);
            let mut e = q;
            while e < limit && IS_REPRESENTABLE[data[e] as usize] != 0 {
                e += 1;
            }
            if e - q >= MIN_PASSTHROUGH_BYTES {
                return q;
            }
        }
        q += 4;
    }
    limit
}

/// The 5-character Fill signal, solid variant: `byte` repeated `len` times
/// (spec section 9).
fn fill_signal(byte: u8, len: usize) -> [u8; 5] {
    debug_assert!((MIN_FILL_BYTES..=MAX_FILL_BYTES).contains(&len));
    let payload = ((byte as u64) << 11) | (len as u64 - 1);
    value_to_5chars_64(FILL_SIGNAL_BASE + payload)
}

/// The 5-character Fill signal, tail variant: `zeros` zero bytes and the two
/// literals, in the order `order` names (spec section 9).
fn tail_signal(zeros: usize, order: u8, lit: [u8; 2]) -> [u8; 5] {
    debug_assert!((MIN_TAIL_ZEROS..=MAX_TAIL_ZEROS).contains(&zeros));
    let payload = ((order as u64) << 21)
        | ((zeros as u64 - 1) << 16)
        | ((lit[0] as u64) << 8)
        | (lit[1] as u64);
    value_to_5chars_64(TAIL_SIGNAL_BASE + payload)
}

/// What step 1 decided: which Fill signal to spend, and how many bytes it
/// covers.
struct FillChoice {
    signal: [u8; 5],
    covers: usize,
}

/// Step 1 (spec section 6.1): the Fill signal to spend at `window`, if any.
///
/// Both variants cost five characters, so the one that covers more bytes wins
/// and a tie goes to the solid variant. `run` is the run of identical bytes at
/// `window[0]`, which the caller has already measured and which is also the
/// zero run when that byte is zero.
fn choose_fill(window: &[u8], run: usize) -> Option<FillChoice> {
    let mut covers = if run >= MIN_FILL_BYTES { run } else { 0 };
    let mut choice = None;

    if window[0] == 0 {
        let zeros = run.min(MAX_TAIL_ZEROS);
        if zeros >= MIN_TAIL_ZEROS && zeros + 2 <= window.len() && zeros + 2 > covers {
            covers = zeros + 2;
            choice = Some(tail_signal(zeros, 0, [window[zeros], window[zeros + 1]]));
        }
    }
    if window.len() >= 3 && window[2] == 0 {
        let zeros = zero_run(&window[2..]);
        if zeros >= MIN_TAIL_ZEROS && zeros + 2 > covers {
            covers = zeros + 2;
            choice = Some(tail_signal(zeros, 1, [window[0], window[1]]));
        }
    }

    if covers == 0 {
        return None;
    }
    Some(FillChoice {
        signal: choice.unwrap_or_else(|| fill_signal(window[0], run)),
        covers,
    })
}

/// The run of zero bytes at `window[0]`, capped where the tail variant's
/// 5-bit length field saturates.
#[inline]
fn zero_run(window: &[u8]) -> usize {
    let limit = window.len().min(MAX_TAIL_ZEROS);
    let mut i = 0usize;
    while i < limit && window[i] == 0 {
        i += 1;
    }
    i
}

/// The 5-character DP signal for a segment (spec section 9).
fn dp_signal(prefix: &DpPrefix) -> [u8; 5] {
    debug_assert!((1..=MAX_DP_SEGMENT_CHARS).contains(&prefix.len));
    let payload = ((prefix.profile as u64) << 24)
        | ((prefix.mask as u64) << 11)
        | (prefix.len as u64 - 1);
    value_to_5chars_64(DP_SIGNAL_BASE + payload)
}

/// One worker's share of a parallel encode: the characters it produced, where
/// in the input it stopped, and the positions it could be spliced at.
pub struct Part {
    /// The characters produced from `start` to [`Part::end`].
    pub out: String,
    /// `(input position, character offset in `out`)` for every position where
    /// the encoder had no block-mode group pending -- the positions at which
    /// its output is self-contained, and therefore the positions another
    /// encoder can join it at. Ascending.
    pub points: Vec<(usize, usize)>,
    /// The input position one past everything `out` accounts for.
    pub end: usize,
}

/// Encode `data` as a Base85N string.
pub fn encode(data: &[u8]) -> String {
    encode_range(data, 0, data.len(), None, false).out
}

/// The encoding loop, over `data[start..]`, stopping once it has consumed
/// through `stop` -- and reading past `stop` where a construct crosses it,
/// which is why this takes the whole input and a bound rather than a slice.
///
/// `meet`, when given, is a sorted list of another encoder's splice points:
/// the loop stops at the first position it shares with that list, which is
/// what repairs a seam. `record` asks for [`Part::points`] to be collected.
pub fn encode_range(
    data: &[u8],
    start: usize,
    stop: usize,
    meet: Option<&[(usize, usize)]>,
    record: bool,
) -> Part {
    // The buffer is sized once and written through a cursor, rather than pushed
    // into: the length is known for every emit, so a per-group capacity test has
    // nothing to decide, and writing by position lets the block-mode loop carry
    // its bounds information into `as_chunks_mut`.
    let mut out: Vec<u8> = vec![0u8; encode_capacity(stop - start) + 16];
    let mut w = 0usize;
    let mut pos = start;
    // No `with_capacity`, which looks like an oversight and is not: reserving
    // was measured and does not pay. A worker records a point wherever nothing
    // is pending, which on mixed input is every 42 bytes -- 100,000 of them,
    // 1.6 MB, for a 4 MB chunk -- and on high-entropy input is twice in a whole
    // file, because the skip crosses it without ever putting the encoder in a
    // state with nothing pending. Sizing for the first wastes megabytes on the
    // second, and best of five interleaved rounds of a four-thread 16 MB encode
    // put `Vec::new()`, a 4096-entry reservation and a length-derived one
    // within 3 % of each other, in changing order: the growth is not where the
    // time goes.
    let mut points: Vec<(usize, usize)> = Vec::new();

    // The substitution table the last DP segment used, and the profile and mask
    // it was built for. `u32::MAX` is no key: a real one is a profile below 8
    // shifted into the high half.
    #[cfg(not(feature = "simd"))]
    let mut xlat = IDENTITY_ASCII;
    // With the `simd` feature the substitution is applied as the pairs it was
    // built from -- a comparison and a blend each, sixteen bytes at a time --
    // so the table itself is never built.
    #[cfg(feature = "simd")]
    let (mut xlat_pairs, mut xlat_k) = ([(0u8, 0u8); crate::alphabet::RSET_LEN], 0usize);
    let mut xlat_key = u32::MAX;

    // Start of the pending run of block-mode bytes, or `usize::MAX` for none.
    // Consecutive block-mode iterations are converted in one call instead of
    // four bytes at a time. That does not change the output: block mode
    // consumes exactly one 4-byte group per iteration, and block mode over a
    // whole number of groups is the concatenation of the per-group results.
    let mut block_start = usize::MAX;

    while pos < data.len() {
        // A position with no group pending is a position where everything
        // emitted so far stands on its own, and the encoder's state is exactly
        // `pos`. Those are the positions two encoders can agree at.
        if block_start == usize::MAX {
            if pos >= stop {
                break;
            }
            if let Some(list) = meet {
                if pos > start && list.binary_search_by_key(&pos, |&(p, _)| p).is_ok() {
                    break;
                }
            }
            if record {
                points.push((pos, w));
            }
        }

        // Step 1: a run worth a signal of its own -- either variant of Fill.
        // Five characters for up to 2048 identical bytes, or for a short zero
        // run together with the two bytes beside it, which block mode would
        // otherwise charge 1.25 characters each for.
        let run = fill_run(&data[pos..]);
        if let Some(fill) = choose_fill(&data[pos..], run) {
            if block_start != usize::MAX {
                w += flush_block(data, block_start, pos, &mut out, w);
                block_start = usize::MAX;
            }
            reserve(&mut out, w, 5);
            out[w..w + 5].copy_from_slice(&fill.signal);
            w += 5;
            pos += fill.covers;
            continue;
        }

        // Steps 2 and 3. The scan's answer is used only when it reaches
        // MIN_PASSTHROUGH_BYTES, so anything that settles in advance that it
        // cannot retires the call outright: a stretch shorter than the
        // threshold, or one the gate rules out. That is worth a test of its
        // own because of where the loop arrives here from -- a Fill segment
        // continues straight back to the top, so every one of them is followed
        // by a scan at the next position, thousands of them on a zero-padded
        // object file, each initialising the scan's state and then failing on
        // its second or third byte.
        let prefix = if data.len() - pos >= MIN_PASSTHROUGH_BYTES && dp_gate(data, pos) {
            scan_dp(&data[pos..])
        } else {
            DpPrefix { len: 0, mask: 0, profile: 0 }
        };
        if prefix.len >= MIN_PASSTHROUGH_BYTES {
            // At MIN_PASSTHROUGH_BYTES the two modes cost the same 25
            // characters and DP only gains from there, so the length test
            // settles step 3's size comparison too.
            debug_assert!(5 + prefix.len <= prefix.len.div_ceil(4) * 5);
            if block_start != usize::MAX {
                w += flush_block(data, block_start, pos, &mut out, w);
                block_start = usize::MAX;
            }

            reserve(&mut out, w, 5 + prefix.len);
            out[w..w + 5].copy_from_slice(&dp_signal(&prefix));
            w += 5;

            // Section 4.3's substitution, as a table: the identity over ASCII
            // with the segment's `k` donors patched in. Every byte a DP segment
            // can carry is ASCII, so 128 entries cover it.
            //
            // Segments do not choose their profile and mask independently of
            // one another -- pretty-printed JSON is a long run of segments
            // carrying the same R-Set characters -- so the last table built is
            // kept and most segments skip the rebuild entirely.
            let key = ((prefix.profile as u32) << 16) | prefix.mask as u32;
            if xlat_key != key {
                let (pairs, k) = donors(prefix.profile as usize, prefix.mask);
                #[cfg(not(feature = "simd"))]
                {
                    xlat = IDENTITY_ASCII;
                    for &(rset, donor) in &pairs[..k] {
                        xlat[rset as usize] = donor;
                    }
                }
                #[cfg(feature = "simd")]
                {
                    xlat_pairs = pairs;
                    xlat_k = k;
                }
                xlat_key = key;
            }

            let src = &data[pos..pos + prefix.len];
            let dst = &mut out[w..w + prefix.len];
            #[cfg(feature = "simd")]
            crate::simd::translate(&xlat_pairs[..xlat_k], src, dst);
            #[cfg(not(feature = "simd"))]
            for (o, &b) in dst.iter_mut().zip(src.iter()) {
                debug_assert!(b < 128, "the scan accepts only ASCII bytes");
                *o = xlat[(b & 0x7f) as usize];
            }
            w += prefix.len;
            pos += prefix.len;
            continue;
        }

        // Step 4, block-mode fallback: exactly one 4-byte group, however long
        // the failed candidate was. Nothing but the end of the input can hand
        // `process_with_block_mode` a partial group this way.
        if block_start == usize::MAX {
            block_start = pos;
        }
        pos += (data.len() - pos).min(4);

        // Every position up to the next decision point takes this same branch,
        // so jump to it rather than re-deciding every four bytes.
        //
        // Spec section 11.1 makes this an optimisation and nothing else: an
        // encoder that skips must emit exactly what one that re-decides emits.
        // `skip_enabled` is what lets the test suite build the second one out of
        // the first (`tests::skip`); outside a test build it is the constant
        // `true` and costs nothing.
        //
        // The gate is what keeps the lookahead off the path it cannot help:
        // where the next byte is representable, a DP candidate starts right
        // here and the scan the loop is about to run is the cheaper way to
        // find out how far it reaches. Where it is not, the lookahead runs
        // over binary, which is exactly where it earns its keep.
        if skip_enabled() && pos < data.len() && IS_REPRESENTABLE[data[pos] as usize] == 0 {
            // The skip is bounded by `stop`, so that a worker encoding one
            // chunk of a parallel encode does not run a block-mode stretch to
            // the end of the file. For a whole-input encode the bound is the
            // input and nothing changes.
            let next = next_decision_point(data, pos, data.len().min(stop.max(pos)));
            pos += ((next - pos) / 4) * 4;
        }
        // Breaking here leaves a whole number of block-mode groups pending --
        // every iteration of this branch consumes four bytes -- so the flush
        // below emits exactly what continuing would have emitted.
        if pos >= stop {
            break;
        }
    }

    if block_start != usize::MAX {
        // What the parallel splice rests on: a worker that stopped short of the
        // input's end leaves a whole number of block-mode groups behind, so its
        // output is the concatenation of complete constructs and another
        // encoder can be joined to it. Only the real end of the input may leave
        // a partial group, which is the one place padding is emitted.
        debug_assert!(
            pos >= data.len() || (pos - block_start).is_multiple_of(4),
            "a worker stopping at {pos} left {} bytes of a block-mode group pending",
            (pos - block_start) % 4
        );
        w += flush_block(data, block_start, pos, &mut out, w);
    }

    // Ascending, because the splice looks them up by binary search, and one per
    // position where nothing was pending -- which is what makes them positions
    // another encoder can join at.
    debug_assert!(points.windows(2).all(|p| p[0].0 < p[1].0), "splice points out of order");

    out.truncate(w);
    // Every byte written is an Alphabet-N character, all of which are ASCII.
    debug_assert!(out.is_ascii());
    Part {
        out: String::from_utf8(out).expect("encoder emits only Alphabet-N characters"),
        points,
        end: pos,
    }
}

/// Below this, a chunk is not worth a thread: the seam repair is measured in
/// tens of kilobytes (spec section 11.3), so chunks have to be far larger than
/// that for the speculation to pay.
pub const MIN_PARALLEL_CHUNK: usize = 1 << 20;

/// Encode `data` on up to `threads` threads, producing exactly what
/// [`encode`] produces.
///
/// The format makes this possible without a chunk-size parameter, and
/// therefore without a second canonical form: signals carry their own mask,
/// profile, value, length and order, and segment boundaries are decided by the
/// data rather than by where an encoder started. Two encoders that begin at
/// different offsets therefore converge -- see spec section 11.3, which also
/// carries the measured convergence distances.
///
/// Each worker encodes its chunk speculatively and records the positions at
/// which its output stands on its own. The seams are then resolved in order:
/// where the previous worker really ended is looked up in the next worker's
/// positions, and where it is not found, that stretch is re-encoded until the
/// two chains meet. What is discarded is speculation, never output.
pub fn encode_parallel(data: &[u8], threads: usize) -> String {
    let threads = threads.max(1);
    if threads == 1 || data.len() < 2 * MIN_PARALLEL_CHUNK {
        return encode(data);
    }

    let chunk = (data.len() / threads).max(MIN_PARALLEL_CHUNK);
    let starts: Vec<usize> = (0..).map(|i| i * chunk).take_while(|&s| s < data.len()).collect();

    let parts: Vec<Part> = std::thread::scope(|scope| {
        let handles: Vec<_> = starts
            .iter()
            .enumerate()
            .map(|(i, &begin)| {
                let end = starts.get(i + 1).copied().unwrap_or(data.len());
                // Only a worker whose output may have to be joined mid-chunk
                // pays for recording its positions.
                scope.spawn(move || encode_range(data, begin, end, None, i > 0))
            })
            .collect();
        handles.into_iter().map(|h| h.join().expect("worker panicked")).collect()
    });

    let mut out = String::with_capacity(encode_capacity(data.len()));
    out.push_str(&parts[0].out);
    let mut cursor = parts[0].end;

    for part in &parts[1..] {
        // The previous stretch may have run past this whole chunk.
        if cursor >= part.end {
            continue;
        }
        if let Ok(i) = part.points.binary_search_by_key(&cursor, |&(p, _)| p) {
            out.push_str(&part.out[part.points[i].1..]);
            cursor = part.end;
            continue;
        }
        // The seam: encode forward until the two chains meet.
        let repair = encode_range(data, cursor, part.end, Some(&part.points), false);
        out.push_str(&repair.out);
        cursor = repair.end;
        if let Ok(i) = part.points.binary_search_by_key(&cursor, |&(p, _)| p) {
            out.push_str(&part.out[part.points[i].1..]);
            cursor = part.end;
        }
    }

    if cursor < data.len() {
        out.push_str(&encode_range(data, cursor, data.len(), None, false).out);
    }
    out
}

/// Convert the accumulated block-mode range `data[start..end]`, growing `out`
/// first, and return how many characters were written at `w`.
fn flush_block(data: &[u8], start: usize, end: usize, out: &mut Vec<u8>, w: usize) -> usize {
    let n = end - start;
    let need = n / 4 * 5 + if n.is_multiple_of(4) { 0 } else { n % 4 + 1 };
    reserve(out, w, need);
    process_with_block_mode(&data[start..end], &mut out[w..])
}

/// Block mode (spec section 6.3). Per the main loop, only a genuinely final
/// remainder is padded; every other iteration consumes a whole group, so a
/// `buf` that is not a multiple of 4 reaches here only at the end of the input.
///
/// Writes at most `(buf.len()/4)*5 + 4` characters at the front of `dst`, and
/// returns how many. Zipped exact chunks carry both lengths into the loop, so
/// neither side is bounds-checked per group.
fn process_with_block_mode(buf: &[u8], dst: &mut [u8]) -> usize {
    let full_len = buf.len() - buf.len() % 4;
    let groups = full_len / 4;

    // Four groups per iteration. Each group's digit extraction is a short
    // dependency chain with very little to fill it, and neighbouring groups are
    // entirely independent, so issuing four together keeps the multipliers busy
    // and pays the loop's own bookkeeping once per sixteen bytes rather than
    // once per four. Exact chunks carry both lengths into the loop, so neither
    // side is bounds-checked per group.
    let quads = full_len / 16;
    let (qin, _) = buf[..quads * 16].as_chunks::<16>();
    let (qout, _) = dst[..quads * 20].as_chunks_mut::<20>();
    for (src, chars) in qin.iter().zip(qout) {
        let (groups4, _) = src.as_chunks::<4>();
        let a = value_to_5chars_32(u32::from_be_bytes(groups4[0]));
        let b = value_to_5chars_32(u32::from_be_bytes(groups4[1]));
        let c = value_to_5chars_32(u32::from_be_bytes(groups4[2]));
        let d = value_to_5chars_32(u32::from_be_bytes(groups4[3]));
        chars[0..5].copy_from_slice(&a);
        chars[5..10].copy_from_slice(&b);
        chars[10..15].copy_from_slice(&c);
        chars[15..20].copy_from_slice(&d);
    }

    // The groups the quads did not cover, one at a time.
    let (ins, _) = buf[quads * 16..full_len].as_chunks::<4>();
    let (outs, _) = dst[quads * 20..groups * 5].as_chunks_mut::<5>();
    for (src, chars) in ins.iter().zip(outs) {
        *chars = value_to_5chars_32(u32::from_be_bytes(*src));
    }

    let rem = buf.len() - full_len;
    if rem > 0 {
        let mut padded = [0u8; 4];
        padded[..rem].copy_from_slice(&buf[full_len..]);
        let group = value_to_5chars_32(u32::from_be_bytes(padded));
        // Take the first rem+1 characters.
        dst[groups * 5..groups * 5 + rem + 1].copy_from_slice(&group[..rem + 1]);
        groups * 5 + rem + 1
    } else {
        groups * 5
    }
}

#[cfg(test)]
mod lane_tests {
    use super::*;
    use crate::alphabet::{NOT_REPRESENTABLE, NUM_PROFILES, PROFILES, RSET_ASCII, RSET_LEN};

    /// A deterministic word source for the property tests below.
    fn words(seed: u64) -> impl Iterator<Item = u64> {
        let mut state = seed;
        std::iter::repeat_with(move || {
            state ^= state << 13;
            state ^= state >> 7;
            state ^= state << 17;
            state
        })
    }

    /// [`lane_ge`] and [`lane_min`] against the per-lane arithmetic they stand
    /// for, over random inputs rather than a handful of chosen ones.
    ///
    /// Their correctness rests on a borrow not crossing a lane boundary, which
    /// is a property of the *values* in the lanes -- both operands below 128 --
    /// and not of the shape of the word. Chosen inputs are the wrong instrument
    /// for that: they cover the cases someone thought of.
    #[test]
    fn lane_operations_agree_over_random_words() {
        // Both operations are documented for lanes below 128, which is what the
        // scan puts in them: ranks 0..=13 and RANK_ABSENT_LANE.
        let masked = |w: u64| w & 0x7f7f_7f7f_7f7f_7f7f;
        for (x, y) in words(0x9E37_79B9_7F4A_7C15).zip(words(0xD1B5_4A32_D192_ED03)).take(100_000) {
            let (x, y) = (masked(x), masked(y));
            let min = lane_min(x, y);
            let ge = lane_ge(x, y);
            for p in 0..8 {
                let xp = (x >> (8 * p)) & 0xff;
                let yp = (y >> (8 * p)) & 0xff;
                assert_eq!((min >> (8 * p)) & 0xff, xp.min(yp), "lane {p} of {x:#018x}/{y:#018x}");
                assert_eq!(
                    (ge >> (8 * p)) & 0x80 != 0,
                    xp >= yp,
                    "lane {p} of {x:#018x}/{y:#018x}"
                );
            }
        }
    }

    /// [`lanes_within`] against the same question asked one byte at a time.
    ///
    /// The hard requirement is one-sided: a word whose lanes all lie in the
    /// range must never be rejected, because the encoder reads a rejection as
    /// "no passthrough segment can begin in here" and skips the positions. The
    /// converse may be conservative. It is in fact exact, and this asserts
    /// that too -- one instrument for both, so that a change that made it
    /// merely conservative would be noticed rather than assumed.
    ///
    /// Exhaustive per lane position: every byte value in every lane, against a
    /// background of values inside and outside the range. Then random words,
    /// where several lanes are wrong at once.
    #[test]
    fn lanes_within_agrees_with_the_bytes_it_stands_for() {
        let scalar = |w: u64, lo: u64, hi: u64| (0..8).all(|p| (lo..=hi).contains(&((w >> (8 * p)) & 0xff)));
        let bounds = [(0x09u64, 0x7Eu64), (0x00, 0x7f), (0x20, 0x40)];

        for &(lo, hi) in &bounds {
            for background in [0x00u8, 0x09, 0x41, 0x7E, 0x7F, 0x80, 0xFF] {
                for lane in 0..8 {
                    for value in 0..=255u8 {
                        let mut bytes = [background; 8];
                        bytes[lane] = value;
                        let w = u64::from_le_bytes(bytes);
                        assert_eq!(
                            lanes_within(w, lo, hi),
                            scalar(w, lo, hi),
                            "lane {lane} = {value:#04x}, background {background:#04x}, \
                             range {lo:#04x}..={hi:#04x}"
                        );
                    }
                }
            }
            for w in words(0x2545_F491_4F6C_DD1D).take(200_000) {
                assert_eq!(lanes_within(w, lo, hi), scalar(w, lo, hi), "{w:#018x}");
                // And the same word squeezed into the range, which is the case
                // that must never be rejected.
                let inside = w & 0x3f3f_3f3f_3f3f_3f3f | 0x2020_2020_2020_2020;
                if scalar(inside, lo, hi) {
                    assert!(lanes_within(inside, lo, hi), "{inside:#018x} was rejected");
                }
            }
        }
    }

    /// The scalar meaning of the two lane operations, spelled out.
    #[test]
    fn lane_operations_agree_with_per_lane_arithmetic() {
        let cases = [0u64, 1, 12, 13, 127, RANK_ABSENT_ALL, 0x000d_0700_0102_0c0d];
        for &x in &cases {
            for &y in &cases {
                let min = lane_min(x, y);
                let ge = lane_ge(x, y);
                for p in 0..8 {
                    let xp = (x >> (8 * p)) & 0xff;
                    let yp = (y >> (8 * p)) & 0xff;
                    assert!(xp < 128 && yp < 128, "test inputs stay in range");
                    assert_eq!((min >> (8 * p)) & 0xff, xp.min(yp));
                    assert_eq!((ge >> (8 * p)) & 0x80 != 0, xp >= yp);
                }
            }
        }
    }

    /// The scan reads one class per byte and nothing else, so every property it
    /// relies on has to hold of the table rather than of the code that used to
    /// ask three questions in sequence.
    #[test]
    fn dp_classes_agree_with_the_tables_they_replace() {
        let mut donor_slots = 0u8;
        for b in 0..=255u8 {
            let cls = DP_CLASS[b as usize];
            let representable = IS_REPRESENTABLE[b as usize] != 0;
            assert_eq!(
                cls == DP_STOP,
                !representable,
                "{b:#04x}: DP_STOP must be exactly the non-representable bytes"
            );
            if cls == DP_STOP {
                assert_eq!(RANK_PACKED[b as usize], NOT_REPRESENTABLE);
                continue;
            }
            assert!(cls < 63, "{b:#04x}: every class but DP_STOP is a real bit");
            if (DP_RSET_BASE..DP_DONOR_BASE).contains(&cls) {
                assert_eq!(RSET_ASCII[(cls - DP_RSET_BASE) as usize], b);
            } else if cls == DP_PLAIN {
                // What lets the scan pass over it without touching the minimum.
                assert_eq!(RANK_PACKED[b as usize], RANK_ABSENT_ALL);
                assert!(PROFILES.iter().all(|p| !p.contains(&b)));
            } else {
                donor_slots += 1;
                assert!(PROFILES.iter().any(|p| p.contains(&b)));
                assert_ne!(RANK_PACKED[b as usize], RANK_ABSENT_ALL);
            }
        }
        // Every distinct class is a bit of one u64, and each can be accounted
        // for at most once, which is what bounds the scan's per-segment work.
        assert_eq!(donor_slots as usize + RSET_LEN, 35);
    }

    #[test]
    fn packed_ranks_match_the_profile_table() {
        for (p, profile) in PROFILES.iter().enumerate() {
            for (rank, &c) in profile.iter().enumerate() {
                let lane = (RANK_PACKED[c as usize] >> (8 * p)) & 0xff;
                assert_eq!(lane, rank as u64, "{} in profile {}", c as char, p);
            }
        }
        // A character no profile spends ranks "absent" everywhere.
        assert_eq!(RANK_PACKED[b'a' as usize], RANK_ABSENT_ALL);
        assert_eq!(RANK_PACKED[0], NOT_REPRESENTABLE);
    }

    #[test]
    fn profiles_are_thirteen_distinct_alphabet_characters() {
        for (p, profile) in PROFILES.iter().enumerate() {
            assert_eq!(profile.len(), RSET_LEN);
            for (i, &c) in profile.iter().enumerate() {
                assert!(
                    crate::alphabet::ALPHABET_VALUE[c as usize] >= 0,
                    "profile {} rank {} is not in Alphabet-N",
                    p,
                    i
                );
                assert!(
                    !profile[..i].contains(&c),
                    "profile {} repeats {}",
                    p,
                    c as char
                );
            }
        }
        assert_eq!(PROFILES.len(), NUM_PROFILES);
    }
}
