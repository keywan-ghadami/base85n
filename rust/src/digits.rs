// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Base85 digit <-> value conversion (spec section 8).

use crate::alphabet::{char_to_value, ALPHABET_N};
#[cfg(test)]
use crate::alphabet::value_to_char;

pub const POW85_2: u32 = 7225; // 85^2
pub const POW85_3: u32 = 614_125; // 85^3
pub const POW85_4: u64 = 52_200_625; // 85^4

/// Alphabet-N characters for every two-digit base-85 value 0..85^2-1, most
/// significant digit first.
///
/// Entries are pairs rather than a flat array so that reading one is a single
/// indexed load. Only the first 85^2 of them are ever read -- block mode
/// touches those twice per 4-byte group, over ~226 cache lines, so they settle
/// into L1 and stay there for the duration of an encode -- but the table is
/// rounded up to a power of two so that an index can be brought into range with
/// a mask.
///
/// That is what pays for the extra 1934 bytes, which are never touched. A
/// remainder the caller has already computed is in range by construction, but
/// nothing in its *type* says so, and the bounds check the compiler then cannot
/// discharge was being paid for with a second division -- 7 instructions on the
/// path that carries every block-mode byte, against 1 for the mask.
///
/// The padding is zero, and that is load-bearing rather than incidental: NUL is
/// not an Alphabet-N character, so an index that somehow left the real range
/// would emit output this project's own decoders reject with
/// `InvalidCharacter`, rather than plausible wrong data. It is what takes the
/// place of the bounds check's panic, so do not tidy it into a "real"
/// character. The same holds for the unused half of `IDENTITY_ASCII`, which the
/// translation loop reaches through `b & 0x7f`.
pub const PAIR_TABLE_LEN: usize = 8192;

/// The mask that brings a two-digit value into [`PAIR_CHARS`]'s range. Every
/// index the encoder forms is already below 85^2; this is what lets the
/// compiler know it.
pub const PAIR_MASK: usize = PAIR_TABLE_LEN - 1;

pub const PAIR_CHARS: [[u8; 2]; PAIR_TABLE_LEN] = {
    let mut table = [[0u8; 2]; PAIR_TABLE_LEN];
    let mut v = 0usize;
    while v < POW85_2 as usize {
        table[v] = [ALPHABET_N[v / 85], ALPHABET_N[v % 85]];
        v += 1;
    }
    table
};

/// [`ALPHABET_N`] padded to 128 entries, for the same reason [`PAIR_CHARS`] is
/// padded to 8192: the middle digit is a remainder below 85, and a mask says so
/// where the division that produced it does not.
pub const DIGIT_CHARS: [u8; 128] = {
    let mut table = [0u8; 128];
    let mut v = 0usize;
    while v < 85 {
        table[v] = ALPHABET_N[v];
        v += 1;
    }
    table
};

/// `Base85DigitsToValue`: combine 5 Alphabet-N digit values (each 0-84)
/// into a single integer, most-significant digit first.
pub fn digits_to_value(digits: &[u8; 5]) -> u64 {
    let mut val: u64 = 0;
    for &d in digits {
        val = val * 85 + d as u64;
    }
    val
}

/// `ValueToBase85Digits`: split `value` into 5 Alphabet-N digit values
/// (each 0-84), most-significant digit first. `value` must be `< 85^5`.
pub fn value_to_digits(mut value: u64) -> [u8; 5] {
    // Above 85^5 the leading digit leaves ALPHABET_N. Section 9 caps every
    // signal below it and `mod digits` is private, so this is internal
    // robustness rather than a public precondition -- but an unchecked one was
    // only ever documented.
    debug_assert!(value < 4_437_053_125, "value_to_digits is defined below 85^5");
    let mut digits = [0u8; 5];
    for i in (0..5).rev() {
        digits[i] = (value % 85) as u8;
        value /= 85;
    }
    digits
}

/// Converts a value (0..85^5-1, which covers every `u32`) into 5 Alphabet-N
/// characters, most significant digit first.
///
/// The obvious loop -- five rounds of "digit = value % 85; value /= 85" --
/// costs five divisions by a constant, each depending on the one before it.
/// Reading the digits out in pairs needs two divisions and three loads instead,
/// and this is the whole of block mode's arithmetic.
///
/// The two divisions are by 85^2 and 85^3 rather than the more obvious 85^3 and
/// then 85^2 of the remainder, so that neither waits for the other: `value /
/// 85^2` is `head*85 + mid`, because `85^3 = 85 * 85^2`, which recovers the
/// middle digit from a quotient computed in parallel with the head.
#[inline]
pub fn value_to_5chars_32(value: u32) -> [u8; 5] {
    let q = value / POW85_2; // = head * 85 + mid
    let head = (value / POW85_3) as usize; // digits 0,1: 0..85^2-1
    let tail = (value - q * POW85_2) as usize; // digits 3,4: 0..85^2-1
    let mid = (q - head as u32 * 85) as usize; // digit 2:    0..84

    // Every index is in range by construction: `head` is at most u32::MAX/85^3
    // = 6993, `tail` is a remainder mod 85^2, and `mid` is a remainder mod 85.
    // The masks are what tell the compiler so; see PAIR_CHARS.
    debug_assert!(head < POW85_2 as usize && tail < POW85_2 as usize && mid < 85);
    let h = PAIR_CHARS[head & PAIR_MASK];
    let t = PAIR_CHARS[tail & PAIR_MASK];
    [h[0], h[1], DIGIT_CHARS[mid & 127], t[0], t[1]]
}

/// Same, for the range of values that does not fit in 32 bits: every signal is
/// `2^32 + payload` or above (spec section 9). One signal covers a whole
/// segment -- up to 2048 characters of DP, or up to 2048 bytes of Fill -- so
/// this path is cold and stays the straightforward loop.
pub fn value_to_5chars_64(value: u64) -> [u8; 5] {
    let digits = value_to_digits(value);
    [
        ALPHABET_N[digits[0] as usize],
        ALPHABET_N[digits[1] as usize],
        ALPHABET_N[digits[2] as usize],
        ALPHABET_N[digits[3] as usize],
        ALPHABET_N[digits[4] as usize],
    ]
}

/// Encode `value` as a 5-character Alphabet-N string. Used by the tests to build
/// signals by hand; the encoder writes digits straight into its output buffer.
#[cfg(test)]
pub fn value_to_group(value: u64) -> String {
    let digits = value_to_digits(value);
    digits.iter().map(|&d| value_to_char(d)).collect()
}

/// Convert 5 already-validated Alphabet-N characters into their combined
/// value. Returns `None` if any character is not in Alphabet-N.
///
/// A fixed-size array rather than a slice: the length is part of the format,
/// the compiler can check it here rather than a `debug_assert!` that a release
/// build drops, and the one caller stops building a `Vec` per five characters
/// to satisfy a slice signature -- on the decoder's error path, which walks a
/// whole rejected stream.
pub fn chars_to_value(chars: &[char; 5]) -> Option<u64> {
    let mut digits = [0u8; 5];
    for (i, &c) in chars.iter().enumerate() {
        digits[i] = char_to_value(c)?;
    }
    Some(digits_to_value(&digits))
}
