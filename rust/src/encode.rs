// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Encoding algorithm (spec section 6): the two-pass "Pass 1 (window
//! and mask discovery) / Pass 2 (boundary finalization with fixed mask)"
//! Dynamic Prefix Identification procedure, escape-pair-safe DP Output
//! Segmentation (step 1.d), and the deferred-remainder Block Mode
//! fallback (step 2.b).

use crate::alphabet::{
    CLS_INDEX_SHIFT, CLS_RSET_BITS, CLS_UNREPRESENTABLE, DP_ESCAPE_ALWAYS, DP_XLAT, ENC_CLASS,
    RSET_ASCII,
};
use crate::constants::{
    DP_SIGNAL_BASE, MAX_CONSECUTIVE_ESCAPES, MAX_DP_OUTPUT_CHARS_PER_SIGNAL, MIN_PASSTHROUGH_BYTES,
};
use crate::digits::{value_to_5chars_32, value_to_5chars_64};

/// Longest representable run whose Pass 2 scratch stays inline. Two characters
/// per byte for the transformed text, plus one segment offset per 255 bytes and
/// a spare; only a longer run reaches the heap.
const SCRATCH_INLINE_WINDOW: usize = 512;

/// What the output buffer is sized at up front, excluding growth.
///
/// Block mode emits exactly 1.25 characters per byte (plus at most 2 for a
/// partial final group), so this is the exact size an input with no Dynamic
/// Passthrough in it needs -- which is every high-entropy input, the case where
/// the buffer is large enough for its size to matter. A DP segment can exceed
/// that budget, by at most 0.1875 characters per byte, so the main loop checks
/// the room it needs before each emit and grows on the rare occasion it has to.
fn encode_capacity(n: usize) -> usize {
    n + n / 4 + 16
}

/// The encoder's only capacity test: once per emit rather than once per
/// character, and taken only when Dynamic Passthrough has spent more than block
/// mode's 1.25 characters per byte.
#[inline]
fn reserve(out: &mut Vec<u8>, w: usize, need: usize) {
    if need > out.len() - w {
        // A quarter of headroom, so repeated growth stays amortised without
        // overshooting far past what the input needs.
        let want = w + need;
        out.resize(want + want / 4, 0);
    }
}

