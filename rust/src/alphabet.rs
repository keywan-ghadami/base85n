// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Alphabet-N, the R-Set, and the passthrough-safe replacement characters
//! defined in spec sections 4-4.3.

/// The 85-character alphabet used by Base85N, in index order (0-84).
pub const ALPHABET_N: &[u8; 85] =
    b"0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ.-:+=^!/*?`_~()[]{}@%$#";

/// The fixed escape character (index 74 of Alphabet-N).
pub const ESCAPE_CHAR: u8 = b'~';

/// The 13 R-Set characters, indexed by R-Set index j (0-12), given as their
/// ASCII byte value. See spec section 4.1.
pub const RSET_ASCII: [u8; 13] = [
    b' ',  // 0  space
    b'"',  // 1  double quote
    b'\'', // 2  single quote
    b',',  // 3  comma
    b';',  // 4  semicolon
    b'\\', // 5  backslash
    b'|',  // 6  pipe
    b'<',  // 7  less-than
    b'>',  // 8  greater-than
    b'&',  // 9  ampersand
    b'\t', // 10 horizontal tab
    b'\n', // 11 line feed
    b'\r', // 12 carriage return
];

/// The 13 "allowed passthrough-safe" replacement characters, indexed by
/// R-Set index j (0-12). See spec section 4.2.
pub const REPLACEMENT_CHARS: [u8; 13] = *b":+=^!/*?`()[]";

/// Byte-indexed lookup tables, evaluated at compile time.
///
/// Every input byte is tested for Alphabet-N membership and for R-Set
/// membership, twice per byte in the encoder's hot path, so how these
/// lookups are implemented dominates encoding time. Earlier versions used a
/// `thread_local` table for the alphabet (a TLS access per byte) and a
/// linear scan over the 13-entry R-Set arrays; both are replaced here by
/// plain `const` arrays, which also removes all lazy initialisation.
///
/// Each entry is the index, or -1 for "not a member".
const fn index_table(chars: &[u8]) -> [i8; 256] {
    let mut table = [-1i8; 256];
    let mut i = 0usize;
    while i < chars.len() {
        table[chars[i] as usize] = i as i8;
        i += 1;
    }
    table
}

/// ASCII byte -> Alphabet-N digit value (0-84), or -1.
pub const ALPHABET_VALUE: [i8; 256] = index_table(ALPHABET_N);
/// ASCII byte -> R-Set index j (0-12), or -1.
const RSET_INDEX: [i8; 256] = index_table(&RSET_ASCII);
/// ASCII byte -> replacement-character index j (0-12), or -1.
const REPLACEMENT_INDEX: [i8; 256] = index_table(&REPLACEMENT_CHARS);

// Packed, byte-indexed tables for the three inner loops.
//
// Each loop classifies one byte per iteration, and each of the three tables
// below answers, in a single load, every question its loop needs to ask about
// that byte. The fields are packed rather than kept apart because the loads, not
// the arithmetic, are what the loops are made of: one indexed load and a mask
// beats two indexed loads and two branches.
//
// All three are derived from ALPHABET_N, RSET_ASCII and REPLACEMENT_CHARS by
// `const fn` at compile time, so there is no second copy of section 4 to keep in
// step, and no lazy initialisation to make the library's thread-safety
// conditional.

/// Pass 1's view of a byte (spec 6.1, step 1.a):
///
/// - bit 15: [`CLS_UNREPRESENTABLE`] -- neither in Alphabet-N nor in the R-Set,
///   so a representable run ends here.
/// - bits 0..12: `1 << j` if the byte is R-Set character j. This is exactly one
///   bit of the window mask, which lets Pass 1 accumulate the mask for a whole
///   block of bytes with a plain OR and no per-byte branch.
/// - bits 24..27: the same j as a small integer, for the one caller that needs
///   to index by it. OR-ing entries garbles this field, so it is only read from
///   a single unaccumulated lookup.
pub const CLS_UNREPRESENTABLE: u32 = 0x0000_8000;
pub const CLS_RSET_BITS: u32 = 0x0000_1FFF;
pub const CLS_INDEX_SHIFT: u32 = 24;

