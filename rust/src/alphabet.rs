// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Alphabet-N, the R-Set, and the eight replacement alphabets defined in
//! spec sections 4 to 4.2.

/// The 85-character alphabet used by Base85N, in index order (0-84).
pub const ALPHABET_N: &[u8; 85] =
    b"0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ.-:+=^!/*?`_~()[]{}@%$#";

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

/// The number of replacement alphabets, and the range of the signal's 3-bit
/// alphabet identifier.
pub const NUM_ALPHABETS: usize = 8;

/// The eight replacement alphabets of spec section 4.2, each a list of
/// `(R-Set index, donor character)` substitutions.
///
/// Under alphabet `a`, `RSET_ASCII[j]` is written as its donor character, the
/// donor character itself becomes unrepresentable, and every other Alphabet-N
/// character represents itself. Each alphabet is therefore injective, which is
/// why this format needs no escape character.
///
/// The donors are the least frequent Alphabet-N characters measured over a
/// mixed corpus -- `^ @ % $ ? ! ~ #`, then `* + = _` and backtick -- except
/// where an alphabet's own target shape makes one of them common: alphabet 3
/// (markup) spends `{` rather than `#`, and alphabet 5 (code) spends the
/// backtick, which is rare in source but not in Markdown.
pub const REPLACEMENT_ALPHABETS: [&[(u8, u8)]; NUM_ALPHABETS] = [
    // 0 none
    &[],
    // 1 text
    &[(0, b'^'), (11, b'@'), (12, b'%'), (10, b'$')],
    // 2 prose
    &[(0, b'^'), (11, b'@'), (3, b'%'), (1, b'$'), (2, b'?'), (4, b'!')],
    // 3 markup
    &[
        (0, b'^'),
        (11, b'@'),
        (7, b'%'),
        (8, b'$'),
        (9, b'?'),
        (1, b'!'),
        (2, b'~'),
        (3, b'{'),
    ],
    // 4 json
    &[(0, b'^'), (11, b'@'), (1, b'%'), (3, b'$'), (5, b'?'), (12, b'!')],
    // 5 code
    &[
        (0, b'^'),
        (11, b'@'),
        (3, b'%'),
        (4, b'$'),
        (1, b'?'),
        (2, b'!'),
        (10, b'~'),
        (8, b'`'),
    ],
    // 6 shell
    &[
        (0, b'^'),
        (11, b'@'),
        (6, b'%'),
        (5, b'$'),
        (1, b'?'),
        (2, b'!'),
        (9, b'~'),
        (4, b'#'),
    ],
    // 7 full
    &[
        (0, b'^'),
        (11, b'@'),
        (12, b'%'),
        (10, b'$'),
        (3, b'?'),
        (4, b'!'),
        (1, b'~'),
        (2, b'#'),
        (7, b'*'),
        (8, b'+'),
        (9, b'='),
        (6, b'_'),
        (5, b'`'),
    ],
];

/// Byte-indexed lookup tables, evaluated at compile time.
///
/// Every table below is derived from `ALPHABET_N`, `RSET_ASCII` and
/// `REPLACEMENT_ALPHABETS` by `const fn`, so there is no second copy of
/// section 4 to keep in step and no lazy initialisation to make the library's
/// thread-safety conditional.
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

/// Which alphabets can represent a byte: bit `a` is set iff the byte is
/// representable under replacement alphabet `a` (spec section 6.1, step 1).
///
/// This is what lets the encoder settle all eight scans in one pass over the
/// input: it walks forward AND-ing this mask into a live set, and an alphabet's
/// run ends exactly at the position where its bit leaves that set.
pub const REPR: [u8; 256] = {
    let mut table = [0u8; 256];
    let mut b = 0usize;
    while b < 256 {
        let mut mask = 0u8;
        let mut a = 0usize;
        while a < NUM_ALPHABETS {
            let subs = REPLACEMENT_ALPHABETS[a];
            // An Alphabet-N character represents itself unless this alphabet
            // spends it as a donor; an R-Set character is representable only if
            // this alphabet substitutes it. No byte is both, so the two rules
            // never contend.
            let mut representable = ALPHABET_VALUE[b] >= 0;
            let mut k = 0usize;
            while k < subs.len() {
                let (j, donor) = subs[k];
                if b == donor as usize {
                    representable = false;
                }
                if b == RSET_ASCII[j as usize] as usize {
                    representable = true;
                }
                k += 1;
            }
            if representable {
                mask |= 1 << a;
            }
            a += 1;
        }
        table[b] = mask;
        b += 1;
    }
    table
};

/// `ENC_XLAT[a][b]` is the character byte `b` becomes in DP output under
/// alphabet `a`. Only meaningful where `REPR[b]` has bit `a` set.
pub const ENC_XLAT: [[u8; 256]; NUM_ALPHABETS] = {
    let mut tables = [[0u8; 256]; NUM_ALPHABETS];
    let mut a = 0usize;
    while a < NUM_ALPHABETS {
        let mut b = 0usize;
        while b < 256 {
            tables[a][b] = b as u8;
            b += 1;
        }
        let subs = REPLACEMENT_ALPHABETS[a];
        let mut k = 0usize;
        while k < subs.len() {
            let (j, donor) = subs[k];
            tables[a][RSET_ASCII[j as usize] as usize] = donor;
            k += 1;
        }
        a += 1;
    }
    tables
};

/// Set in [`DEC_XLAT`] for a character that is not a member of Alphabet-N.
pub const DEC_INVALID: u16 = 0x8000;

/// `DEC_XLAT[a][c]` is the byte character `c` stands for under alphabet `a`,
/// or [`DEC_INVALID`] if `c` is not in Alphabet-N.
///
/// One lookup answers both questions a decoder has about a character inside a
/// DP segment, and there is no state to carry between characters: version
/// 0.3.0 has no construct that spans two of them.
pub const DEC_XLAT: [[u16; 256]; NUM_ALPHABETS] = {
    let mut tables = [[0u16; 256]; NUM_ALPHABETS];
    let mut a = 0usize;
    while a < NUM_ALPHABETS {
        let mut c = 0usize;
        while c < 256 {
            tables[a][c] = if ALPHABET_VALUE[c] < 0 {
                DEC_INVALID | c as u16
            } else {
                c as u16
            };
            c += 1;
        }
        let subs = REPLACEMENT_ALPHABETS[a];
        let mut k = 0usize;
        while k < subs.len() {
            let (j, donor) = subs[k];
            tables[a][donor as usize] = RSET_ASCII[j as usize] as u16;
            k += 1;
        }
        a += 1;
    }
    tables
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
