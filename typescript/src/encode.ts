/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * Base85N encoder. Implements spec Section 6: the single-scan Dynamic Prefix
 * Identification of step 1, and the one-group Block Mode fallback of step 2.b.
 *
 * Step 1 asks the same question of eight replacement alphabets -- how far from here
 * can you represent every byte -- and takes the alphabet that reaches furthest,
 * smallest identifier winning a tie. Asking them one at a time would walk the window
 * eight times; `scanAlphabets` walks it once, carrying a live set of the alphabets
 * still able to represent everything seen so far. `REPR` gives that set for a byte in
 * one lookup, so the walk is an AND per byte, and an alphabet's run ends exactly where
 * its bit leaves the set.
 *
 * That also settles spec Section 6.6 with no state carried between iterations. When
 * Dynamic Passthrough is taken the scan costs `bestLen` inspections and the loop
 * consumes `bestLen` bytes. When it is not, the scan is bounded by how far a candidate
 * got -- under 20 bytes, by definition of the branch -- and `firstDpCapableRun` then
 * skips the whole stretch in which no alphabet can reach the threshold, so the loop
 * does not re-enter the decision every 4 bytes either.
 */
import {
  BLOCK_VALUE_LIMIT,
  ENC_XLAT,
  LENGTH_FIELD_DIVISOR,
  MAX_DP_ANALYSIS_BYTES,
  MIN_PASSTHROUGH_BYTES,
  NUM_ALPHABETS,
  REPR,
} from "./constants.js";
import { valueToBase85Chars } from "./digits.js";

/** What `scanAlphabets` found: the longest representable prefix and its alphabet. */
interface PrefixScan {
  /** Length of the longest representable prefix, capped at MAX_DP_ANALYSIS_BYTES. */
  bestLen: number;
  /** Identifier of the alphabet achieving it; smallest wins a tie. */
  bestAlphabet: number;
}

const ALL_ALPHABETS = (1 << NUM_ALPHABETS) - 1;

/**
 * spec Section 6.1, step 1 (Dynamic Prefix Identification), resolved for all eight
 * alphabets in a single walk from `pos`.
 *
 * No per-alphabet bookkeeping is needed: an alphabet that drops out earlier reaches
 * strictly less far than one still in `live`, so when the walk stops -- at the first
 * byte no surviving alphabet can carry, or at the cap -- `live` is exactly the set
 * achieving the greatest length, and that length is the position reached.
 */
function scanAlphabets(data: Uint8Array, pos: number): PrefixScan {
  const limit = Math.min(data.length - pos, MAX_DP_ANALYSIS_BYTES);

  let live = ALL_ALPHABETS;
  let i = 0;
  for (; i < limit; i++) {
    const next = live & (REPR[data[pos + i] as number] as number);
    if (next === 0) break; // every surviving alphabet ends here
    live = next;
  }

  // Lowest set bit: the smallest identifier, which is the tie-break spec Section 6.1
  // step 1 requires. `live` is never zero here.
  return { bestLen: i, bestAlphabet: 31 - Math.clz32(live & -live) };
}

/**
 * The first offset at or after `from` where a Dynamic Passthrough candidate could
 * begin -- the first position starting a run of at least MIN_PASSTHROUGH_BYTES bytes
 * that some alphabet can represent -- or `data.length` if there is none.
 *
 * Every position before it takes the block-mode branch and consumes exactly 4 bytes,
 * so the encoder may jump to the last 4-byte boundary at or before it without
 * changing the output (spec Section 6.6).
 *
 * The lookahead samples every MIN_PASSTHROUGH_BYTES positions rather than reading all
 * of them: any window that long contains a multiple of the stride, so a qualifying run
 * cannot fall between samples. On high-entropy input nearly every sample lands on a
 * byte no alphabet can represent and is rejected on its first lookup, which is what
 * makes the lookahead cheaper than the work it removes.
 */
