/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * Base85N: a binary-to-text encoding scheme using a single 85-character alphabet
 * (Alphabet-N) with a Dynamic Passthrough (DP) mode for efficient, partially
 * human-readable representation of compatible byte sequences.
 *
 * See the specification in spec/ (base85n-v0.3.0.md) for the full text, in
 * particular Section 4.2's eight replacement alphabets and Section 6.1's
 * single-scan Dynamic Passthrough prefix identification, which this package
 * follows exactly.
 */
export { encode } from "./encode.js";
export { decode } from "./decode.js";
export { Base85NDecodeError } from "./errors.js";
export type { Base85NDecodeErrorCode, Base85NDecodeErrorOptions } from "./errors.js";
export {
  ALPHABET_N_CHARS_STR,
  MAX_DP_ANALYSIS_BYTES,
  MAX_DP_OUTPUT_CHARS_PER_SIGNAL,
  MIN_PASSTHROUGH_BYTES,
  REPLACEMENT_ALPHABETS,
} from "./constants.js";