/// Encode `data` as a Base85N string.
pub fn encode(data: &[u8]) -> String {
    // The buffer is sized once and written through a cursor, rather than pushed
    // into: the length is known for every emit, so a per-group capacity test has
    // nothing to decide, and writing by position lets the block-mode loop carry
    // its bounds information into `chunks_exact_mut`.
    let mut out: Vec<u8> = vec![0u8; encode_capacity(data.len())];
    let mut w = 0usize;
    let mut pos = 0usize;

    // Pass 2's scratch, reused across iterations and sized by the largest window
    // seen so far. It starts inline, because the payloads this format exists for
    // are short -- a UUID, a header line, a log record -- and anything whose
    // representable runs stay within SCRATCH_INLINE_WINDOW bytes never allocates
    // scratch at all. High-entropy input of any size is covered too: its
    // representable runs are a couple of bytes each.
    let mut scratch = Pass2Scratch::new();

    // State of the representable run currently being consumed; `end == 0` with
    // `pos == 0` forces the first scan.
    let mut run = RunState::empty();

    while pos < data.len() {
        // Step 1.a: Pass 1 -- Window and Mask Discovery, once per run. The
        // block-mode fallback below ignores representability and can step past
        // `run.end`, landing in a later run which is then scanned here; runs
        // handled this way are disjoint, so the total scanning work stays linear
        // in `data.len()`.
        if pos >= run.end {
            run.scan(data, pos);
        }
        let window_len = run.end - pos;

        // Skip the mode decision where it cannot change the answer. A DP
        // candidate is never longer than the representable run it starts in, so
        // until the next run that reaches MIN_PASSTHROUGH_BYTES the block-mode
        // branch is certain -- and block mode over whole 4-byte groups is the
        // concatenation of the per-group results, so that entire stretch is
        // encoded in one call. Only worth trying when the current window is
        // itself too short for DP; inside a long representable run the scan
        // would return immediately and cost a rescan for nothing.
        if window_len < MIN_PASSTHROUGH_BYTES {
            let limit = first_dp_capable_run(data, pos);
            let batch = ((limit - pos) / 4) * 4;
            if batch >= 4 {
                // Block mode's 1.25 characters per byte is what the buffer was
                // sized for, so this never grows -- the check is here so that
                // every write goes through the same one.
                reserve(&mut out, w, batch / 4 * 5);
                w += process_with_block_mode(&data[pos..pos + batch], &mut out[w..]);
                pos += batch;
                // The batch may end mid-run, so the cached run state no longer
                // describes the new position.
                run.end = 0;
                continue;
            }
        }

        let final_mask = run.mask_from(data, pos);

        // Step 1.b: Pass 2 -- Boundary Finalization with Fixed Mask, with step
        // 1.d's DP Output Segmentation folded into the same walk.
        let dp = scratch.pass2(&data[pos..run.end], final_mask);

        // Step 2.a: DP Suitability Check.
        let use_dp_mode = dp.candidate_len >= MIN_PASSTHROUGH_BYTES && {
            let conceptual_dp_output_length = dp.segments * 5 + dp.chars;
            let block_mode_output_length = dp.candidate_len.div_ceil(4) * 5;
            conceptual_dp_output_length <= block_mode_output_length
        };

        // How much room this iteration needs, and what it consumes.
        let (consumed, need) = if use_dp_mode {
            (dp.candidate_len, dp.segments * 5 + dp.chars)
        } else {
            let n = if dp.candidate_len >= 4 {
                // Step 2.b, Block Mode fallback: block-encode only the exact
                // multiple-of-4 leading portion now. The trailing 0-3 bytes are
                // deliberately left unpadded for the next iteration.
                dp.candidate_len - dp.candidate_len % 4
            } else {
                // Fewer than 4 candidate bytes (e.g. the byte at `pos` is
                // unrepresentable, so the window was empty). This is the branch
                // that can consume past `run.end`.
                (data.len() - pos).min(4)
            };
            (n, n / 4 * 5 + if n % 4 > 0 { n % 4 + 1 } else { 0 })
        };

        reserve(&mut out, w, need);

        if use_dp_mode {
            // Step 2.b, DP branch.
            let mut start = 0usize;
            for k in 0..dp.segments {
                let end = scratch.seg_ends[k];
                let payload = ((final_mask as u64) << 9) | (end - start) as u64;
                out[w..w + 5].copy_from_slice(&value_to_5chars_64(DP_SIGNAL_BASE + payload));
                w += 5;
                out[w..w + (end - start)].copy_from_slice(&scratch.xf[start..end]);
                w += end - start;
                start = end;
            }
        } else {
            w += process_with_block_mode(&data[pos..pos + consumed], &mut out[w..]);
        }

        pos += consumed;
    }

    out.truncate(w);
    // Every byte written is an Alphabet-N character, all of which are ASCII.
    debug_assert!(out.is_ascii());
    String::from_utf8(out).expect("encoder emits only Alphabet-N characters")
}

