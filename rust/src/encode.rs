// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Encoding algorithm (spec section 6): the Fill scan of step 1, the DP prefix
//! scan of step 2, and the one-group Block Mode fallback of step 4.
//!
//! The interesting part is the prefix scan. Version 0.4.0 does not choose
//! between eight fixed alphabets; it *builds* the substitution for each
//! segment out of two fields, and the scan has to keep both viable as it walks
//! forward:
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
    donors, IDENTITY_ASCII, IS_REPRESENTABLE, NOT_REPRESENTABLE, RANK_ABSENT_ALL, RANK_PACKED,
    RSET_INDEX,
};
use crate::constants::{
    DP_SIGNAL_BASE, FILL_SIGNAL_BASE, MAX_DP_ANALYSIS_BYTES, MAX_DP_SEGMENT_CHARS, MAX_FILL_BYTES,
    MIN_FILL_BYTES, MIN_FILL_IN_SEGMENT_BYTES, MIN_PASSTHROUGH_BYTES,
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
/// rolled-back state below is what that costs: a run's first byte may have
/// widened the mask or narrowed the profile choice, and those effects have to
/// be undone when the prefix ends before it. The bytes after the first cannot
/// have changed anything, since they are equal to a byte the scan has already
/// accounted for.
pub fn scan_dp(window: &[u8]) -> DpPrefix {
    let limit = window.len().min(MAX_DP_ANALYSIS_BYTES);

    let mut mask: u16 = 0;
    let mut k: u64 = 0;
    // Lane p: the lowest rank any literal character seen so far holds in
    // profile p. Nothing has been seen, so nothing is ruled out.
    let mut min_donor: u64 = RANK_ABSENT_ALL;
    let mut profile: u8 = 0;

    // The state as it stood before the most recent change, and where that
    // change happened. At most 26 changes can occur in a segment -- 13 mask
    // bits and 13 narrowings of the profile choice -- so this costs nothing per
    // byte.
    let mut prev = (0u16, 0u8);
    let mut prev_pos = usize::MAX;

    // Length of the run of identical bytes ending just before `i`.
    let mut run = 0usize;

    let mut i = 0usize;
    while i < limit {
        let b = window[i];

        if i > 0 && b == window[i - 1] {
            run += 1;
            if run + 1 >= MIN_FILL_IN_SEGMENT_BYTES {
                // `window[start..=i]` are identical and long enough to be a
                // Fill segment of their own, so this prefix ends at `start`.
                let start = i - run;
                if prev_pos == start {
                    return DpPrefix { len: start, mask: prev.0, profile: prev.1 };
                }
                return DpPrefix { len: start, mask, profile };
            }
        } else {
            run = 0;
        }

        let j = RSET_INDEX[b as usize];
        if j >= 0 {
            let bit = 1u16 << j;
            if mask & bit != 0 {
                // Already named by the mask; nothing changes.
                i += 1;
                continue;
            }
            // One more donor to spend: every profile whose lowest literal rank
            // has been reached now drops out.
            let viable = lane_ge(min_donor, (k + 1) * LANE_ONES);
            if viable == 0 {
                break;
            }
            prev = (mask, profile);
            prev_pos = i;
            profile = (viable.trailing_zeros() >> 3) as u8;
            mask |= bit;
            k += 1;
        } else {
            let ranks = RANK_PACKED[b as usize];
            if ranks == NOT_REPRESENTABLE {
                break; // not representable under any mask or profile
            }
            let new_min = lane_min(min_donor, ranks);
            if new_min == min_donor {
                // This character ranks below nothing already seen; the set of
                // viable profiles cannot have changed.
                i += 1;
                continue;
            }
            let viable = lane_ge(new_min, k * LANE_ONES);
            if viable == 0 {
                break;
            }
            prev = (mask, profile);
            prev_pos = i;
            profile = (viable.trailing_zeros() >> 3) as u8;
            min_donor = new_min;
        }
        i += 1;
    }

    DpPrefix { len: i, mask, profile }
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

/// The next position at or after `from` where the main loop could take a
/// branch other than block mode, given that it is inside a block-mode run and
/// therefore only ever *visits* positions `from`, `from + 4`, `from + 8`, ...
///
/// Only those positions have to be tested, and at each of them the two tests
/// are exact rather than heuristic: a Fill segment starts there iff
/// [`MIN_FILL_BYTES`] equal bytes do, and a DP segment can only start there if
/// [`MIN_PASSTHROUGH_BYTES`] representable bytes do. Both bail out on their
/// first counterexample, which on high-entropy input is the second byte they
/// read -- so the whole test costs a handful of loads per 4 bytes consumed,
/// where running the two real scans costs an order of magnitude more.
///
/// The caller may jump straight to the returned position: every position it
/// passes over would have taken step 4 and consumed exactly 4 bytes, and block
/// mode over a whole number of groups is the concatenation of the per-group
/// results, so the output is unchanged.
fn next_decision_point(data: &[u8], from: usize) -> usize {
    let n = data.len();
    let mut q = from;
    while q < n {
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
    n
}

/// The 5-character Solid Fill signal for `byte` repeated `len` times
/// (spec section 9).
fn fill_signal(byte: u8, len: usize) -> [u8; 5] {
    debug_assert!((MIN_FILL_BYTES..=MAX_FILL_BYTES).contains(&len));
    let payload = ((byte as u64) << 11) | (len as u64 - 1);
    value_to_5chars_64(FILL_SIGNAL_BASE + payload)
}

/// The 5-character DP signal for a segment (spec section 9).
fn dp_signal(prefix: &DpPrefix) -> [u8; 5] {
    debug_assert!((1..=MAX_DP_SEGMENT_CHARS).contains(&prefix.len));
    let payload = ((prefix.profile as u64) << 24)
        | ((prefix.mask as u64) << 11)
        | (prefix.len as u64 - 1);
    value_to_5chars_64(DP_SIGNAL_BASE + payload)
}

/// Encode `data` as a Base85N string.
pub fn encode(data: &[u8]) -> String {
    // The buffer is sized once and written through a cursor, rather than pushed
    // into: the length is known for every emit, so a per-group capacity test has
    // nothing to decide, and writing by position lets the block-mode loop carry
    // its bounds information into `as_chunks_mut`.
    let mut out: Vec<u8> = vec![0u8; encode_capacity(data.len())];
    let mut w = 0usize;
    let mut pos = 0usize;

    // Start of the pending run of block-mode bytes, or `usize::MAX` for none.
    // Consecutive block-mode iterations are converted in one call instead of
    // four bytes at a time. That does not change the output: block mode
    // consumes exactly one 4-byte group per iteration, and block mode over a
    // whole number of groups is the concatenation of the per-group results.
    let mut block_start = usize::MAX;

    while pos < data.len() {
        // Step 1: a run of identical bytes long enough to be worth a signal of
        // its own. Five characters for up to 2048 bytes.
        let run = fill_run(&data[pos..]);
        if run >= MIN_FILL_BYTES {
            if block_start != usize::MAX {
                w += flush_block(data, block_start, pos, &mut out, w);
                block_start = usize::MAX;
            }
            reserve(&mut out, w, 5);
            out[w..w + 5].copy_from_slice(&fill_signal(data[pos], run));
            w += 5;
            pos += run;
            continue;
        }

        // Steps 2 and 3.
        let prefix = scan_dp(&data[pos..]);
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
            let (pairs, k) = donors(prefix.profile as usize, prefix.mask);
            let mut xlat = IDENTITY_ASCII;
            for &(rset, donor) in &pairs[..k] {
                xlat[rset as usize] = donor;
            }

            let src = &data[pos..pos + prefix.len];
            let dst = &mut out[w..w + prefix.len];
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
        // The gate is what keeps the lookahead off the path it cannot help:
        // where the next byte is representable, a DP candidate starts right
        // here and the scan the loop is about to run is the cheaper way to
        // find out how far it reaches. Where it is not, the lookahead runs
        // over binary, which is exactly where it earns its keep.
        if pos < data.len() && IS_REPRESENTABLE[data[pos] as usize] == 0 {
            let next = next_decision_point(data, pos);
            pos += ((next - pos) / 4) * 4;
        }
    }

    if block_start != usize::MAX {
        w += flush_block(data, block_start, pos, &mut out, w);
    }

    out.truncate(w);
    // Every byte written is an Alphabet-N character, all of which are ASCII.
    debug_assert!(out.is_ascii());
    String::from_utf8(out).expect("encoder emits only Alphabet-N characters")
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

    let (ins, _) = buf[..full_len].as_chunks::<4>();
    let (outs, _) = dst[..groups * 5].as_chunks_mut::<5>();
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
    use crate::alphabet::{NUM_PROFILES, PROFILES, RSET_LEN};

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
