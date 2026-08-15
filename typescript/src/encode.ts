/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * Base85N encoder. Implements spec Section 6: the Fill scan of step 1, the DP
 * prefix scan of step 2, and the one-group Block Mode fallback of step 4.
 *
 * The interesting part is the prefix scan. Version 0.4.0 does not choose between
 * eight fixed alphabets; it *builds* the substitution for each segment out of two
 * fields, and the scan has to keep both viable as it walks forward:
 *
 *  - `mask` grows by one bit the first time each R-Set character occurs, and
 *    k = popcount(mask) is how many donors the segment is spending.
 *  - Every literal Alphabet-N character rules out the profiles that would spend
 *    *it* as one of those k donors.
 *
 * A profile stays viable exactly while the lowest rank any literal character holds
 * in it is at least k, so the scan carries one number per profile -- `minDonor`
 * below -- and re-picks the smallest viable profile whenever either side moves.
 * Most bytes move neither: IS_PROFILE_MEMBER settles that in one lookup.
 */
import {
  BLOCK_VALUE_LIMIT,
  FILL_SIGNAL_BASE,
  IS_PROFILE_MEMBER,
  IS_REPRESENTABLE,
  MASK_FIELD_DIVISOR,
  MAX_DP_ANALYSIS_BYTES,
  MAX_FILL_BYTES,
  MIN_FILL_BYTES,
  MIN_FILL_IN_SEGMENT_BYTES,
  MIN_PASSTHROUGH_BYTES,
  NUM_PROFILES,
  PROFILES,
  RANKS,
  RANK_ABSENT,
  R_SET_ASCII,
  R_SET_LEN,
  LENGTH_FIELD_DIVISOR,
  RSET_INDEX,
} from "./constants.js";
import { valueToBase85Chars } from "./digits.js";

/** What `scanDp` found: the accepted prefix and the signal fields for it. */
interface PrefixScan {
  /** Length of the accepted prefix, capped at MAX_DP_ANALYSIS_BYTES. */
  length: number;
  /** Which R-Set characters occur in it. */
  mask: number;
  /** The smallest profile identifier that can carry them. */
  profile: number;
}

/**
 * Scratch state for `scanDp`: the lowest rank any literal character seen so far
 * holds in each profile. Module-level so that the scan, which runs once per
 * encoder iteration, does not allocate.
 */
const minDonor = new Uint8Array(NUM_PROFILES);

/** The smallest profile whose lowest literal rank is at least `k`, or -1. */
function smallestViableProfile(k: number): number {
  for (let p = 0; p < NUM_PROFILES; p++) {
    if ((minDonor[p] as number) >= k) return p;
  }
  return -1;
}

/**
 * The next position at or after `from` where the main loop could take a branch
 * other than block mode, given that it is inside a block-mode run and therefore
 * only ever *visits* positions `from`, `from + 4`, `from + 8`, ...
 *
 * Only those positions have to be tested, and at each of them the two tests are
 * exact rather than heuristic: a Fill segment starts there iff MIN_FILL_BYTES
 * equal bytes do, and a DP segment can only start there if
 * MIN_PASSTHROUGH_BYTES representable bytes do. Both bail out on their first
 * counterexample, which on high-entropy input is the second byte they read.
 *
 * The caller may jump straight to the returned position: every position it
 * passes over would have taken step 4 and consumed exactly 4 bytes, and block
 * mode over a whole number of groups is the concatenation of the per-group
 * results, so the output is unchanged.
 */
function nextDecisionPoint(data: Uint8Array, from: number): number {
  const n = data.length;
  for (let q = from; q < n; q += 4) {
    if (q + 1 < n && data[q + 1] === data[q]) {
      const limit = Math.min(n, q + MIN_FILL_BYTES);
      let e = q + 1;
      while (e < limit && data[e] === data[q]) e++;
      if (e - q >= MIN_FILL_BYTES) return q;
    }
    if (IS_REPRESENTABLE[data[q] as number] !== 0) {
      const limit = Math.min(n, q + MIN_PASSTHROUGH_BYTES);
      let e = q;
      while (e < limit && (IS_REPRESENTABLE[data[e] as number] as number) !== 0) e++;
      if (e - q >= MIN_PASSTHROUGH_BYTES) return q;
    }
  }
  return n;
}

