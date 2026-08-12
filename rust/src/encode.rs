// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Encoding algorithm (spec section 6): the single-scan Dynamic Prefix
//! Identification of step 1, and the deferred-remainder Block Mode fallback of
//! step 2.b.
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
//! The scan costs `best_len` byte inspections and the loop then consumes
//! `best_len` bytes when Dynamic Passthrough is taken, or at least
//! `best_len - 3` when it is not, so the work per byte of input is bounded by a
//! small constant rather than by the window size.

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
    let mut live: u8 = ((1u16 << NUM_ALPHABETS) - 1) as u8;
    let mut stop = [0usize; NUM_ALPHABETS];

    let mut i = 0usize;
    while i < window.len() {
        let next = live & REPR[window[i] as usize];
        if next != live {
            let mut dropped = live & !next;
            while dropped != 0 {
                let a = dropped.trailing_zeros() as usize;
                stop[a] = i;
                dropped &= dropped - 1;
            }
            live = next;
            if live == 0 {
                break;
            }
        }
        i += 1;
    }

    // Whatever is still live reaches the end of the window.
    let mut rest = live;
    while rest != 0 {
        let a = rest.trailing_zeros() as usize;
        stop[a] = i;
        rest &= rest - 1;
    }

    let mut best_len = 0usize;
    let mut best_alphabet = 0usize;
    for (a, &len) in stop.iter().enumerate() {
        // Strictly greater keeps the smallest identifier on a tie.
        if len > best_len {
            best_len = len;
            best_alphabet = a;
        }
    }
    (best_len, best_alphabet)
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
    // four bytes at a time. This does not change which positions the loop
    // visits: every block-mode consumption is a whole number of 4-byte groups,
    // so the concatenation of the per-iteration results is exactly the
    // block-mode encoding of the accumulated range.
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

        // Step 2.b, block-mode fallback. A candidate of 4 bytes or more gives up
        // only its whole 4-byte groups; the trailing 1-3 bytes stay unpadded for
        // the next iteration, since padding a non-final remainder would be
        // indistinguishable from the start of the next group to a decoder.
        let consumed = if best_len >= 4 {
            best_len - best_len % 4
        } else {
            // Fewer than 4 representable bytes under every alphabet. This is the
            // branch that ignores representability entirely.
            (data.len() - pos).min(4)
        };
        if block_start == usize::MAX {
            block_start = pos;
        }
        pos += consumed;
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
