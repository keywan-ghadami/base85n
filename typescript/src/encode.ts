/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * Base85N encoder. Implements spec Section 6, including the Section 6.1
 * two-pass ("Pass 1" window/mask discovery, "Pass 2" boundary finalization)
 * Dynamic Passthrough procedure, escape-pair-safe DP segment splitting
 * (step 1.d), and the deferred-remainder Block Mode fallback (step 2.b).
 */
import {
  ALLOWED_PASSTHROUGH_SAFE_REPLACEMENT_CHARS,
  BLOCK_VALUE_LIMIT,
  ESCAPE_CHAR_CODE,
  LENGTH_FIELD_DIVISOR,
  MAX_CONSECUTIVE_ESCAPES,
  IS_ALPHABET_N_BYTE,
  IS_REPRESENTABLE_BYTE,
  MAX_DP_OUTPUT_CHARS_PER_SIGNAL,
  MIN_PASSTHROUGH_BYTES,
  R_SET_ASCII,
  replacementIndexForByte,
  rSetIndexForAscii,
} from "./constants.js";
import { valueToBase85Chars } from "./digits.js";

/**
 * State of one maximal representable run, carrying spec Section 6.1 step 1.a
 * (Pass 1 -- Window and Mask Discovery) for the whole run so that the run is
 * scanned once instead of once per iteration of the encoding loop.
 */
interface RunState {
  /** Occurrences of each R-Set character in the unconsumed part of the run. */
  counts: Int32Array;
  /** Bit j set iff counts[j] !== 0; kept in step with counts. */
  mask: number;
  /** Offset just past the run's last byte. */
  end: number;
}

/**
 * spec Section 6.1, step 1.a (Pass 1 -- Window and Mask Discovery): scans data
 * starting at `start`, bounded *only* by representability (an R-Set character, or any
 * Alphabet-N character -- which includes the escape character and all replacement
 * characters unconditionally, regardless of escaping cost). Never terminates early due
 * to escaping cost or the consecutive-escape limit; only an actually-unrepresentable
 * byte ends the run.
 *
 * Pass 1's window for a position deeper inside the same run is a suffix of this one, so
 * its mask follows from the counts in constant time. Rescanning it -- the literal
 * reading of Section 6.1 -- is redundant and makes encoding quadratic; see spec
 * Section 6.6.
 */
function scanRun(data: Uint8Array, start: number): RunState {
  const counts = new Int32Array(R_SET_ASCII.length);
  let mask = 0;
  let i = start;

  while (i < data.length) {
    const b = data[i] as number;

    const rIdx = rSetIndexForAscii(b);
    if (rIdx !== -1) {
      counts[rIdx] = (counts[rIdx] as number) + 1;
      mask |= 1 << rIdx;
      i++;
      continue;
    }

    if (IS_ALPHABET_N_BYTE[b]) {
      i++;
      continue;
    }

    break; // unrepresentable byte: the run ends here
  }

  return { counts, mask, end: i };
}

/**
 * Retire data[from, to) from a run's counts, clearing a mask bit as soon as its last
 * occurrence is consumed.
 */
function runConsume(data: Uint8Array, from: number, to: number, run: RunState): void {
  for (let i = from; i < to; i++) {
    const rIdx = rSetIndexForAscii(data[i] as number);
    if (rIdx !== -1) {
      const remaining = (run.counts[rIdx] as number) - 1;
      run.counts[rIdx] = remaining;
      if (remaining === 0) {
        run.mask &= ~(1 << rIdx);
      }
    }
  }
}

/** Result of the Section 6.1 step 1.b prefix scan (Pass 2). */
interface CandidateResult {
  /** Number of bytes (a prefix of the window) included in the DP candidate prefix. */
  candidateLen: number;
  /** Total transformed characters across all segments (Section 6.1's L_transformed). */
  transformedLen: number;
  /** The transformed characters, already split per step 1.d (see `pass2Candidate`). */
  segments: string[];
}

/**
 * spec Section 6.1, step 1.b (Pass 2 -- Boundary Finalization with Fixed Mask):
 * re-walks window (data[start, start+windowLen)) using the single, fixed finalMask
 * (== windowMask from Pass 1, never modified here) to apply Case i/ii/iii and the
 * consecutive-escape limit, producing the actual candidate prefix length and its
 * per-source-byte transformed pieces.
 *
 * Step 1.d's DP Output Segmentation rides along in the same pass. A candidate prefix's
 * transformed output may have to be split across several DP signal segments (Section
 * 6.4's MAX_DP_OUTPUT_CHARS_PER_SIGNAL = 511), and the split must never fall inside a
 * two-character escape pair ("~~" or "~x") -- doing so would make the second segment
 * start with an orphaned literal and leave the first ending in a lone '~', which the
 * decoder (Section 7.1.e) cannot parse, since it decodes each segment as a
 * self-contained unit and would report a dangling escape character. Closing the current
 * segment *before* adding a piece that would push it over the limit guarantees that,
 * and makes segmentation a prefix computation -- so it belongs in this loop rather than
 * in a second walk over a per-source-byte array of pieces.
 */
