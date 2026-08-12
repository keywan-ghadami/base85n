/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * Base85N encoder. Implements spec Section 6: the single-scan Dynamic Prefix
 * Identification of step 1, and the deferred-remainder Block Mode fallback of
 * step 2.b.
 *
 * Step 1 asks the same question of eight replacement alphabets -- how far from here
 * can you represent every byte -- and takes the alphabet that reaches furthest,
 * smallest identifier winning a tie. Asking them one at a time would walk the window
 * eight times; `scanAlphabets` walks it once, carrying a live set of the alphabets
 * still able to represent everything seen so far. `REPR` gives that set for a byte in
 * one lookup, so the walk is an AND per byte, and an alphabet's run ends exactly where
 * its bit leaves the set.
 *
 * That also settles spec Section 6.6 with no state carried between iterations: the
 * scan costs `bestLen` byte inspections and the loop then consumes `bestLen` bytes
 * under Dynamic Passthrough, or at least `bestLen - 3` under block mode, so the work
 * per byte of input is bounded by a small constant rather than by the window size.
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
 */
function scanAlphabets(data: Uint8Array, pos: number): PrefixScan {
  const limit = Math.min(data.length - pos, MAX_DP_ANALYSIS_BYTES);

  // Bit a stays set while alphabet a has represented every byte so far.
  let live = ALL_ALPHABETS;
  const stop = new Int32Array(NUM_ALPHABETS);

  let i = 0;
  for (; i < limit; i++) {
    const next = live & (REPR[data[pos + i] as number] as number);
    if (next !== live) {
      let dropped = live & ~next;
      while (dropped !== 0) {
        const bit = dropped & -dropped;
        stop[31 - Math.clz32(bit)] = i;
        dropped &= dropped - 1;
      }
      live = next;
      if (live === 0) break;
    }
  }
  // Whatever is still live reaches the end of the window.
  let rest = live;
  while (rest !== 0) {
    const bit = rest & -rest;
    stop[31 - Math.clz32(bit)] = i;
    rest &= rest - 1;
  }

  let bestLen = 0;
  let bestAlphabet = 0;
  for (let a = 0; a < NUM_ALPHABETS; a++) {
    // Strictly greater keeps the smallest identifier on a tie.
    if ((stop[a] as number) > bestLen) {
      bestLen = stop[a] as number;
      bestAlphabet = a;
    }
  }
  return { bestLen, bestAlphabet };
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
  // block-mode iterations are converted in one call instead of four bytes at a time.
  // This does not change which positions the loop visits: every block-mode consumption
  // is a whole number of 4-byte groups, so the concatenation of the per-iteration
  // results is exactly the block-mode encoding of the accumulated range.
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

    // Step 2.b, block-mode fallback. A candidate of 4 bytes or more gives up only its
    // whole 4-byte groups; the trailing 1-3 bytes stay unpadded for the next iteration,
    // since padding a non-final remainder would be indistinguishable from the start of
    // the next group to a decoder.
    let consumed: number;
    if (bestLen >= 4) {
      consumed = Math.floor(bestLen / 4) * 4;
    } else {
      // Fewer than 4 representable bytes under every alphabet. This is the branch that
      // ignores representability entirely.
      consumed = Math.min(4, n - pos);
    }
    if (blockStart < 0) blockStart = pos;
    pos += consumed;
  }

  if (blockStart >= 0) {
    out += processWithBlockMode(data, blockStart, pos - blockStart);
  }

  return out;
}

// Re-exported for testing/introspection purposes.
export const _internal = {
  scanAlphabets,
  processWithBlockMode,
  buildDpSignal,
  transformSegment,
};
