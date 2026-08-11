/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * Base85N constants, derived directly from spec sections 4, 6.4 and 9.
 */

/** The 85-character Base85N alphabet (Alphabet-N), indices 0-84. */
export const ALPHABET_N_CHARS_STR =
  "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ.-:+=^!/*?`_~()[]{}@%$#";

if (ALPHABET_N_CHARS_STR.length !== 85) {
  throw new Error("internal error: ALPHABET_N_CHARS_STR must have exactly 85 characters");
}

/** char -> integer value (0-84) */
export const CHAR_TO_VALUE: ReadonlyMap<string, number> = new Map(
  Array.from(ALPHABET_N_CHARS_STR).map((ch, idx) => [ch, idx]),
);

/** integer value (0-84) -> char */
export const VALUE_TO_CHAR: readonly string[] = Array.from(ALPHABET_N_CHARS_STR);

/** The fixed escape character, index 74 of Alphabet-N. */
export const ESCAPE_CHAR = "~";
export const ESCAPE_CHAR_CODE = ESCAPE_CHAR.charCodeAt(0);

/**
 * R-Set characters (Section 4.1): ASCII value for R-Set index j (0-12).
 */
export const R_SET_ASCII: readonly number[] = [
  32, // 0: space
  34, // 1: "
  39, // 2: '
  44, // 3: ,
  59, // 4: ;
  92, // 5: backslash
  124, // 6: |
  60, // 7: <
  62, // 8: >
  38, // 9: &
  9, // 10: \t
  10, // 11: \n
  13, // 12: \r
];

/**
 * Byte-indexed lookup tables.
 *
 * Every input byte is tested for Alphabet-N membership and for R-Set membership,
 * twice per byte in the encoder's hot path. A `Map` lookup hashes its key, and for
 * the alphabet check the key had to be built with `String.fromCharCode` first,
 * allocating a one-character string per byte. Typed arrays indexed by the byte
 * remove both costs. Entries are the index, or -1 for "not a member".
 */
const R_SET_INDEX_BY_BYTE: Int8Array = (() => {
  const t = new Int8Array(256).fill(-1);
  R_SET_ASCII.forEach((ascii, j) => {
    t[ascii] = j;
  });
  return t;
})();

export function rSetIndexForAscii(byte: number): number {
  return R_SET_INDEX_BY_BYTE[byte] as number;
}

/**
 * Allowed passthrough-safe replacement characters (Section 4.2), ordered by R-Set index j.
 */
export const ALLOWED_PASSTHROUGH_SAFE_REPLACEMENT_CHARS: readonly string[] = [
  ":", // j=0 (replaces space)
  "+", // j=1 (replaces ")
  "=", // j=2 (replaces ')
  "^", // j=3 (replaces ,)
  "!", // j=4 (replaces ;)
  "/", // j=5 (replaces backslash)
  "*", // j=6 (replaces |)
  "?", // j=7 (replaces <)
  "`", // j=8 (replaces >)
  "(", // j=9 (replaces &)
  ")", // j=10 (replaces \t)
  "[", // j=11 (replaces \n)
  "]", // j=12 (replaces \r)
];

if (ALLOWED_PASSTHROUGH_SAFE_REPLACEMENT_CHARS.length !== 13 || R_SET_ASCII.length !== 13) {
  throw new Error("internal error: expected exactly 13 R-Set / replacement entries");
}

/** replacement char -> R-Set index j, or -1 if not a passthrough-safe replacement char. */
const REPLACEMENT_INDEX_BY_BYTE: Int8Array = (() => {
  const t = new Int8Array(256).fill(-1);
  ALLOWED_PASSTHROUGH_SAFE_REPLACEMENT_CHARS.forEach((ch, j) => {
    t[ch.charCodeAt(0)] = j;
  });
  return t;
})();

/** Alphabet-N membership by byte value, for the encoder's per-byte check. */
export const IS_ALPHABET_N_BYTE: Uint8Array = (() => {
  const t = new Uint8Array(256);
  for (const ch of ALPHABET_N_CHARS_STR) {
    t[ch.charCodeAt(0)] = 1;
  }
  return t;
})();

/**
 * Pass 1's two membership questions folded into one lookup: a byte belongs to a
 * representable run iff it is an R-Set character or an Alphabet-N character (which
 * includes the escape character and every replacement character, regardless of
 * escaping cost).
 */
