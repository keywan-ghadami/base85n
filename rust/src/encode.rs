// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Encoding algorithm (spec section 6): the two-pass "Pass 1 (window
//! and mask discovery) / Pass 2 (boundary finalization with fixed mask)"
//! Dynamic Prefix Identification procedure, escape-pair-safe DP Output
//! Segmentation (step 1.d), and the deferred-remainder Block Mode
//! fallback (step 2.b).

use crate::alphabet::{
    is_alphabet_n_byte, replacement_index_for_byte, rset_index_for_byte, ESCAPE_CHAR,
    REPLACEMENT_CHARS, RSET_ASCII,
};
use crate::constants::{
    DP_SIGNAL_BASE, MAX_CONSECUTIVE_ESCAPES, MAX_DP_OUTPUT_CHARS_PER_SIGNAL, MIN_PASSTHROUGH_BYTES,
};
use crate::digits::value_to_group;

/// Encode `data` as a Base85N string.
pub fn encode(data: &[u8]) -> String {
    let mut output = String::new();
    let mut pos = 0usize;

    // State of the representable run currently being consumed; `end == 0`
    // with `pos == 0` forces the first scan.
    let mut run = RunState { counts: [0; RSET_ASCII.len()], mask: 0, end: 0 };
    // Reused across iterations; see Pass2Buffers.
    let mut buffers = Pass2Buffers::default();

    while pos < data.len() {
        let remaining = &data[pos..];

        // Step 1.a: Pass 1 -- Window and Mask Discovery, once per run.
        // The block-mode fallback below ignores representability and can
        // step past `run.end`, landing in a later run which is then scanned
        // here; runs handled this way are disjoint, so the total scanning
        // work stays linear in `data.len()`.
        if pos >= run.end {
            run = scan_run(data, pos);
        }
        let window_len = run.end - pos;
        let window_mask = run.mask;
        let window = &remaining[..window_len];

        // Step 1.b: Pass 2 -- Boundary Finalization with Fixed Mask. This
        // also produces the transformed characters and each consumed
        // byte's per-byte contribution length (1 or 2 chars), which step
        // 1.d needs to segment without ever splitting a 2-char escape
        // pair.
        let candidate_len = run_pass2(window, window_mask, &mut buffers);
        let final_mask = window_mask;
        let transformed = &buffers.transformed;
        let piece_lens = &buffers.piece_lens;

        let consumed = if candidate_len == 0 {
            // dp_candidate_prefix is empty (e.g. the first byte of window
            // was unrepresentable, so window itself was empty). Step
            // 2.b's final "else" branch.
            let n = remaining.len().min(4);
            process_with_block_mode(&remaining[..n], &mut output);
            n
        } else {
            // Step 1.d: DP Output Segmentation, computed exactly via greedy
            // packing of whole per-byte contributions -- never approximated
            // by ceil(L_transformed / 511).
            let segments = segment_pieces(transformed, piece_lens);
            let num_segments = segments.len();
            let l_transformed = transformed.chars().count();

            // Step 2.a: DP Suitability Check.
            let conceptual_dp_output_length = num_segments * 5 + l_transformed;
            let block_mode_output_length = ceil_div(candidate_len, 4) * 5;
            let use_dp_mode = candidate_len >= MIN_PASSTHROUGH_BYTES
                && conceptual_dp_output_length <= block_mode_output_length;

            if use_dp_mode {
                // Step 2.b, DP branch.
                for seg in &segments {
                    let seg_len = seg.chars().count();
                    let payload = ((final_mask as u64) << 9) | (seg_len as u64);
                    output.push_str(&value_to_group(DP_SIGNAL_BASE + payload));
                    output.push_str(seg);
                }
                candidate_len
            } else {
                // Step 2.b, Block Mode fallback branch.
                let r = candidate_len % 4;
                if candidate_len >= 4 {
                    let full_len = candidate_len - r;
                    process_with_block_mode(&remaining[..full_len], &mut output);
                    // The trailing `r` (0-3) bytes are deliberately left
                    // unpadded at the front of the buffer for the next
                    // iteration; do not advance `pos` past them.
                    full_len
                } else {
                    let n = remaining.len().min(4);
                    process_with_block_mode(&remaining[..n], &mut output);
                    n
                }
            }
        };

        if pos + consumed < run.end {
            // Still inside the same run: retire the consumed bytes so the
            // next iteration's mask covers exactly the remainder.
            run.consume(data, pos, pos + consumed);
        }
        pos += consumed;
    }

    output
}

fn ceil_div(a: usize, b: usize) -> usize {
    a.div_ceil(b)
}

/// Step 1.a: Pass 1 -- Window and Mask Discovery, scanned once per
/// representable run rather than once per iteration of the main loop.
///
/// `counts[j]` is how often `R_CHAR[j]` still occurs in the unconsumed part
/// of the run, and `end` is where the run stops (bounded only by
/// representability, never by escaping cost or the consecutive-escape
/// limit). Pass 1's window for a position deeper inside the same run is a
/// suffix of this one, so its mask follows from the counts in constant
/// time; rescanning it -- the literal reading of spec section 6.1 -- is
/// redundant and makes encoding quadratic. See spec section 6.6.
struct RunState {
    counts: [usize; RSET_ASCII.len()],
    /// Bit `j` set iff `counts[j] != 0`; kept in step with `counts` so the
    /// encoding loop reads it instead of recomputing it.
    mask: u16,
    end: usize,
}