/**
 * Step 1: the length of the run of identical bytes starting at `data[pos]`,
 * capped at MAX_FILL_BYTES.
 */
function fillRun(data: Uint8Array, pos: number): number {
  const b = data[pos] as number;
  const limit = Math.min(data.length - pos, MAX_FILL_BYTES);
  let i = 1;
  while (i < limit && data[pos + i] === b) i++;
  return i;
}

/**
 * Step 2 (Section 6.2): the longest prefix from `pos` that one profile can
 * carry, with the mask and profile in effect for it. On a stop, the state
 * returned is the one in effect *before* the byte that ended the scan.
 *
 * The scan also stops where a run of MIN_FILL_IN_SEGMENT_BYTES identical bytes
 * begins, so that Fill can reach runs inside passthrough text (Section 6.5,
 * rule 1). The rolled-back state is what that costs: a run's first byte may
 * have widened the mask or narrowed the profile choice, and the bytes after it
 * cannot have changed anything, being equal to a byte already accounted for.
 */
function scanDp(data: Uint8Array, pos: number): PrefixScan {
  const limit = Math.min(data.length - pos, MAX_DP_ANALYSIS_BYTES);

  let mask = 0;
  let k = 0;
  let profile = 0;
  minDonor.fill(RANK_ABSENT);

  // The state as it stood before the most recent change, and where that change
  // happened. At most 26 changes can occur in a segment, so this costs nothing
  // per byte.
  let prevMask = 0;
  let prevProfile = 0;
  let prevPos = -1;

  // Length of the run of identical bytes ending just before `i`.
  let run = 0;

  let i = 0;
  while (i < limit) {
    const b = data[pos + i] as number;

    if (i > 0 && b === data[pos + i - 1]) {
      run++;
      if (run + 1 >= MIN_FILL_IN_SEGMENT_BYTES) {
        const start = i - run;
        if (prevPos === start) return { length: start, mask: prevMask, profile: prevProfile };
        return { length: start, mask, profile };
      }
    } else {
      run = 0;
    }

    const j = RSET_INDEX[b] as number;
    if (j >= 0) {
      const bit = 1 << j;
      if ((mask & bit) !== 0) {
        i++; // already named by the mask; nothing changes
        continue;
      }
      // One more donor to spend: every profile whose lowest literal rank has
      // been reached now drops out.
      const viable = smallestViableProfile(k + 1);
      if (viable < 0) break;
      prevMask = mask;
      prevProfile = profile;
      prevPos = i;
      profile = viable;
      mask |= bit;
      k++;
    } else {
      if (IS_REPRESENTABLE[b] === 0) break; // not representable under any mask
      if (IS_PROFILE_MEMBER[b] === 0) {
        i++; // no profile spends it, so it constrains nothing
        continue;
      }
      let changed = false;
      const base = b * NUM_PROFILES;
      for (let p = 0; p < NUM_PROFILES; p++) {
        const rank = RANKS[base + p] as number;
        if (rank < (minDonor[p] as number)) {
          minDonor[p] = rank;
          changed = true;
        }
      }
      if (!changed) {
        i++;
        continue;
      }
      const viable = smallestViableProfile(k);
      // The literal has ruled out every profile: the segment ends before it,
      // with the state Section 6.2 says to commit -- the one from before this
      // byte, whose lowered ranks are discarded along with it.
      if (viable < 0) break;
      prevMask = mask;
      prevProfile = profile;
      prevPos = i;
      profile = viable;
    }
    i++;
  }

  return { length: i, mask, profile };
}

/** Section 6.3: encode a byte range using standard 4-byte-to-5-char blocks. */
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
 * Build the 5-character DP signal for a segment (Section 9). The length is
 * stored biased by one, so the smallest segment a signal can name is 1
 * character and the largest MAX_DP_SEGMENT_CHARS.
 */
function buildDpSignal(profile: number, mask: number, segmentLength: number): string {
  const payload =
    profile * MASK_FIELD_DIVISOR * LENGTH_FIELD_DIVISOR +
    mask * LENGTH_FIELD_DIVISOR +
    (segmentLength - 1);
  return valueToBase85Chars(BLOCK_VALUE_LIMIT + payload);
}