export const IS_REPRESENTABLE_BYTE: Uint8Array = (() => {
  const t = new Uint8Array(256);
  for (const ch of ALPHABET_N_CHARS_STR) {
    t[ch.charCodeAt(0)] = 1;
  }
  for (const ascii of R_SET_ASCII) {
    t[ascii] = 1;
  }
  return t;
})();

/** Replacement-character index by byte value, or -1. */
export function replacementIndexForByte(byte: number): number {
  return REPLACEMENT_INDEX_BY_BYTE[byte] as number;
}

const REPLACEMENT_CHAR_TO_INDEX: ReadonlyMap<string, number> = new Map(
  ALLOWED_PASSTHROUGH_SAFE_REPLACEMENT_CHARS.map((ch, j) => [ch, j]),
);

export function replacementIndexForChar(ch: string): number {
  return REPLACEMENT_CHAR_TO_INDEX.get(ch) ?? -1;
}

/** Section 6.4 constants. */
export const MAX_CONSECUTIVE_ESCAPES = 3;
export const MAX_DP_OUTPUT_CHARS_PER_SIGNAL = 511; // 9-bit length field: 0-511
export const MIN_PASSTHROUGH_BYTES = 20;

/**
 * Alphabet-N value by character code, -1 for anything that is not a member.
 *
 * Every Alphabet-N character is ASCII, so the table covers 0..127 and the decoder
 * rejects a code at or above 128 with a comparison rather than a lookup. Reading it as
 * a signed array makes "not a member" survive an OR, so one sign test covers a whole
 * 5-character group.
 */
export const VALUE_BY_CHAR_CODE: Int8Array = (() => {
  const t = new Int8Array(128).fill(-1);
  for (let v = 0; v < ALPHABET_N_CHARS_STR.length; v++) {
    t[ALPHABET_N_CHARS_STR.charCodeAt(v)] = v;
  }
  return t;
})();

/**
 * The decoder's view of a character inside a DP segment, packed so that the segment
 * loop asks one question per character instead of three:
 *
 *   bit 31        DEC_INVALID -- the character is not in Alphabet-N.
 *   bit 30        DEC_ESCAPE -- the character is '~'.
 *   bits 16..28   (1 << j) if the character is replacement character j; intersect with
 *                 the signal's 13-bit mask (shifted into place) to decide whether this
 *                 occurrence stands for an R-Set byte or for itself.
 *   bits 0..7     the byte to emit when it does: R-Set character j's ASCII value, or
 *                 the character itself when it is not a replacement character at all.
 *
 * Derived from the tables above rather than written out, so there is no second copy of
 * Section 4 to keep in step.
 */
export const DEC_INVALID = 0x80000000 | 0;
export const DEC_ESCAPE = 0x40000000;

export const DEC_SUB: Int32Array = (() => {
  const t = new Int32Array(128);
  for (let c = 0; c < 128; c++) {
    const ch = String.fromCharCode(c);
    if (!CHAR_TO_VALUE.has(ch)) {
      t[c] = DEC_INVALID | c;
    } else if (ch === ESCAPE_CHAR) {
      t[c] = DEC_ESCAPE | c;
    } else {
      const j = ALLOWED_PASSTHROUGH_SAFE_REPLACEMENT_CHARS.indexOf(ch);
      t[c] = j === -1 ? c : (1 << (16 + j)) | (R_SET_ASCII[j] as number);
    }
  }
  return t;
})();

/** Section 8 powers of 85, for weighing a group's digits without chaining multiplies. */
export const POW85_2 = 85 ** 2;
export const POW85_3 = 85 ** 3;
export const POW85_4 = 85 ** 4;

/** Section 9: DP signal numeric layout. */
export const BLOCK_VALUE_LIMIT = 2 ** 32; // decodedValue < this => standard block
export const SIGNAL_PAYLOAD_MAX = 2 ** 22 - 1; // max valid SignalPayload
export const LENGTH_FIELD_BITS = 9; // Length_9bit_encoded_value width
export const LENGTH_FIELD_DIVISOR = 2 ** LENGTH_FIELD_BITS; // 512

/** Whitespace characters ignored between Base85N constructs (Section 7.1). */
export const IGNORED_WHITESPACE = new Set<string>([" ", "\t", "\n", "\r"]);