function pass2Candidate(data: Uint8Array, start: number, windowLen: number, finalMask: number): CandidateResult {
  const segments: string[] = [];
  let current = "";
  let transformedLen = 0;
  let consecutiveEscapeTriggerCount = 0;
  let candidateLen = 0;

  for (let i = start; i < start + windowLen; i++) {
    const b = data[i] as number;
    let piece: string;

    const rIdx = rSetIndexForAscii(b);
    if (rIdx !== -1) {
      // Case i: R-Set character. finalMask is guaranteed to have this bit set (Pass 1
      // always sets it for any R-Set byte included in window, and bits never clear).
      piece = ALLOWED_PASSTHROUGH_SAFE_REPLACEMENT_CHARS[rIdx] as string;
      consecutiveEscapeTriggerCount = 0;
    } else {
      const replIdx = replacementIndexForByte(b);
      const needsEscaping =
        b === ESCAPE_CHAR_CODE || (replIdx !== -1 && (finalMask & (1 << replIdx)) !== 0);
      const ch = String.fromCharCode(b);

      if (needsEscaping) {
        // Case ii: requires escaping, against the fixed finalMask.
        consecutiveEscapeTriggerCount++;
        if (consecutiveEscapeTriggerCount > MAX_CONSECUTIVE_ESCAPES) {
          break; // scan terminates immediately; b and the rest of window are excluded
        }
        piece = "~" + ch;
      } else {
        // Case iii: plain literal (window guarantees representability).
        piece = ch;
        consecutiveEscapeTriggerCount = 0;
      }
    }

    if (current.length + piece.length > MAX_DP_OUTPUT_CHARS_PER_SIGNAL) {
      segments.push(current);
      current = "";
    }
    current += piece;
    transformedLen += piece.length;
    candidateLen = i - start + 1;
  }

  if (current.length > 0) segments.push(current);
  return { candidateLen, transformedLen, segments };
}

/**
 * The offset of the first position at or after `from` where a Dynamic Passthrough
 * candidate could begin -- the first position whose representable run reaches
 * MIN_PASSTHROUGH_BYTES -- or `data.length` if there is none.
 *
 * A DP candidate is never longer than the representable run it starts in, so before this
 * offset the encoder is certain to take the block-mode branch. Block mode over a whole
 * number of 4-byte groups is exactly the concatenation of the per-group results, so that
 * stretch can be encoded in one call instead of re-entering the mode decision every 4
 * bytes. The output is unchanged.
 *
 * The lookahead samples every MIN_PASSTHROUGH_BYTES positions rather than reading all of
 * them: any window that long contains a multiple of the stride, so a qualifying run
 * cannot fall between samples. On high-entropy input nearly every sample lands on an
 * unrepresentable byte and is rejected on its first lookup, which is what makes the
 * lookahead cheaper than the work it removes.
 */
function firstDpCapableRun(data: Uint8Array, from: number): number {
  const n = data.length;
  let p = from;
  while (p < n) {
    if (IS_REPRESENTABLE_BYTE[data[p] as number] === 0) {
      p += MIN_PASSTHROUGH_BYTES;
      continue;
    }

    // Back to this run's start, but never before `from`: earlier positions are not the
    // caller's concern.
    let start = p;
    while (start > from && IS_REPRESENTABLE_BYTE[data[start - 1] as number] === 1) start--;

    // Forward only until the threshold is settled either way.
    let end = p;
    while (end < n && IS_REPRESENTABLE_BYTE[data[end] as number] === 1) {
      end++;
      if (end - start >= MIN_PASSTHROUGH_BYTES) return start;
    }

    // Too short. Resume the lattice at this run's end; a later run of the required
    // length still cannot dodge it.
    p = end;
    if (p === from) p++; // defensive: always make progress
  }
  return n;
}