impl RunState {
    /// Retire `data[from..to]` from the counts, clearing a mask bit as soon
    /// as its last occurrence is consumed.
    fn consume(&mut self, data: &[u8], from: usize, to: usize) {
        for &b in &data[from..to] {
            if let Some(j) = rset_index_for_byte(b) {
                let c = &mut self.counts[j as usize];
                *c -= 1;
                if *c == 0 {
                    self.mask &= !(1 << j);
                }
            }
        }
    }
}

fn scan_run(data: &[u8], pos: usize) -> RunState {
    let mut counts = [0usize; RSET_ASCII.len()];
    let mut mask: u16 = 0;
    let mut end = pos;

    for &b in &data[pos..] {
        if let Some(j) = rset_index_for_byte(b) {
            counts[j as usize] += 1;
            mask |= 1 << j;
            end += 1;
        } else if is_alphabet_n_byte(b) {
            // Includes '~' and all 13 replacement characters
            // unconditionally, regardless of escaping cost.
            end += 1;
        } else {
            break;
        }
    }

    RunState { counts, mask, end }
}

/// Scratch buffers for Pass 2, owned by `encode` and reused across
/// iterations. Sizing them from `window.len()` on every call would allocate
/// a window-sized buffer to hold as few as three characters, since Pass 2
/// can bail out long before Pass 1's window ends.
#[derive(Default)]
struct Pass2Buffers {
    /// The transformed DP character representation of `dp_candidate_prefix`.
    transformed: String,
    /// Each consumed byte's contribution length to `transformed`, in
    /// order: 1 for Case i/iii, 2 for Case ii (an inseparable `~x` pair).
    piece_lens: Vec<u8>,
}

/// Step 1.b: Pass 2 -- Boundary Finalization with Fixed Mask. Re-scans
/// `window` byte-by-byte using `final_mask = window_mask` (which is never
/// modified during this pass), applying Case i/ii/iii and the
/// `MAX_CONSECUTIVE_ESCAPES` early-termination rule. Since the mask is
/// fixed throughout, each byte's transformed representation is computed
/// directly here (no separate transform pass is needed).
fn run_pass2(window: &[u8], final_mask: u16, buffers: &mut Pass2Buffers) -> usize {
    let mut candidate_len = 0usize;
    let mut consecutive_escape_trigger_count: u32 = 0;
    let transformed = &mut buffers.transformed;
    let piece_lens = &mut buffers.piece_lens;
    transformed.clear();
    piece_lens.clear();

    for &b in window {
        if let Some(j) = rset_index_for_byte(b) {
            // Case i: R-Set character.
            transformed.push(REPLACEMENT_CHARS[j as usize] as char);
            piece_lens.push(1);
            candidate_len += 1;
            consecutive_escape_trigger_count = 0;
            continue;
        }

        let requires_escaping = b == ESCAPE_CHAR
            || replacement_index_for_byte(b)
                .map(|j| final_mask & (1 << j) != 0)
                .unwrap_or(false);

        if requires_escaping {
            // Case ii: requires escaping (against the fixed final_mask).
            consecutive_escape_trigger_count += 1;
            if consecutive_escape_trigger_count > MAX_CONSECUTIVE_ESCAPES {
                // B and all remaining bytes of window are excluded.
                break;
            }
            transformed.push('~');
            transformed.push(b as char);
            piece_lens.push(2);
            candidate_len += 1;
            continue;
        }

        // Case iii: plain literal. `window` guarantees every byte here is
        // representable (Alphabet-N or R-Set); having failed Case i/ii,
        // it must be a plain Alphabet-N literal.
        transformed.push(b as char);
        piece_lens.push(1);
        candidate_len += 1;
        consecutive_escape_trigger_count = 0;
    }

    candidate_len
}

/// Step 1.d: DP Output Segmentation. Greedily packs whole per-byte
/// contributions (from `piece_lens`, in the same left-to-right order as
/// Pass 2) into segments of at most `MAX_DP_OUTPUT_CHARS_PER_SIGNAL`
/// characters, closing the current segment *before* adding a
/// contribution that would push it over the limit. This guarantees a
/// segment boundary never falls inside a Case ii 2-character escape
/// pair.
fn segment_pieces(transformed: &str, piece_lens: &[u8]) -> Vec<String> {
    let chars: Vec<char> = transformed.chars().collect();
    let mut segments = Vec::new();
    let mut seg_start = 0usize;
    let mut seg_len = 0usize;
    let mut char_idx = 0usize;

    for &piece in piece_lens {
        let piece = piece as usize;
        if seg_len + piece > MAX_DP_OUTPUT_CHARS_PER_SIGNAL {
            segments.push(chars[seg_start..char_idx].iter().collect());
            seg_start = char_idx;
            seg_len = 0;
        }
        seg_len += piece;
        char_idx += piece;
    }

    if seg_len > 0 {
        segments.push(chars[seg_start..char_idx].iter().collect());
    }

    segments
}

/// `ProcessWithBlockMode` (spec section 6.2). Per section 6.2's
/// added paragraph, every call site in this module other than the two
/// genuinely-final-remainder branches passes a `buf` whose length is an
/// exact multiple of 4, so no padding occurs there.
fn process_with_block_mode(buf: &[u8], output: &mut String) {
    let full_len = (buf.len() / 4) * 4;
    let mut i = 0usize;
    while i < full_len {
        let chunk = [buf[i], buf[i + 1], buf[i + 2], buf[i + 3]];
        let value = u32::from_be_bytes(chunk) as u64;
        output.push_str(&value_to_group(value));
        i += 4;
    }

    let rem = buf.len() - full_len;
    if rem > 0 {
        let mut padded = [0u8; 4];
        padded[..rem].copy_from_slice(&buf[full_len..]);
        let value = u32::from_be_bytes(padded) as u64;
        let group = value_to_group(value);
        output.push_str(&group[..rem + 1]);
    }
}