/// Step 1.a: Pass 1 -- Window and Mask Discovery, scanned once per representable
/// run rather than once per iteration of the main loop.
///
/// The run is bounded *only* by representability (an R-Set character, or any
/// Alphabet-N character, which includes the escape character and all replacement
/// characters unconditionally, regardless of escaping cost). Rescanning it for a
/// position deeper inside the same run -- the literal reading of spec section
/// 6.1 -- is redundant and makes encoding quadratic; see spec section 6.6.
///
/// The mask for a suffix is answered by a comparison, not a recount: `R_Char[j]`
/// occurs in `[off, end)` exactly when its *last* occurrence in the run is at or
/// after `off`. `last[j]` holds that position, found by a backward walk that
/// stops as soon as every character the forward scan saw has been placed -- for
/// the common case, a run whose characters recur near its end, a walk of a few
/// bytes. It is done lazily, on the first suffix asked for, because most runs
/// are never asked: a run consumed whole by one DP segment has no suffix, and a
/// run with no R-Set character in it has nothing to narrow.
struct RunState {
    /// Last offset of `R_Char[j]` within the run.
    last: [usize; RSET_ASCII.len()],
    /// Offset the forward scan started from.
    start: usize,
    /// Offset into `data`, exclusive, where the run ends.
    end: usize,
    /// `suffix_mask` is the answer for every `off <= hold`.
    hold: usize,
    /// Bit j set iff `R_Char[j]` occurs in `[start, end)`.
    mask: u16,
    /// The last answer given.
    suffix_mask: u16,
    /// `last` has been filled in.
    located: bool,
}

impl RunState {
    fn empty() -> Self {
        RunState {
            last: [0; RSET_ASCII.len()],
            start: 0,
            end: 0,
            hold: 0,
            mask: 0,
            suffix_mask: 0,
            located: false,
        }
    }

    fn scan(&mut self, data: &[u8], pos: usize) {
        let mut acc = 0u32;
        let mut i = pos;

        // Eight bytes at a time. A block is entirely representable iff the OR of
        // its eight class words has no CLS_UNREPRESENTABLE bit, and that same OR
        // carries every R-Set bit the block contributes to the mask -- so the
        // common case, a long representable run, costs one load and one OR per
        // byte with no per-byte branch.
        while data.len() - i >= 8 {
            let c = &data[i..i + 8];
            let a = ENC_CLASS[c[0] as usize]
                | ENC_CLASS[c[1] as usize]
                | ENC_CLASS[c[2] as usize]
                | ENC_CLASS[c[3] as usize];
            let b = ENC_CLASS[c[4] as usize]
                | ENC_CLASS[c[5] as usize]
                | ENC_CLASS[c[6] as usize]
                | ENC_CLASS[c[7] as usize];
            let both = a | b;
            if both & CLS_UNREPRESENTABLE != 0 {
                break;
            }
            acc |= both;
            i += 8;
        }
        while i < data.len() {
            let c = ENC_CLASS[data[i] as usize];
            if c & CLS_UNREPRESENTABLE != 0 {
                break;
            }
            acc |= c;
            i += 1;
        }

        // acc's index field is meaningless once entries have been OR-ed
        // together; only the R-Set bits are read out.
        self.mask = (acc & CLS_RSET_BITS) as u16;
        self.located = false;
        self.start = pos;
        self.end = i;
    }

    /// The window mask for the suffix of the run that starts at `off`, which
    /// lies in `[self.start, self.end)`.
    fn mask_from(&mut self, data: &[u8], off: usize) -> u16 {
        if self.mask == 0 || off == self.start {
            return self.mask;
        }

        if !self.located {
            // Walking backward, the first occurrence met of a character is its
            // last occurrence, so one bit of `pending` is retired per hit and the
            // walk ends with the earliest of the last occurrences.
            let mut pending = self.mask;
            let mut i = self.end;
            while i > self.start && pending != 0 {
                i -= 1;
                let cls = ENC_CLASS[data[i] as usize];
                let bit = (cls & CLS_RSET_BITS) as u16;
                if bit & pending != 0 {
                    self.last[((cls >> CLS_INDEX_SHIFT) & 0xF) as usize] = i;
                    pending &= !bit;
                }
            }
            self.located = true;
            self.hold = 0; // no answer cached yet; off > 0 forces the rebuild
        } else if off <= self.hold {
            return self.suffix_mask;
        }

        // The answer cannot change until `off` passes the earliest of the last
        // occurrences still in it, so record that position and rebuild only when
        // it is reached -- at most RSET_ASCII.len() times over a whole run,
        // rather than once per iteration of the encoder's main loop.
        let mut m = 0u16;
        let mut hold = usize::MAX;
        for j in 0..RSET_ASCII.len() {
            if (self.mask >> j) & 1 != 0 && self.last[j] >= off {
                m |= 1 << j;
                if self.last[j] < hold {
                    hold = self.last[j];
                }
            }
        }
        self.suffix_mask = m;
        self.hold = hold;
        m
    }
}

