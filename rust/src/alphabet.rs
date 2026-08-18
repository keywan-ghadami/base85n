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
pub const RANK_ABSENT_ALL: u64 = RANK_ABSENT_LANE * 0x0101_0101_0101_0101;

/// The class a byte falls in for the Dynamic Passthrough prefix scan, in one
/// numbering: [`DP_PLAIN`], an R-Set index, a donor slot, or [`DP_STOP`].
///
/// One numbering, because the scan tracks all of them in a single 64-bit
/// "already accounted for" set, and a class's bit in that set is exactly "this
/// byte changes nothing" -- true for a repeated R-Set character or donor, and
/// true from the start for [`DP_PLAIN`]. The two ends of the range fall out of
/// the same test: [`DP_PLAIN`] is 0, so its bit can be set before the scan
/// begins, and [`DP_STOP`] is 63, so it too is a well-defined shift, landing on
/// the one bit of the set that is never set.
///
/// A plain Alphabet-N character is one no profile spends as a donor. Its
/// [`RANK_PACKED`] entry is [`RANK_ABSENT_ALL`], which no minimum can be
/// lowered by, so it is exactly a character the scan can pass over.
pub const DP_CLASS: [u8; 256] = {
    let mut table = [DP_STOP; 256];
    let mut slot = 0u8;
    let mut b = 0usize;
    while b < 256 {
        if RSET_INDEX[b] >= 0 {
            table[b] = DP_RSET_BASE + RSET_INDEX[b] as u8;
        } else if ALPHABET_VALUE[b] >= 0 {
            let mut is_donor = false;
            let mut p = 0usize;
            while p < NUM_PROFILES {
                let mut r = 0usize;
                while r < RSET_LEN {
                    if PROFILES[p][r] as usize == b {
                        is_donor = true;
                    }
                    r += 1;
                }
                p += 1;
            }
            if is_donor {
                table[b] = DP_DONOR_BASE + slot;
                slot += 1;
            } else {
                table[b] = DP_PLAIN;
            }
        }
        b += 1;
    }
    table
};

/// [`DP_CLASS`] of a character no profile spends: the scan never has to
/// account for it, so its bit is set before the scan starts.
pub const DP_PLAIN: u8 = 0;

/// [`DP_CLASS`] of the first R-Set character; the 13 of them follow in R-Set
/// index order.
pub const DP_RSET_BASE: u8 = 1;

/// [`DP_CLASS`] of the first donor character; the rest follow in byte order.
pub const DP_DONOR_BASE: u8 = 1 + RSET_LEN as u8;

/// [`DP_CLASS`] of a byte no DP segment can carry. The one bit of the scan's
/// set that is never set, so that "not representable" falls out of the same
/// test as "already accounted for" without a branch of its own.
pub const DP_STOP: u8 = 63;

/// 1 for a byte a DP segment could carry -- Alphabet-N or R-Set -- and 0 for
/// one that ends any segment it appears in.
///
/// This is the encoder's lookahead table: one load answers the only question
/// [`crate::encode`]'s skip has to ask per byte.
pub const IS_REPRESENTABLE: [u8; 256] = {
    let mut table = [0u8; 256];
    let mut b = 0usize;
    while b < 256 {
        if ALPHABET_VALUE[b] >= 0 || RSET_INDEX[b] >= 0 {
            table[b] = 1;
        }
        b += 1;
    }
    table
};

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

#[cfg(test)]
mod tests {
    use super::*;

    /// The three assertions of spec section 12.1, in the order the spec makes
    /// them. Nothing here is checked anywhere else in the crate: `index_table`
    /// overwrites a duplicate without complaint, and [`DP_CLASS`] would file a
    /// byte that is in both sets under R-Set and carry on. Both would change
    /// what the encoder emits, and neither would fail a round trip.
    #[test]
    fn section_12_1_structural() {
        assert_eq!(ALPHABET_N.len(), 85);
        for (i, &c) in ALPHABET_N.iter().enumerate() {
            assert!(
                !ALPHABET_N[..i].contains(&c),
                "Alphabet-N repeats {:?}",
                c as char
            );
        }

        assert_eq!(RSET_ASCII.len(), RSET_LEN);
        for (i, &r) in RSET_ASCII.iter().enumerate() {
            assert!(!RSET_ASCII[..i].contains(&r), "the R-Set repeats {:?}", r as char);
            assert!(!ALPHABET_N.contains(&r), "{:?} is in both sets", r as char);
        }

        // The third -- eight profiles of thirteen distinct Alphabet-N
        // characters -- is `there_are_exactly_eight_profiles_of_thirteen_
        // distinct_donors` in `tests::edge_cases`, together with the round
        // trips that depend on it.
    }

    /// Every byte a Dynamic Passthrough segment can carry lies in
    /// `[0x09, 0x7E]`, and both ends are tight.
    ///
    /// Consumed by `lanes_within(word, 0x09, 0x7E)` in `encode`, which decides
    /// that no DP segment can begin inside a word whose lanes leave that range
    /// -- and lets the block-mode skip step over the positions in it. A
    /// representable byte outside the range would make the skip run past a
    /// decision point, and the encoder would emit output no other
    /// implementation produces, against spec section 6.5's rule 6.
    ///
    /// The range may be *wider* than the set, and is: 0x0B, 0x0C and 0x0E to
    /// 0x1F pass it without being representable. That costs the skip a walk it
    /// need not have made and nothing else.
    #[test]
    fn representable_bytes_lie_in_the_range_the_word_gate_tests() {
        let representable: Vec<u8> = (0..=255u8).filter(|&b| IS_REPRESENTABLE[b as usize] != 0).collect();
        assert_eq!(representable.len(), 98);
        assert_eq!(*representable.iter().min().unwrap(), 0x09);
        assert_eq!(*representable.iter().max().unwrap(), 0x7E);
        for &b in &representable {
            assert!((0x09..=0x7E).contains(&b), "{b:#04x} is outside the gate's range");
        }
    }

    /// Every representable byte is ASCII.
    ///
    /// Consumed three times over in `encode`: the substitution table is
    /// [`IDENTITY_ASCII`], 128 entries; the translation loop indexes it with
    /// `b & 0x7f`, which in a release build is the only thing keeping that
    /// index in range; and the scan's comment says as much.
    #[test]
    fn representable_bytes_are_ascii() {
        for b in 0..=255u8 {
            if IS_REPRESENTABLE[b as usize] != 0 {
                assert!(b < 128, "{b:#04x} is representable and not ASCII");
                assert_eq!(
                    b & 0x7f,
                    b,
                    "{b:#04x} would be translated as a different byte"
                );
            }
        }
        assert_eq!(IDENTITY_ASCII.len(), 128);
    }

    /// No byte the decoder ignores as whitespace is an Alphabet-N character.
    ///
    /// Consumed by `decode`, which does not strip whitespace up front: it
    /// decodes the input as it stands and only builds a filtered copy once that
    /// has failed. The reasoning is that a stream containing whitespace cannot
    /// decode successfully, which holds exactly while no whitespace byte is a
    /// character the first pass would accept as data.
    #[test]
    fn no_ignorable_whitespace_is_an_alphabet_n_character() {
        for &ws in b" \t\n\r" {
            assert!(
                !ALPHABET_N.contains(&ws),
                "{ws:#04x} is whitespace the decoder ignores and a character it would read"
            );
        }
        // Which is why the smallest one is well clear of them.
        assert_eq!(*ALPHABET_N.iter().min().unwrap(), 0x21);
    }
}