/** Build the 5-character Solid Fill signal for `byte` repeated `length` times. */
function buildFillSignal(byte: number, length: number): string {
  return valueToBase85Chars(FILL_SIGNAL_BASE + byte * LENGTH_FIELD_DIVISOR + (length - 1));
}

/**
 * Section 4.3's substitution, as a character-code table: the identity over
 * ASCII with the segment's k donors patched in. Every byte a DP segment can
 * carry is ASCII, so 128 entries cover it.
 */
const encXlat = new Uint8Array(128);

function buildEncXlat(profile: number, mask: number): void {
  for (let b = 0; b < 128; b++) encXlat[b] = b;
  const donors = PROFILES[profile] as string;
  let rank = 0;
  for (let j = 0; j < R_SET_LEN; j++) {
    if ((mask & (1 << j)) !== 0) {
      encXlat[R_SET_ASCII[j] as number] = donors.charCodeAt(rank);
      rank++;
    }
  }
}

/** Transform `len` bytes at `start` into DP characters under the built table. */
function transformSegment(data: Uint8Array, start: number, len: number): string {
  // Built in chunks so that a 2048-byte segment does not go through
  // String.fromCharCode with an argument list that long.
  let out = "";
  const CHUNK = 256;
  for (let i = 0; i < len; i += CHUNK) {
    const upto = Math.min(CHUNK, len - i);
    const codes = new Array<number>(upto);
    for (let k = 0; k < upto; k++) {
      codes[k] = encXlat[(data[start + i + k] as number) & 0x7f] as number;
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
  // block-mode iterations are converted in one call instead of four bytes at a
  // time, which does not change the output: block mode consumes exactly one
  // 4-byte group per iteration, and block mode over a whole number of groups is
  // the concatenation of the per-group results.
  let blockStart = -1;

  while (pos < n) {
    // Step 1: a run of identical bytes long enough to be worth a signal of its
    // own. Five characters for up to 2048 bytes.
    const run = fillRun(data, pos);
    if (run >= MIN_FILL_BYTES) {
      if (blockStart >= 0) {
        out += processWithBlockMode(data, blockStart, pos - blockStart);
        blockStart = -1;
      }
      out += buildFillSignal(data[pos] as number, run);
      pos += run;
      continue;
    }

    // Steps 2 and 3.
    const { length, mask, profile } = scanDp(data, pos);
    if (length >= MIN_PASSTHROUGH_BYTES) {
      // At MIN_PASSTHROUGH_BYTES the two modes cost the same 25 characters and
      // DP only gains from there, so the length test settles step 3's size
      // comparison too.
      if (blockStart >= 0) {
        out += processWithBlockMode(data, blockStart, pos - blockStart);
        blockStart = -1;
      }
      out += buildDpSignal(profile, mask, length);
      buildEncXlat(profile, mask);
      out += transformSegment(data, pos, length);
      pos += length;
      continue;
    }

    // Step 4, block-mode fallback: exactly one 4-byte group, however long the
    // failed candidate was. Nothing but the end of the input can hand
    // processWithBlockMode a partial group this way.
    if (blockStart < 0) blockStart = pos;
    pos += Math.min(4, n - pos);

    // Every position up to the next decision point takes this same branch, so
    // jump to it rather than re-deciding every four bytes.
    //
    // The gate is what keeps the lookahead off the path it cannot help: where
    // the next byte is representable, a DP candidate starts right here and the
    // scan the loop is about to run is the cheaper way to find out how far it
    // reaches. Where it is not, the lookahead runs over binary, which is
    // exactly where it earns its keep.
    if (pos < n && IS_REPRESENTABLE[data[pos] as number] === 0) {
      pos += Math.floor((nextDecisionPoint(data, pos) - pos) / 4) * 4;
    }
  }

  if (blockStart >= 0) {
    out += processWithBlockMode(data, blockStart, pos - blockStart);
  }

  return out;
}

// Re-exported for testing/introspection purposes.
export const _internal = {
  scanDp,
  fillRun,
  nextDecisionPoint,
  processWithBlockMode,
  buildDpSignal,
  buildFillSignal,
};