pub const ENC_CLASS: [u32; 256] = {
    let mut table = [CLS_UNREPRESENTABLE; 256];
    let mut b = 0usize;
    while b < 256 {
        if RSET_INDEX[b] >= 0 {
            let j = RSET_INDEX[b] as u32;
            table[b] = (1u32 << j) | (j << CLS_INDEX_SHIFT);
        } else if ALPHABET_VALUE[b] >= 0 {
            table[b] = 0;
        }
        b += 1;
    }
    table
};

/// Pass 2's view of a byte (spec 6.1, step 1.b):
///
/// - bits 0..7: the character the byte becomes in DP output -- its replacement
///   character if it is an R-Set byte (Case i), the byte itself otherwise
///   (Cases ii and iii).
/// - bits 16..31: the escape trigger. The byte needs a `~` prefix iff this field
///   intersects `window_mask | DP_ESCAPE_ALWAYS`: it is `1 << j` for replacement
///   character j, which triggers only while R-Set character j is in the window,
///   and [`DP_ESCAPE_ALWAYS`] for `~` itself, which is escaped unconditionally.
///
/// No byte is both an R-Set character and a replacement character, so the two
/// fields never have to describe the same byte at once.
pub const DP_ESCAPE_ALWAYS: u32 = 0x8000;

pub const DP_XLAT: [u32; 256] = {
    let mut table = [0u32; 256];
    let mut b = 0usize;
    while b < 256 {
        table[b] = if RSET_INDEX[b] >= 0 {
            REPLACEMENT_CHARS[RSET_INDEX[b] as usize] as u32
        } else if b as u8 == ESCAPE_CHAR {
            (DP_ESCAPE_ALWAYS << 16) | b as u32
        } else if REPLACEMENT_INDEX[b] >= 0 {
            (1u32 << (16 + REPLACEMENT_INDEX[b] as u32)) | b as u32
        } else {
            b as u32
        };
        b += 1;
    }
    table
};

/// The decoder's view of a character inside a DP segment (spec 7.2):
///
/// - bit 31: [`DEC_INVALID`] -- not a member of Alphabet-N.
/// - bit 30: [`DEC_ESCAPE`] -- the character is `~`.
/// - bits 16..28: `1 << j` if the character is replacement character j.
///   Intersect with the signal's 13-bit mask to decide whether this occurrence
///   stands for an R-Set byte or for itself.
/// - bits 0..7: the byte to emit when it does -- R-Set character j's ASCII
///   value, or the character itself when it is not a replacement character.
pub const DEC_INVALID: u32 = 0x8000_0000;
pub const DEC_ESCAPE: u32 = 0x4000_0000;

pub const DEC_SUB: [u32; 256] = {
    let mut table = [0u32; 256];
    let mut b = 0usize;
    while b < 256 {
        table[b] = if ALPHABET_VALUE[b] < 0 {
            DEC_INVALID | b as u32
        } else if b as u8 == ESCAPE_CHAR {
            DEC_ESCAPE | b as u32
        } else if REPLACEMENT_INDEX[b] >= 0 {
            let j = REPLACEMENT_INDEX[b] as u32;
            (1u32 << (16 + j)) | RSET_ASCII[j as usize] as u32
        } else {
            b as u32
        };
        b += 1;
    }
    table
};

/// Returns the Alphabet-N integer value (0-84) of `c`, or `None` if `c` is
/// not a member of Alphabet-N.
pub fn char_to_value(c: char) -> Option<u8> {
    if !c.is_ascii() {
        return None;
    }
    match ALPHABET_VALUE[c as usize] {
        -1 => None,
        v => Some(v as u8),
    }
}

/// Returns the Alphabet-N character for integer value `v` (0-84).
#[cfg(test)]
pub fn value_to_char(v: u8) -> char {
    ALPHABET_N[v as usize] as char
}

/// Returns the R-Set index `j` (0-12) for the given transformed-stream
/// byte, if `b` is one of the 13 `REPLACEMENT_CHARS`.
pub fn replacement_index_for_byte(b: u8) -> Option<u8> {
    match REPLACEMENT_INDEX[b as usize] {
        -1 => None,
        j => Some(j as u8),
    }
}
