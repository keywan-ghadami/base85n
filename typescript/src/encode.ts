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
  CHAR_TO_VALUE,
  ESCAPE_CHAR_CODE,
  LENGTH_FIELD_DIVISOR,
  MAX_CONSECUTIVE_ESCAPES,
  MAX_DP_OUTPUT_CHARS_PER_SIGNAL,
  MIN_PASSTHROUGH_BYTES,
  replacementIndexForChar,
  rSetIndexForAscii,
} from "./constants.js";
import { valueToBase85Chars } from "./digits.js";

/** Result of the Section 6.1 step 1.a prefix scan (Pass 1). */
interface WindowResult {
  /** Number of bytes (starting at the scan's start offset) included in the window. */
  windowLen: number;
  /** R-Set activation mask (13 bits) of every R-Set character seen in the window. */
  windowMask: number;
}

/**
 * spec Section 6.1, step 1.a (Pass 1 -- Window and Mask Discovery): scans data
 * starting at `start`, bounded *only* by representability (an R-Set character, or any
 * Alphabet-N character -- which includes the escape character and all replacement
 * characters unconditionally, regardless of escaping cost). Never terminates early due
 * to escaping cost or the consecutive-escape limit; only an actually-unrepresentable
 * byte ends the window.
 */
function pass1Window(data: Uint8Array, start: number): WindowResult {
  let windowMask = 0;
  let i = start;

  while (i < data.length) {
    const b = data[i] as number;

    const rIdx = rSetIndexForAscii(b);
    if (rIdx !== -1) {
      windowMask |= 1 << rIdx;
      i++;
      continue;
    }

    const ch = String.fromCharCode(b);
    if (CHAR_TO_VALUE.has(ch)) {
      i++;
      continue;
    }

    break; // unrepresentable byte: window ends here
  }

  return { windowLen: i - start, windowMask };
}

/** Result of the Section 6.1 step 1.b prefix scan (Pass 2). */
interface CandidateResult {
  /** Number of bytes (a prefix of the window) included in the DP candidate prefix. */
  candidateLen: number;
  /** Per-source-byte transformed "pieces" (each 1 or 2 characters -- 2 for an escape pair). */
  pieces: string[];
}

/**
 * spec Section 6.1, step 1.b (Pass 2 -- Boundary Finalization with Fixed Mask):
 * re-walks window (data[start, start+windowLen)) using the single, fixed finalMask
 * (== windowMask from Pass 1, never modified here) to apply Case i/ii/iii and the
 * consecutive-escape limit, producing the actual candidate prefix length and its
 * per-source-byte transformed pieces.
 *
 * Pieces are kept separate (rather than immediately concatenated into one string) so
 * that when a candidate prefix's transformed output must be split across multiple DP
 * signal segments (Section 6.4's MAX_DP_OUTPUT_CHARS_PER_SIGNAL = 511), the segmenter
 * (see `packIntoSegments` below) can guarantee it never splits a two-character escape
 * pair ("~~" or "~x") across a segment boundary -- doing so would make the second
 * segment start with an orphaned literal and leave the first segment ending in a lone
 * '~', which the decoder (Section 7.1.e) cannot parse (it decodes each DP segment as a
 * self-contained unit and would report a dangling escape character).
 */