function firstDpCapableRun(data: Uint8Array, from: number): number {
  const n = data.length;
  let p = from;
  while (p < n) {
    if (REPR[data[p] as number] === 0) {
      p += MIN_PASSTHROUGH_BYTES;
      continue;
    }

    // Back to this run's start, but never before `from`.
    let start = p;
    while (start > from && (REPR[data[start - 1] as number] as number) !== 0) start--;

    // Forward only until the threshold is settled either way.
    let end = p;
    while (end < n && (REPR[data[end] as number] as number) !== 0) {
      end++;
      if (end - start >= MIN_PASSTHROUGH_BYTES) return start;
    }

    // Too short. Resume the lattice at this run's end.
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

/**
 * Build the 5-character DP signal for a segment with the given alphabet identifier and
 * character length. Section 9 stores the length biased by one, so the smallest segment
 * a signal can name is 1 character and the largest 1024.
 */
function buildDpSignal(alphabet: number, segmentLength: number): string {
  const signalPayload = alphabet * LENGTH_FIELD_DIVISOR + (segmentLength - 1);
  return valueToBase85Chars(BLOCK_VALUE_LIMIT + signalPayload);
}

/** Transform `len` bytes at `start` into DP characters under `alphabet`. */
function transformSegment(data: Uint8Array, start: number, len: number, alphabet: number): string {
  const xlat = ENC_XLAT[alphabet] as Uint8Array;
  // Built in chunks so that a 1024-byte segment does not go through
  // String.fromCharCode with an argument list that long.
  let out = "";
  const CHUNK = 256;
  for (let i = 0; i < len; i += CHUNK) {
    const upto = Math.min(CHUNK, len - i);
    const codes = new Array<number>(upto);
    for (let k = 0; k < upto; k++) {
      codes[k] = xlat[data[start + i + k] as number] as number;
    }
    out += String.fromCharCode(...codes);
  }
  return out;
}

/**
 * Encode `data` (raw bytes) into a Base85N string.
 */
export function encode(data: Uint8Array): string {
  let out = "";
  let pos = 0;
  const n = data.length;

  // Start of the pending run of block-mode bytes, or -1 for none. Consecutive
  // block-mode iterations are converted in one call instead of four bytes at a time,
  // and stretches where no alphabet can reach MIN_PASSTHROUGH_BYTES are skipped
  // outright. Neither changes the output: block mode consumes exactly one 4-byte group
  // per iteration, so every position skipped would have taken that branch, and block
  // mode over a whole number of groups is the concatenation of the per-group results.
  let blockStart = -1;

  while (pos < n) {
    const { bestLen, bestAlphabet } = scanAlphabets(data, pos);

    if (bestLen >= MIN_PASSTHROUGH_BYTES) {
      // Step 2.a. At MIN_PASSTHROUGH_BYTES the two modes cost the same 25 characters
      // and Dynamic Passthrough only gains from there, so the length test settles the
      // size comparison too.
      if (blockStart >= 0) {
        out += processWithBlockMode(data, blockStart, pos - blockStart);
        blockStart = -1;
      }
      out += buildDpSignal(bestAlphabet, bestLen);
      out += transformSegment(data, pos, bestLen, bestAlphabet);
      pos += bestLen;
      continue;
    }

    // Step 2.b, block-mode fallback: exactly one 4-byte group, however long the failed
    // candidate was. Nothing but the end of the input can hand processWithBlockMode a
    // partial group this way.
    if (blockStart < 0) blockStart = pos;
    pos += Math.min(4, n - pos);

    // Skip the stretch in which no alphabet can reach the DP threshold. Every position
    // passed over would have taken this same branch and consumed 4 bytes, so the output
    // is unchanged.
    const limit = firstDpCapableRun(data, pos);
    pos += Math.floor((limit - pos) / 4) * 4;
  }

  if (blockStart >= 0) {
    out += processWithBlockMode(data, blockStart, pos - blockStart);
  }

  return out;
}

// Re-exported for testing/introspection purposes.
export const _internal = {
  scanAlphabets,
  firstDpCapableRun,
  processWithBlockMode,
  buildDpSignal,
  transformSegment,
};
