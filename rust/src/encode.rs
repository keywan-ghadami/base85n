// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Encoding algorithm (spec section 6): the single-scan Dynamic Prefix
//! Identification of step 1, and the one-group Block Mode fallback of step 2.b.
//!
//! Step 1 asks the same question of eight replacement alphabets -- how far from
//! here can you represent every byte -- and takes the alphabet that reaches
//! furthest, smallest identifier winning a tie. Asking them one at a time would
//! walk the window eight times; [`scan_alphabets`] instead walks it once,
//! carrying a live set of the alphabets still able to represent everything seen
//! so far. `REPR` gives that set for a byte in one lookup, so the walk is an AND
//! per byte, and an alphabet's run ends exactly where its bit leaves the set.
//!
//! That also settles spec section 6.6 without any caching between iterations.
//! When Dynamic Passthrough is taken the scan costs `best_len` inspections and
//! the loop consumes `best_len` bytes. When it is not, the scan is bounded by
//! how far a candidate got -- under 20 bytes, by definition of the branch --
//! and [`first_dp_capable_run`] then skips the whole stretch in which no
//! alphabet can reach the threshold, so the loop does not re-enter the decision
//! every 4 bytes either.

use crate::alphabet::{ENC_XLAT, NUM_ALPHABETS, REPR};
use crate::constants::{
    DP_SIGNAL_BASE, MAX_DP_ANALYSIS_BYTES, MAX_DP_OUTPUT_CHARS_PER_SIGNAL, MIN_PASSTHROUGH_BYTES,
};
use crate::digits::{value_to_5chars_32, value_to_5chars_64};

/// What the output buffer is sized at up front, excluding growth.
///
/// Block mode emits exactly 1.25 characters per byte (plus at most 2 for a
/// partial final group), so this is the exact size an input with no Dynamic
/// Passthrough in it needs -- which is every high-entropy input, the case where
/// the buffer is large enough for its size to matter. A DP segment never
/// exceeds it either: it spends one character per byte plus a 5-character
/// signal per 1024 bytes, which is below 1.25 characters per byte for every
/// segment long enough to be taken. The reserve below is kept for the short
/// trailing cases and as the single place every write is checked.
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

/// Step 1: how far each alphabet reaches from `pos`, resolved in one walk.
///
/// Returns the length of the longest representable prefix and the identifier of
/// the alphabet achieving it, the smallest identifier winning a tie as spec
/// section 6.1 step 1 requires. The length is capped at
/// [`MAX_DP_ANALYSIS_BYTES`].
fn scan_alphabets(data: &[u8], pos: usize) -> (usize, usize) {
    let limit = (data.len() - pos).min(MAX_DP_ANALYSIS_BYTES);
    let window = &data[pos..pos + limit];

    // Bit `a` stays set while alphabet `a` has represented every byte so far.
    // No per-alphabet bookkeeping is needed: an alphabet that drops out earlier
    // reaches strictly less far than one still in `live`, so when the walk stops
    // -- at the first byte no surviving alphabet can carry, or at the cap --
    // `live` is exactly the set achieving the greatest length, and that length
    // is the position reached.
    let mut live: u8 = ((1u16 << NUM_ALPHABETS) - 1) as u8;
    let mut i = 0usize;
    while i < window.len() {
        let next = live & REPR[window[i] as usize];
        if next == 0 {
            break; // every surviving alphabet ends here
        }
        live = next;
        i += 1;
    }

    // Lowest set bit: the smallest identifier, which is the tie-break spec
    // section 6.1 step 1 requires. `live` is never zero here.
    (i, live.trailing_zeros() as usize)
}

