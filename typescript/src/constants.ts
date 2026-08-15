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

/**
 * R-Set characters (Section 4.1): ASCII value for R-Set index j (0-12). The
 * indices are normative -- they fix the bit positions in a segment's mask.
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

/** The size of the R-Set, and the width of a segment's mask field. */
export const R_SET_LEN = 13;

if (R_SET_ASCII.length !== R_SET_LEN) {
  throw new Error("internal error: expected exactly 13 R-Set entries");
}

/** The number of donor profiles, and the range of the signal's 3-bit id. */
export const NUM_PROFILES = 8;

/**
 * The eight donor profiles of Section 4.2: each an ordered sequence of 13
 * distinct Alphabet-N characters.
 *
 * A segment whose mask has k bits set spends the profile's first k characters,
 * in mask-bit order, as the stand-ins for the R-Set characters that occur in
 * it. Only those first k become unrepresentable; the rest of the profile is
 * still ordinary data, which is why a profile is a ranking and not an alphabet.
 */
export const PROFILES: readonly string[] = [
  "~^?%@+`$#!*.-",
  "~^+[]`?@!%#*(",
  "^~$#?%!`@[]+_",
  "~+?%@!^[]:`()",
  "~%^`+?!$@(){}",
  "^~?@!+%*$()_#",
  "^~@%?$+!#[]=*",
  "^$~@?!%`[]:}{",
];

if (PROFILES.length !== NUM_PROFILES || PROFILES.some((p) => p.length !== R_SET_LEN)) {
  throw new Error("internal error: expected 8 profiles of 13 donors each");
}

/**
 * Byte-indexed lookup tables.
 *
 * Every input byte is classified in the encoder's hot path. A `Map` lookup hashes its
 * key, and the key had to be built with `String.fromCharCode` first, allocating a
 * one-character string per byte. Typed arrays indexed by the byte remove both costs.
 * All of them are derived from ALPHABET_N_CHARS_STR, R_SET_ASCII and PROFILES, so
 * there is no second copy of Section 4 to keep in step.
 */

/** Alphabet-N membership by byte value. */
export const IS_ALPHABET_N_BYTE: Uint8Array = (() => {
  const t = new Uint8Array(256);
  for (const ch of ALPHABET_N_CHARS_STR) {
    t[ch.charCodeAt(0)] = 1;
  }
  return t;
})();

/** R-Set index j (0-12) by byte value, or -1. */
export const RSET_INDEX: Int8Array = (() => {
  const t = new Int8Array(256).fill(-1);
  R_SET_ASCII.forEach((ascii, j) => {
    t[ascii] = j;
  });
  return t;
})();

/** 1 for a byte that a DP segment could carry: Alphabet-N or R-Set. */
export const IS_REPRESENTABLE: Uint8Array = (() => {
  const t = new Uint8Array(256);
  for (let b = 0; b < 256; b++) {
    if (IS_ALPHABET_N_BYTE[b] === 1 || RSET_INDEX[b] !== -1) t[b] = 1;
  }
  return t;
})();

/**
 * The rank a byte holds in each profile, laid out as NUM_PROFILES entries per
 * byte: RANKS[b * NUM_PROFILES + p]. A character a profile does not contain
 * ranks RANK_ABSENT there, one past the last real rank, so "absent" and "ranked
 * below no possible k" are the same value.
 */
export const RANK_ABSENT = R_SET_LEN;

export const RANKS: Uint8Array = (() => {
  const t = new Uint8Array(256 * NUM_PROFILES).fill(RANK_ABSENT);
  PROFILES.forEach((profile, p) => {
    for (let rank = 0; rank < profile.length; rank++) {
      t[profile.charCodeAt(rank) * NUM_PROFILES + p] = rank;
    }
  });
  return t;
})();

/**
 * 1 for a byte that appears in at least one profile, and so could narrow the
 * choice of profile if it occurs as a literal.
 *
 * Only ~20 of the 85 alphabet characters do. Testing this first keeps the
 * prefix scan's per-byte cost at a single lookup for ordinary text, and pays
 * the eight-lane update only where it can change something.
 */
export const IS_PROFILE_MEMBER: Uint8Array = (() => {
  const t = new Uint8Array(256);
  for (const profile of PROFILES) {
    for (const ch of profile) t[ch.charCodeAt(0)] = 1;
  }
  return t;
})();

/** Set in DEC_BASE for a character that is not a member of Alphabet-N. */
export const DEC_INVALID = 0x8000;

/**
 * The decoding table for a DP segment before its donors are patched in: an
 * Alphabet-N character stands for its own byte value, anything else is invalid.
 * One lookup then answers both questions a decoder has about a character.
 *
 * Every Alphabet-N character is ASCII, so the table covers 0..127 and the
 * decoder rejects a code at or above 128 with a comparison rather than a lookup.
 */
export const DEC_BASE: Uint16Array = (() => {
  const t = new Uint16Array(128);
  for (let c = 0; c < 128; c++) t[c] = IS_ALPHABET_N_BYTE[c] === 1 ? c : DEC_INVALID | c;
  return t;
})();

/** Section 6.4 constants. */
export const MAX_DP_ANALYSIS_BYTES = 2048;
export const MAX_DP_SEGMENT_CHARS = 2048; // 11-bit length field, biased by one
export const MIN_PASSTHROUGH_BYTES = 20;
export const MIN_FILL_BYTES = 5;
/**
 * The shortest run of identical bytes that ends a DP segment. Inside
 * passthrough text a run already costs one character per byte, so breaking out
 * to a Fill signal also costs the signal that resumes passthrough afterwards:
 * it pays only from eleven bytes up.
 */
export const MIN_FILL_IN_SEGMENT_BYTES = 11;
export const MAX_FILL_BYTES = 2048;

/**
 * Alphabet-N value by character code, -1 for anything that is not a member.
 *
 * Reading it as a signed array makes "not a member" survive an OR, so one sign test
 * covers a whole 5-character group.
 */
export const VALUE_BY_CHAR_CODE: Int8Array = (() => {
  const t = new Int8Array(128).fill(-1);
  for (let v = 0; v < ALPHABET_N_CHARS_STR.length; v++) {
    t[ALPHABET_N_CHARS_STR.charCodeAt(v)] = v;
  }
  return t;
})();

/** Section 8 powers of 85, for weighing a group's digits without chaining multiplies. */
export const POW85_2 = 85 ** 2;
export const POW85_3 = 85 ** 3;
export const POW85_4 = 85 ** 4;

/** Section 9: the numeric ranges a 5-character group's value can fall in. */
export const BLOCK_VALUE_LIMIT = 2 ** 32; // value < this => standard 4-byte block
export const FILL_SIGNAL_BASE = BLOCK_VALUE_LIMIT + 2 ** 27; // 3 profile + 13 mask + 11 length
export const FUTURE_SIGNAL_BASE = FILL_SIGNAL_BASE + 2 ** 19; // 8 byte value + 11 length

/** Field widths within a signal payload, as divisors (Section 9). */
export const LENGTH_FIELD_DIVISOR = 2 ** 11;
export const MASK_FIELD_DIVISOR = 2 ** 13;

/** Whitespace characters ignored between Base85N constructs (Section 7.1). */
export const IGNORED_WHITESPACE = new Set<string>([" ", "\t", "\n", "\r"]);
