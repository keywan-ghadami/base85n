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
const ALPHABET_VALUE: [i8; 256] = index_table(ALPHABET_N);
/// ASCII byte -> R-Set index j (0-12), or -1.
const RSET_INDEX: [i8; 256] = index_table(&RSET_ASCII);
/// ASCII byte -> replacement-character index j (0-12), or -1.
const REPLACEMENT_INDEX: [i8; 256] = index_table(&REPLACEMENT_CHARS);

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

/// Returns `true` if `b` (interpreted as an ASCII byte) is a member of
/// Alphabet-N.
pub fn is_alphabet_n_byte(b: u8) -> bool {
    ALPHABET_VALUE[b as usize] >= 0
}

/// Returns the Alphabet-N character for integer value `v` (0-84).
pub fn value_to_char(v: u8) -> char {
    ALPHABET_N[v as usize] as char
}

/// Returns the R-Set index `j` (0-12) for the given original-data byte, if
/// `b` is one of the 13 R-Set characters.
pub fn rset_index_for_byte(b: u8) -> Option<u8> {
    match RSET_INDEX[b as usize] {
        -1 => None,
        j => Some(j as u8),
    }
}

/// Returns the R-Set index `j` (0-12) for the given transformed-stream
/// byte, if `b` is one of the 13 `REPLACEMENT_CHARS`.
pub fn replacement_index_for_byte(b: u8) -> Option<u8> {
    match REPLACEMENT_INDEX[b as usize] {
        -1 => None,
        j => Some(j as u8),
    }
}
