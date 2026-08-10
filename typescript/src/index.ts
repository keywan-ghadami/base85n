/**
 * Base85N: a binary-to-text encoding scheme using a single 85-character alphabet
 * (Alphabet-N) with a Dynamic Passthrough (DP) mode for efficient, partially
 * human-readable representation of compatible byte sequences.
 *
 * See the repository README.md for the full specification and NOTES.md for two
 * mandatory clarifications this implementation follows.
 */
export { encode } from "./encode.js";
export { decode } from "./decode.js";
export { Base85NDecodeError } from "./errors.js";
export type { Base85NDecodeErrorCode, Base85NDecodeErrorOptions } from "./errors.js";
export {
  ALPHABET_N_CHARS_STR,
  MAX_CONSECUTIVE_ESCAPES,
  MAX_DP_OUTPUT_CHARS_PER_SIGNAL,
  MIN_PASSTHROUGH_BYTES,
} from "./constants.js";
