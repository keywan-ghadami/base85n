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

/// A lookup table mapping every possible byte value (0-255) to its
/// Alphabet-N integer value (0-84), or `None` if the byte's ASCII
/// representation is not part of Alphabet-N. Built once, indexed by `u8`.
struct AlphabetLut([Option<u8>; 256]);

fn build_lut() -> AlphabetLut {
    let mut table = [None; 256];
    let mut i = 0usize;
    while i < ALPHABET_N.len() {
        table[ALPHABET_N[i] as usize] = Some(i as u8);
        i += 1;
    }
    AlphabetLut(table)
}

thread_local! {
    static LUT: AlphabetLut = build_lut();
}

/// Returns the Alphabet-N integer value (0-84) of `c`, or `None` if `c` is
/// not a member of Alphabet-N.
pub fn char_to_value(c: char) -> Option<u8> {
    if !c.is_ascii() {
        return None;
    }
    LUT.with(|lut| lut.0[c as usize])
}

/// Returns `true` if `b` (interpreted as an ASCII byte) is a member of
/// Alphabet-N.
pub fn is_alphabet_n_byte(b: u8) -> bool {
    LUT.with(|lut| lut.0[b as usize].is_some())
}

/// Returns the Alphabet-N character for integer value `v` (0-84).
pub fn value_to_char(v: u8) -> char {
    ALPHABET_N[v as usize] as char
}

/// Returns the R-Set index `j` (0-12) for the given original-data byte, if
/// `b` is one of the 13 R-Set characters.
pub fn rset_index_for_byte(b: u8) -> Option<u8> {
    RSET_ASCII.iter().position(|&r| r == b).map(|i| i as u8)
}

/// Returns the R-Set index `j` (0-12) for the given transformed-stream
/// byte, if `b` is one of the 13 `REPLACEMENT_CHARS`.
pub fn replacement_index_for_byte(b: u8) -> Option<u8> {
    REPLACEMENT_CHARS.iter().position(|&r| r == b).map(|i| i as u8)
}