/** Section 6.2: ProcessWithBlockMode -- encode a byte range using standard 4-byte-to-5-char blocks. */
function processWithBlockMode(data: Uint8Array, start: number, len: number): string {
  let out = "";
  let i = 0;
  while (i < len) {
    const remaining = len - i;
    const chunkLen = Math.min(4, remaining);
    const b0 = data[start + i] as number;
    const b1 = chunkLen > 1 ? (data[start + i + 1] as number) : 0;
    const b2 = chunkLen > 2 ? (data[start + i + 2] as number) : 0;
    const b3 = chunkLen > 3 ? (data[start + i + 3] as number) : 0;
    const value = b0 * 16777216 + b1 * 65536 + b2 * 256 + b3;
    const chars = valueToBase85Chars(value);
    // Full block -> all 5 chars. Partial (1/2/3 trailing bytes) -> first 2/3/4 chars.
    out += chunkLen === 4 ? chars : chars.slice(0, chunkLen + 1);
    i += chunkLen;
  }
  return out;
}

/** Build the 5-character DP signal for a segment with the given R-Set mask and character length. */
function buildDpSignal(mask: number, segmentLength: number): string {
  const signalPayload = mask * LENGTH_FIELD_DIVISOR + segmentLength;
  const value = BLOCK_VALUE_LIMIT + signalPayload;
  return valueToBase85Chars(value);
}

/**
 * Encode `data` (raw bytes) into a Base85N string.
 */
export function encode(data: Uint8Array): string {
  let out = "";
  let pos = 0;
  const n = data.length;

  // State of the representable run currently being consumed; end === 0 with pos === 0
  // forces the first scan.
  let run: RunState = { counts: new Int32Array(R_SET_ASCII.length), mask: 0, end: 0 };

  while (pos < n) {
    if (pos >= run.end) {
      // Entering a run that has not been scanned yet. The final block-mode branch below
      // ignores representability and can step past run.end, landing in a later run which
      // is then scanned here; runs handled this way are disjoint, so the total scanning
      // work stays linear in n.
      run = scanRun(data, pos);
    }
    const windowLen = run.end - pos;

    // Skip the mode decision where it cannot change the answer. Until the next run that
    // reaches MIN_PASSTHROUGH_BYTES the block-mode branch is certain, and block mode over
    // whole 4-byte groups is the concatenation of the per-group results, so that whole
    // stretch goes out in one call. Only worth trying when the current window is itself
    // too short for DP: inside a long representable run the scan would return immediately
    // and cost a rescan for nothing.
    if (windowLen < MIN_PASSTHROUGH_BYTES) {
      const limit = firstDpCapableRun(data, pos);
      const batch = Math.floor((limit - pos) / 4) * 4;
      if (batch >= 4) {
        out += processWithBlockMode(data, pos, batch);
        pos += batch;
        // The batch may end mid-run, so the cached run state no longer describes the
        // new position.
        run.end = 0;
        continue;
      }
    }

    const windowMask = run.mask;
    const { candidateLen, transformedLen, segments } = pass2Candidate(data, pos, windowLen, windowMask);
    const finalMask = windowMask;

    let useDpMode = false;
    if (candidateLen >= MIN_PASSTHROUGH_BYTES) {
      // Uses the *actual* number of segments this candidate prefix will occupy (which can
      // exceed the naive ceil(transformedLen / 511) estimate when escape-pair-aware packing
      // must leave a little slack at a segment boundary), so the DP-vs-block-mode efficiency
      // comparison always reflects the real output length that will be produced below.
      const conceptualDpOutputLength = segments.length * 5 + transformedLen;
      const blockModeOutputLength = Math.ceil(candidateLen / 4) * 5;
      useDpMode = conceptualDpOutputLength <= blockModeOutputLength;
    }

    let consumed: number;
    if (useDpMode) {
      for (const segment of segments) {
        out += buildDpSignal(finalMask, segment.length);
        out += segment;
      }
      consumed = candidateLen;
    } else if (candidateLen >= 4) {
      // DP mode not chosen. Per spec Section 6.1 step 2.b, block-encode only the exact
      // multiple-of-4 leading portion of candidateLen immediately; any 0-3 trailing bytes
      // are deferred, unpadded, to the next loop iteration.
      consumed = Math.floor(candidateLen / 4) * 4;
      out += processWithBlockMode(data, pos, consumed);
    } else {
      // Fewer than 4 candidate bytes (or no representable prefix at all). This is the
      // branch that can consume past run.end.
      consumed = Math.min(4, n - pos);
      out += processWithBlockMode(data, pos, consumed);
    }

    if (pos + consumed < run.end) {
      // Still inside the same run: retire the consumed bytes so the next iteration's mask
      // covers exactly the remainder.
      runConsume(data, pos, pos + consumed, run);
    }
    pos += consumed;
  }

  return out;
}

// Re-exported for testing/introspection purposes.
export const _internal = {
  scanRun,
  pass2Candidate,
  firstDpCapableRun,
  processWithBlockMode,
  buildDpSignal,
};