/// What Pass 2 found.
struct DpScan {
    /// Bytes of the window that form `dp_candidate_prefix`.
    candidate_len: usize,
    /// Transformed characters written.
    chars: usize,
    /// Segments the greedy packing splits them into.
    segments: usize,
}

/// Pass 2's scratch, owned by `encode` and reused across iterations. Sizing it
/// from `window.len()` on every call would allocate a window-sized buffer to
/// hold as few as three characters, since Pass 2 can bail out long before Pass
/// 1's window ends.
struct Pass2Scratch {
    /// The transformed DP character representation of `dp_candidate_prefix`.
    xf: Vec<u8>,
    /// End offset of each segment within `xf`.
    seg_ends: Vec<usize>,
}

impl Pass2Scratch {
    fn new() -> Self {
        Pass2Scratch {
            xf: Vec::with_capacity(SCRATCH_INLINE_WINDOW * 2),
            seg_ends: Vec::with_capacity(SCRATCH_INLINE_WINDOW / 255 + 2),
        }
    }

    /// Step 1.b: Pass 2 -- Boundary Finalization with Fixed Mask. Re-scans
    /// `window` byte-by-byte against the *fixed* `final_mask` (== `window_mask`
    /// from Pass 1, never modified here), applying Case i/ii/iii and the
    /// `MAX_CONSECUTIVE_ESCAPES` early-termination rule to determine how many
    /// leading bytes of `window` form `dp_candidate_prefix`, and writes the
    /// transformed text to `self.xf`.
    ///
    /// Step 1.d's segmentation is greedy over the same byte sequence -- close
    /// the current segment *before* adding a piece that would push it past
    /// MAX_DP_OUTPUT_CHARS_PER_SIGNAL, so a boundary never falls inside a Case
    /// ii escape pair -- which makes it a prefix computation like everything
    /// else here, and lets it run in this loop instead of two more passes over
    /// the window.
    fn pass2(&mut self, window: &[u8], final_mask: u16) -> DpScan {
        self.xf.clear();
        self.seg_ends.clear();

        // A byte is escaped iff its trigger field meets this: its own bit for a
        // replacement character whose R-Set partner is in the window, or
        // DP_ESCAPE_ALWAYS for '~'.
        let trigger = final_mask as u32 | DP_ESCAPE_ALWAYS;
        let mut consecutive_escape_trigger_count: u32 = 0;
        let mut seg_len = 0usize;
        let mut i = 0usize;
        let mut byte_at_a_time_until = 0usize;

        while i < window.len() {
            // Eight bytes at a time while none of them needs escaping. Escapes
            // are what make Pass 2 branch: without one, every byte contributes
            // exactly its translated character and exactly one to the segment
            // length, so eight of them are one OR-reduction, eight pushes and a
            // single segment-length test. A group that does need escaping is
            // handed to the byte path in full, rather than one byte at a time
            // with a fresh eight-byte probe in between.
            if i >= byte_at_a_time_until
                && window.len() - i >= 8
                && seg_len + 8 <= MAX_DP_OUTPUT_CHARS_PER_SIGNAL
            {
                let p = &window[i..i + 8];
                let t: [u32; 8] = [
                    DP_XLAT[p[0] as usize],
                    DP_XLAT[p[1] as usize],
                    DP_XLAT[p[2] as usize],
                    DP_XLAT[p[3] as usize],
                    DP_XLAT[p[4] as usize],
                    DP_XLAT[p[5] as usize],
                    DP_XLAT[p[6] as usize],
                    DP_XLAT[p[7] as usize],
                ];
                let any = t[0] | t[1] | t[2] | t[3] | t[4] | t[5] | t[6] | t[7];

                if (any >> 16) & trigger == 0 {
                    self.xf.extend_from_slice(&[
                        t[0] as u8, t[1] as u8, t[2] as u8, t[3] as u8, t[4] as u8, t[5] as u8,
                        t[6] as u8, t[7] as u8,
                    ]);
                    i += 8;
                    seg_len += 8;
                    consecutive_escape_trigger_count = 0;
                    continue;
                }
                byte_at_a_time_until = i + 8;
            }

            let b = window[i];
            let t = DP_XLAT[b as usize];
            i += 1;

            if (t >> 16) & trigger != 0 {
                // Case ii: requires escaping, against the fixed final_mask.
                consecutive_escape_trigger_count += 1;
                if consecutive_escape_trigger_count > MAX_CONSECUTIVE_ESCAPES {
                    i -= 1; // b is excluded along with the rest of the window
                    break;
                }
                if seg_len + 2 > MAX_DP_OUTPUT_CHARS_PER_SIGNAL {
                    self.seg_ends.push(self.xf.len());
                    seg_len = 0;
                }
                self.xf.push(b'~');
                self.xf.push(b);
                seg_len += 2;
                continue;
            }

            // Case i (R-Set character, substituted) or Case iii (plain literal);
            // DP_XLAT holds the right character for both.
            consecutive_escape_trigger_count = 0;
            if seg_len + 1 > MAX_DP_OUTPUT_CHARS_PER_SIGNAL {
                self.seg_ends.push(self.xf.len());
                seg_len = 0;
            }
            self.xf.push(t as u8);
            seg_len += 1;
        }

        if seg_len > 0 {
            self.seg_ends.push(self.xf.len());
        }

        DpScan {
            candidate_len: i,
            chars: self.xf.len(),
            segments: self.seg_ends.len(),
        }
    }
}