/// The first offset at or after `from` where a Dynamic Passthrough candidate
/// could begin -- the first position starting a run of at least
/// `MIN_PASSTHROUGH_BYTES` bytes that some alphabet can represent -- or
/// `data.len()` if there is none.
///
/// Every position before it takes the block-mode branch and consumes exactly
/// 4 bytes, so the encoder may jump to the last 4-byte boundary at or before
/// it without changing the output (spec section 6.6).
///
/// It can afford to look ahead because any `MIN_PASSTHROUGH_BYTES` consecutive
/// positions contain exactly one multiple of `MIN_PASSTHROUGH_BYTES`, so a run
/// that long cannot avoid a sampling lattice of that stride. Sampling instead of
/// scanning turns the lookahead from one table lookup per byte into one per 20
/// bytes on the input where it matters -- high-entropy data, where nearly every
/// sample lands on a byte no alphabet can represent and is rejected at once.
fn first_dp_capable_run(data: &[u8], from: usize) -> usize {
    let unrepresentable = |b: u8| REPR[b as usize] == 0;
    let mut p = from;
    while p < data.len() {
        if unrepresentable(data[p]) {
            p += MIN_PASSTHROUGH_BYTES;
            continue;
        }

        // Back to this run's start, but never before `from`: positions before it
        // are not the caller's concern.
        let mut start = p;
        while start > from && !unrepresentable(data[start - 1]) {
            start -= 1;
        }

        // Forward only until the threshold is settled either way.
        let mut end = p;
        while end < data.len() && !unrepresentable(data[end]) {
            end += 1;
            if end - start >= MIN_PASSTHROUGH_BYTES {
                return start;
            }
        }

        // Too short. Resume the lattice at this run's end; a later run of the
        // required length still cannot dodge it.
        p = end;
        if p == from {
            p += 1; // defensive: always make progress
        }
    }
    data.len()
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
    // four bytes at a time, and stretches where no alphabet can reach
    // MIN_PASSTHROUGH_BYTES are skipped outright. Neither changes the output:
    // block mode consumes exactly one 4-byte group per iteration, so every
    // position skipped would have taken that branch, and block mode over a
    // whole number of groups is the concatenation of the per-group results.
    let mut block_start = usize::MAX;

    while pos < data.len() {
        let (best_len, best_alphabet) = scan_alphabets(data, pos);

        if best_len >= MIN_PASSTHROUGH_BYTES {
            // Step 2.a. At MIN_PASSTHROUGH_BYTES the two modes cost the same 25
            // characters and Dynamic Passthrough only gains from there, so the
            // length test settles the size comparison too.
            if block_start != usize::MAX {
                w += flush_block(data, block_start, pos, &mut out, w);
                block_start = usize::MAX;
            }

            reserve(&mut out, w, 5 + best_len);
            // The 10-bit length field, biased by one, is what bounds this; the
            // scan's own cap is what keeps it in range.
            debug_assert!(best_len <= MAX_DP_OUTPUT_CHARS_PER_SIGNAL);
            let payload = ((best_alphabet as u64) << 10) | (best_len as u64 - 1);
            out[w..w + 5].copy_from_slice(&value_to_5chars_64(DP_SIGNAL_BASE + payload));
            w += 5;

            let xlat = &ENC_XLAT[best_alphabet];
            let src = &data[pos..pos + best_len];
            let dst = &mut out[w..w + best_len];
            for (o, &b) in dst.iter_mut().zip(src.iter()) {
                *o = xlat[b as usize];
            }
            w += best_len;
            pos += best_len;
            continue;
        }

        // Step 2.b, block-mode fallback: exactly one 4-byte group, however long
        // the failed candidate was. Nothing but the end of the input can hand
        // `process_with_block_mode` a partial group this way.
        if block_start == usize::MAX {
            block_start = pos;
        }
        pos += (data.len() - pos).min(4);

        // Skip the stretch in which no alphabet can reach the DP threshold.
        // Every position passed over would have taken this same branch and
        // consumed 4 bytes, so the output is unchanged.
        let limit = first_dp_capable_run(data, pos);
        pos += ((limit - pos) / 4) * 4;
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
fn flush_block(
    data: &[u8],
    start: usize,
    end: usize,
    out: &mut Vec<u8>,
    w: usize,
) -> usize {
    let n = end - start;
    let need = n / 4 * 5 + if n.is_multiple_of(4) { 0 } else { n % 4 + 1 };
    reserve(out, w, need);
    process_with_block_mode(&data[start..end], &mut out[w..])
}

/// `ProcessWithBlockMode` (spec section 6.2). Per section 6.2's closing
/// paragraph, only a genuinely final remainder is padded; the main loop defers
/// every other one, so a `buf` that is not a multiple of 4 reaches here only at
/// the end of the input.
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
