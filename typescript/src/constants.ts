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

if (R_SET_ASCII.length !== 13) {
  throw new Error("internal error: expected exactly 13 R-Set entries");
}

/** The number of replacement alphabets, and the range of the signal's 3-bit id. */
export const NUM_ALPHABETS = 8;

/**
 * The eight replacement alphabets of Section 4.2, each a list of
 * [R-Set index, donor character] substitutions.
 *
 * Under alphabet a, R_SET_ASCII[j] is written as its donor character, the donor
 * character itself becomes unrepresentable, and every other Alphabet-N character
 * represents itself. Each alphabet is therefore injective, which is why this format
 * needs no escape character.
 *
 * The donors are the least frequent Alphabet-N characters measured over a mixed
 * corpus -- ^ @ % $ ? ! ~ #, then * + = _ and backtick -- except where an alphabet's
 * own target shape makes one of them common: alphabet 3 (markup) spends "{" rather
 * than "#", and alphabet 5 (code) spends the backtick, which is rare in source but
 * not in Markdown.
 */
export const REPLACEMENT_ALPHABETS: ReadonlyArray<ReadonlyArray<readonly [number, string]>> = [
  /* 0 none   */ [],
  /* 1 text   */ [[0, "^"], [11, "@"], [12, "%"], [10, "$"]],
  /* 2 prose  */ [[0, "^"], [11, "@"], [3, "%"], [1, "$"], [2, "?"], [4, "!"]],
  /* 3 markup */ [[0, "^"], [11, "@"], [7, "%"], [8, "$"], [9, "?"], [1, "!"], [2, "~"], [3, "{"]],
  /* 4 json   */ [[0, "^"], [11, "@"], [1, "%"], [3, "$"], [5, "?"], [12, "!"]],
  /* 5 code   */ [[0, "^"], [11, "@"], [3, "%"], [4, "$"], [1, "?"], [2, "!"], [10, "~"], [8, "`"]],
  /* 6 shell  */ [[0, "^"], [11, "@"], [6, "%"], [5, "$"], [1, "?"], [2, "!"], [9, "~"], [4, "#"]],
  /* 7 full   */ [
    [0, "^"], [11, "@"], [12, "%"], [10, "$"], [3, "?"], [4, "!"], [1, "~"], [2, "#"],
    [7, "*"], [8, "+"], [9, "="], [6, "_"], [5, "`"],
  ],
];

if (REPLACEMENT_ALPHABETS.length !== NUM_ALPHABETS) {
  throw new Error("internal error: expected exactly 8 replacement alphabets");
}

/**
 * Byte-indexed lookup tables.
 *
 * Every input byte is classified in the encoder's hot path. A `Map` lookup hashes its
 * key, and the key had to be built with `String.fromCharCode` first, allocating a
 * one-character string per byte. Typed arrays indexed by the byte remove both costs.
 * All three tables below are derived from ALPHABET_N_CHARS_STR, R_SET_ASCII and
 * REPLACEMENT_ALPHABETS, so there is no second copy of Section 4 to keep in step.
 */

/** Alphabet-N membership by byte value. */
export const IS_ALPHABET_N_BYTE: Uint8Array = (() => {
  const t = new Uint8Array(256);
  for (const ch of ALPHABET_N_CHARS_STR) {
    t[ch.charCodeAt(0)] = 1;
  }
  return t;
})();

/**
 * REPR[b] is the set of alphabets that can represent byte b: bit a is set iff b is
 * representable under replacement alphabet a (Section 6.1, step 1).
 *
 * This is what lets the encoder settle all eight scans in one pass: it walks forward
 * AND-ing this mask into a live set, and an alphabet's run ends exactly at the
 * position where its bit leaves that set.
 */
export const REPR: Uint8Array = (() => {
  const t = new Uint8Array(256);
  REPLACEMENT_ALPHABETS.forEach((subs, a) => {
    const donor = new Uint8Array(256);
    for (const [, d] of subs) donor[d.charCodeAt(0)] = 1;
    for (let b = 0; b < 256; b++) {
      // An Alphabet-N character represents itself unless this alphabet spends it as a
      // donor; an R-Set character is representable only if this alphabet substitutes
      // it. No byte is both.
      if (IS_ALPHABET_N_BYTE[b] === 1 && donor[b] === 0) t[b] = (t[b] as number) | (1 << a);
    }
    for (const [j] of subs) {
      const ascii = R_SET_ASCII[j] as number;
      t[ascii] = (t[ascii] as number) | (1 << a);
    }
  });
  return t;
})();

/**
 * ENC_XLAT[a][b] is the character code byte b becomes in DP output under alphabet a.
 * Only meaningful where REPR[b] has bit a set.
 */
export const ENC_XLAT: readonly Uint8Array[] = REPLACEMENT_ALPHABETS.map((subs) => {
  const t = new Uint8Array(256);
  for (let b = 0; b < 256; b++) t[b] = b;
  for (const [j, d] of subs) t[R_SET_ASCII[j] as number] = d.charCodeAt(0);
  return t;
});

/** Set in DEC_XLAT for a character that is not a member of Alphabet-N. */
export const DEC_INVALID = 0x8000;

/**
 * DEC_XLAT[a][c] is the byte that character code c stands for under alphabet a, with
 * DEC_INVALID set when c is not in Alphabet-N. One lookup answers both questions a
 * decoder has about a character inside a DP segment, and there is no state to carry
 * between characters: version 0.3.0 has no construct that spans two of them.
 *
 * Every Alphabet-N character is ASCII, so the tables cover 0..127 and the decoder
 * rejects a code at or above 128 with a comparison rather than a lookup.
 */
export const DEC_XLAT: readonly Uint16Array[] = REPLACEMENT_ALPHABETS.map((subs) => {
  const t = new Uint16Array(128);
  for (let c = 0; c < 128; c++) t[c] = IS_ALPHABET_N_BYTE[c] === 1 ? c : DEC_INVALID | c;
  for (const [j, d] of subs) t[d.charCodeAt(0)] = R_SET_ASCII[j] as number;
  return t;
});

/** Section 6.4 constants. */
export const MAX_DP_ANALYSIS_BYTES = 1024;
export const MAX_DP_OUTPUT_CHARS_PER_SIGNAL = 1024; // 10-bit length field, biased by one
export const MIN_PASSTHROUGH_BYTES = 20;

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

/** Section 9: DP signal numeric layout. */
export const BLOCK_VALUE_LIMIT = 2 ** 32; // decodedValue < this => standard block
export const SIGNAL_PAYLOAD_MAX = 2 ** 13 - 1; // max valid SignalPayload
export const LENGTH_FIELD_BITS = 10; // Length_10bit_encoded_value width
export const LENGTH_FIELD_DIVISOR = 2 ** LENGTH_FIELD_BITS; // 1024

/** Whitespace characters ignored between Base85N constructs (Section 7.1). */
export const IGNORED_WHITESPACE = new Set<string>([" ", "\t", "\n", "\r"]);