function pass2Candidate(data: Uint8Array, start: number, windowLen: number, finalMask: number): CandidateResult {
  const pieces: string[] = [];
  let consecutiveEscapeTriggerCount = 0;
  let candidateLen = 0;

  for (let i = start; i < start + windowLen; i++) {
    const b = data[i] as number;

    // Case i: R-Set character. finalMask is guaranteed to have this bit set (Pass 1
    // always sets it for any R-Set byte included in window, and bits never clear).
    const rIdx = rSetIndexForAscii(b);
    if (rIdx !== -1) {
      pieces.push(ALLOWED_PASSTHROUGH_SAFE_REPLACEMENT_CHARS[rIdx] as string);
      consecutiveEscapeTriggerCount = 0;
      candidateLen = i - start + 1;
      continue;
    }

    const ch = String.fromCharCode(b);
    const replIdx = replacementIndexForChar(ch);
    const needsEscaping =
      b === ESCAPE_CHAR_CODE || (replIdx !== -1 && (finalMask & (1 << replIdx)) !== 0);

    // Case ii: requires escaping, against the fixed finalMask.
    if (needsEscaping) {
      consecutiveEscapeTriggerCount++;
      if (consecutiveEscapeTriggerCount > MAX_CONSECUTIVE_ESCAPES) {
        break; // scan terminates immediately; b and the rest of window are excluded
      }
      pieces.push(b === ESCAPE_CHAR_CODE ? "~~" : "~" + ch);
      candidateLen = i - start + 1;
      continue;
    }

    // Case iii: plain literal (window guarantees representability).
    pieces.push(ch);
    consecutiveEscapeTriggerCount = 0;
    candidateLen = i - start + 1;
  }

  return { candidateLen, pieces };
}

/**
 * Greedily pack transformed-DP pieces (each of length 1 or 2, never to be split) into
 * segments of at most `maxLen` characters each. Since every piece has length <= 2 and
 * maxLen (511) is odd, this never leaves a segment that could still fit the next piece.
 */
function packIntoSegments(pieces: readonly string[], maxLen: number): string[] {
  const segments: string[] = [];
  let current = "";
  for (const piece of pieces) {
    if (current.length + piece.length > maxLen) {
      segments.push(current);
      current = "";
    }
    current += piece;
  }
  if (current.length > 0 || segments.length === 0) {
    segments.push(current);
  }
  return segments;
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

  while (pos < n) {
    const { windowLen, windowMask } = pass1Window(data, pos);
    const { candidateLen, pieces } = pass2Candidate(data, pos, windowLen, windowMask);
    const finalMask = windowMask;

    let useDpMode = false;
    let segments: string[] = [];
    if (candidateLen >= MIN_PASSTHROUGH_BYTES) {
      const lTransformed = pieces.reduce((sum, p) => sum + p.length, 0);
      segments = packIntoSegments(pieces, MAX_DP_OUTPUT_CHARS_PER_SIGNAL);
      const numSegments = segments.length;
      // Uses the *actual* number of segments this candidate prefix will occupy (which can
      // exceed the naive ceil(lTransformed / 511) estimate when escape-pair-aware packing
      // must leave a little slack at a segment boundary), so the DP-vs-block-mode efficiency
      // comparison always reflects the real output length that will be produced below.
      const conceptualDpOutputLength = numSegments * 5 + lTransformed;
      const blockModeOutputLength = Math.ceil(candidateLen / 4) * 5;
      useDpMode = conceptualDpOutputLength <= blockModeOutputLength;
    }

    if (useDpMode) {
      for (const segment of segments) {
        out += buildDpSignal(finalMask, segment.length);
        out += segment;
      }
      pos += candidateLen;
      continue;
    }

    // DP mode not chosen (or no representable prefix at all). Per spec Section 6.1
    // step 2.b, block-encode only the exact multiple-of-4 leading portion of candidateLen
    // immediately; any 0-3 trailing bytes are deferred, unpadded, to the next loop
    // iteration.
    if (candidateLen >= 4) {
      const fullBlocksLen = Math.floor(candidateLen / 4) * 4;
      out += processWithBlockMode(data, pos, fullBlocksLen);
      pos += fullBlocksLen;
    } else {
      const blockLen = Math.min(4, n - pos);
      out += processWithBlockMode(data, pos, blockLen);
      pos += blockLen;
    }
  }

  return out;
}

// Re-exported for testing/introspection purposes.
export const _internal = {
  pass1Window,
  pass2Candidate,
  packIntoSegments,
  processWithBlockMode,
  buildDpSignal,
};