/// The offset of the first position at or after `from` where a Dynamic
/// Passthrough candidate could begin -- the first position whose representable
/// run reaches `MIN_PASSTHROUGH_BYTES` -- or `data.len()` if there is none.
///
/// It can afford to look ahead because any `MIN_PASSTHROUGH_BYTES` consecutive
/// positions contain exactly one multiple of `MIN_PASSTHROUGH_BYTES`, so a run
/// that long cannot avoid a sampling lattice of that stride. Sampling instead of
/// scanning turns the lookahead from one table lookup per byte into one per 20
/// bytes on the input where it matters -- high-entropy data, where nearly every
/// sample lands on an unrepresentable byte and is rejected immediately.
fn first_dp_capable_run(data: &[u8], from: usize) -> usize {
    let unrepresentable = |b: u8| ENC_CLASS[b as usize] & CLS_UNREPRESENTABLE != 0;
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

/// `ProcessWithBlockMode` (spec section 6.2). Per section 6.2's added paragraph,
/// every call site in this module other than the genuinely-final-remainder ones
/// passes a `buf` whose length is an exact multiple of 4, so no padding occurs
/// there.
/// Writes at most `(buf.len()/4)*5 + 4` characters at the front of `dst`, and
/// returns how many. Zipped exact chunks carry both lengths into the loop, so
/// neither side is bounds-checked per group.
fn process_with_block_mode(buf: &[u8], dst: &mut [u8]) -> usize {
    let full_len = buf.len() - buf.len() % 4;
    let groups = full_len / 4;

    for (src, chars) in buf[..full_len]
        .chunks_exact(4)
        .zip(dst[..groups * 5].chunks_exact_mut(5))
    {
        let value = u32::from_be_bytes(src.try_into().expect("chunks_exact(4)"));
        chars.copy_from_slice(&value_to_5chars_32(value));
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
