//! Encoding algorithm (README.md section 6): the two-pass "Pass 1 (window
//! and mask discovery) / Pass 2 (boundary finalization with fixed mask)"
//! Dynamic Prefix Identification procedure, escape-pair-safe DP Output
//! Segmentation (step 1.d), and the deferred-remainder Block Mode
//! fallback (step 2.b).

use crate::alphabet::{
    is_alphabet_n_byte, replacement_index_for_byte, rset_index_for_byte, ESCAPE_CHAR,
    REPLACEMENT_CHARS,
};
use crate::constants::{
    DP_SIGNAL_BASE, MAX_CONSECUTIVE_ESCAPES, MAX_DP_OUTPUT_CHARS_PER_SIGNAL, MIN_PASSTHROUGH_BYTES,
};
use crate::digits::value_to_group;

/// Encode `data` as a Base85N string.
pub fn encode(data: &[u8]) -> String {
    let mut output = String::new();
    let mut pos = 0usize;

    while pos < data.len() {
        let remaining = &data[pos..];

        // Step 1.a: Pass 1 -- Window and Mask Discovery.
        let (window_len, window_mask) = scan_window(remaining);
        let window = &remaining[..window_len];

        // Step 1.b: Pass 2 -- Boundary Finalization with Fixed Mask. This
        // also produces the transformed characters and each consumed
        // byte's per-byte contribution length (1 or 2 chars), which step
        // 1.d needs to segment without ever splitting a 2-char escape
        // pair.
        let Pass2Result { candidate_len, final_mask, transformed, piece_lens } =
            run_pass2(window, window_mask);

        if candidate_len == 0 {
            // dp_candidate_prefix is empty (e.g. the first byte of window
            // was unrepresentable, so window itself was empty). Step
            // 2.b's final "else" branch.
            let n = remaining.len().min(4);
            process_with_block_mode(&remaining[..n], &mut output);
            pos += n;
            continue;
        }

        // Step 1.d: DP Output Segmentation, computed exactly via greedy
        // packing of whole per-byte contributions -- never approximated
        // by ceil(L_transformed / 511).
        let segments = segment_pieces(&transformed, &piece_lens);
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
            pos += candidate_len;
        } else {
            // Step 2.b, Block Mode fallback branch.
            let r = candidate_len % 4;
            if candidate_len >= 4 {
                let full_len = candidate_len - r;
                process_with_block_mode(&remaining[..full_len], &mut output);
                pos += full_len;
                // The trailing `r` (0-3) bytes are deliberately left
                // unpadded at the front of the buffer for the next
                // iteration; do not advance `pos` past them.
            } else {
                let n = remaining.len().min(4);
                process_with_block_mode(&remaining[..n], &mut output);
                pos += n;
            }
        }
    }

    output
}

fn ceil_div(a: usize, b: usize) -> usize {
    a.div_ceil(b)
}

/// Step 1.a: Pass 1 -- Window and Mask Discovery. Scans `buffer`
/// byte-by-byte from the start, bounded only by representability (never
/// by escaping cost or the consecutive-escape limit). Returns the number
/// of bytes in `window` and `window_mask` (the OR of every R-Set bit
/// seen).
fn scan_window(buffer: &[u8]) -> (usize, u16) {
    let mut mask: u16 = 0;
    let mut len = 0usize;

    for &b in buffer {
        if let Some(j) = rset_index_for_byte(b) {
            mask |= 1 << j;
            len += 1;
        } else if is_alphabet_n_byte(b) {
            // Includes '~' and all 13 replacement characters
            // unconditionally, regardless of escaping cost.
            len += 1;
        } else {
            break;
        }
    }

    (len, mask)
}

struct Pass2Result {
    /// Number of original bytes from `window` that became part of
    /// `dp_candidate_prefix`.
    candidate_len: usize,
    /// `window_mask`, fixed for the whole of Pass 2 (a superset of the
    /// R-Set bits actually present in `dp_candidate_prefix`, per README.md
    /// section 6.1.c).
    final_mask: u16,
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
fn run_pass2(window: &[u8], window_mask: u16) -> Pass2Result {
    let final_mask = window_mask;
    let mut candidate_len = 0usize;
    let mut consecutive_escape_trigger_count: u32 = 0;
    let mut transformed = String::with_capacity(window.len() * 2);
    let mut piece_lens = Vec::with_capacity(window.len());

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

    Pass2Result { candidate_len, final_mask, transformed, piece_lens }
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

/// `ProcessWithBlockMode` (README.md section 6.2). Per section 6.2's
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
