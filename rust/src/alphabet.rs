// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Alphabet-N, the R-Set, and the eight donor profiles defined in spec
//! sections 4 to 4.3.
//!
//! Version 0.4.0 replaced the eight fixed replacement alphabets of 0.3.x with
//! a per-segment construction: the signal names which R-Set characters occur
//! (`mask`) and which donor ranking to spend on them (`profile`), and the
//! substitution is derived from the two. A profile is *not* an alphabet -- it
//! is an ordered list of 13 donors, of which a segment consumes only the first
//! `popcount(mask)`.

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

/// The size of the R-Set, and the width of the signal's `mask` field.
pub const RSET_LEN: usize = 13;

/// The number of donor profiles, and the range of the signal's 3-bit profile
/// identifier.
pub const NUM_PROFILES: usize = 8;

/// The eight donor profiles of spec section 4.2: each an ordered sequence of
/// 13 distinct Alphabet-N characters.
///
/// A segment whose mask has `k` bits set spends the profile's first `k`
/// characters, in mask-bit order, as the stand-ins for the R-Set characters
/// that occur in it. Only those first `k` become unrepresentable; the rest of
/// the profile is still ordinary data, which is why a profile is a ranking and
/// not an alphabet.
pub const PROFILES: [[u8; RSET_LEN]; NUM_PROFILES] = [
    *b"~^?%@+`$#!*.-",
    *b"~^+[]`?@!%#*(",
    *b"^~$#?%!`@[]+_",
    *b"~+?%@!^[]:`()",
    *b"~%^`+?!$@(){}",
    *b"^~?@!+%*$()_#",
    *b"^~@%?$+!#[]=*",
    *b"^$~@?!%`[]:}{",
];

/// Byte-indexed lookup tables, evaluated at compile time.
///
/// Every table below is derived from `ALPHABET_N`, `RSET_ASCII` and `PROFILES`
/// by `const fn`, so there is no second copy of section 4 to keep in step and
/// no lazy initialisation to make the library's thread-safety conditional.
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
pub const RSET_INDEX: [i8; 256] = index_table(&RSET_ASCII);

/// The rank a byte would occupy in each profile, packed one profile per byte
/// lane of a `u64`, lane `p` holding profile `p`'s rank.
///
/// A character absent from a profile ranks [`RANK_ABSENT_LANE`] there, which is
/// one past the last real rank, so "absent" and "ranked below every possible
/// `k`" are the same value and the scan needs no special case for it.
///
/// Non-representable bytes -- neither Alphabet-N nor R-Set -- are marked with
/// [`NOT_REPRESENTABLE`] instead, a value the lane arithmetic below would
/// misread, and which the scan therefore tests for before using the entry.
pub const RANK_PACKED: [u64; 256] = {
    let mut table = [NOT_REPRESENTABLE; 256];
    let mut b = 0usize;
    while b < 256 {
        if ALPHABET_VALUE[b] >= 0 || RSET_INDEX[b] >= 0 {
            let mut packed = 0u64;
            let mut p = 0usize;
            while p < NUM_PROFILES {
                let mut rank = RANK_ABSENT_LANE;
                let mut r = 0usize;
                while r < RSET_LEN {
                    if PROFILES[p][r] as usize == b {
                        rank = r as u64;
                    }
                    r += 1;
                }
                packed |= rank << (8 * p);
                p += 1;
            }
            table[b] = packed;
        }
        b += 1;
    }
    table
};

/// The rank of a character that does not appear in a profile at all: 13, one
/// past the last real rank, so no `k` in 0..=13 can reach it.
pub const RANK_ABSENT_LANE: u64 = RSET_LEN as u64;

/// [`RANK_PACKED`] entry for a byte that cannot appear in a DP segment under
/// any mask or profile. Distinct from every real packing, since real ranks
/// never exceed 13.
pub const NOT_REPRESENTABLE: u64 = u64::MAX;

/// [`RANK_ABSENT_LANE`] in all eight lanes: the scan's starting state, before
/// any literal character has constrained the choice of profile.
pub const RANK_ABSENT_ALL: u64 = 0x0d0d_0d0d_0d0d_0d0d;

/// Identity table over the ASCII range, the base every per-segment encoding
/// table is patched into. Every representable byte is ASCII, so 128 entries
/// suffice.
pub const IDENTITY_ASCII: [u8; 128] = {
    let mut table = [0u8; 128];
    let mut b = 0usize;
    while b < 128 {
        table[b] = b as u8;
        b += 1;
    }
    table
};

/// Set in [`DEC_BASE`] for a character that is not a member of Alphabet-N.
pub const DEC_INVALID: u16 = 0x8000;

/// The decoding table for a DP segment before its donors are patched in: an
/// Alphabet-N character stands for its own byte value, anything else is
/// invalid. One lookup then answers both questions a decoder has about a
/// character inside a segment.
pub const DEC_BASE: [u16; 256] = {
    let mut table = [0u16; 256];
    let mut c = 0usize;
    while c < 256 {
        table[c] = if ALPHABET_VALUE[c] < 0 {
            DEC_INVALID | c as u16
        } else {
            c as u16
        };
        c += 1;
    }
    table
};

/// The `k` donor characters a segment with this `mask` spends under this
/// `profile`, in R-Set index order, together with `k` itself.
///
/// Spec section 4.3: the set bits of `mask` consume the first `k` characters
/// of the profile, the lowest bit taking rank 0.
#[inline]
pub fn donors(profile: usize, mask: u16) -> ([(u8, u8); RSET_LEN], usize) {
    let mut pairs = [(0u8, 0u8); RSET_LEN];
    let mut rank = 0usize;
    for (j, &rset) in RSET_ASCII.iter().enumerate() {
        if mask & (1 << j) != 0 {
            pairs[rank] = (rset, PROFILES[profile][rank]);
            rank += 1;
        }
    }
    (pairs, rank)
}

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
