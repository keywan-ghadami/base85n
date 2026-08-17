/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * Base85N: an encoding for data that has to be embedded in a text-based format --
 * JSON, XML, HTML, configuration files -- where Base64 would otherwise be used and
 * the size or the cleanliness of the result matters. It uses a single
 * 85-character alphabet
 * (Alphabet-N), with a Dynamic Passthrough (DP) mode that carries text-like input
 * at one character per byte and a Fill mode that carries a run of up to
 * 2048 identical bytes in five characters.
 *
 * See the specification in spec/ (base85n-v0.5.0.md) for the full text, in
 * particular Section 4's donor profiles and Section 6's encoding procedure,
 * which this package follows exactly.
 */
export { encode } from "./encode.js";
export { decode } from "./decode.js";
export { Base85NDecodeError } from "./errors.js";
export type { Base85NDecodeErrorCode, Base85NDecodeErrorOptions } from "./errors.js";
export {
  ALPHABET_N_CHARS_STR,
  MAX_DP_ANALYSIS_BYTES,
  MAX_DP_SEGMENT_CHARS,
  MAX_FILL_BYTES,
  MIN_FILL_BYTES,
  MIN_FILL_IN_SEGMENT_BYTES,
  MIN_PASSTHROUGH_BYTES,
  NUM_PROFILES,
  PROFILES,
  R_SET_ASCII,
} from "./constants.js";
