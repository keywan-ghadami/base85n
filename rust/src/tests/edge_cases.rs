// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Explicit boundary-condition tests.

use crate::alphabet::{ALPHABET_N, NUM_PROFILES, PROFILES, RSET_ASCII, RSET_LEN};
use crate::constants::{
    MAX_DP_ANALYSIS_BYTES, MAX_FILL_BYTES, MIN_FILL_BYTES, MIN_PASSTHROUGH_BYTES,
};
use crate::{decode, encode};

fn roundtrip(data: &[u8]) {
    let encoded = encode(data);
    let decoded = decode(&encoded)
        .unwrap_or_else(|e| panic!("decode failed: {e:?} (encoded = {encoded:?})"));
    assert_eq!(decoded, data, "roundtrip mismatch; encoded = {encoded:?}");
}

/// Text with no run of identical bytes in it, so Fill never fires and the
/// length arithmetic below is about DP alone.
fn varied(len: usize) -> Vec<u8> {
    (0..len).map(|i| b'a' + (i % 26) as u8).collect()
}

#[test]
fn empty_input() {
    assert_eq!(encode(&[]), "");
    assert_eq!(decode("").unwrap(), Vec::<u8>::new());
}

#[test]
fn lengths_one_through_four() {
    for len in 1..=4usize {
        let data: Vec<u8> = (0..len as u8).map(|i| b'a' + i).collect();
        roundtrip(&data);
    }
}

/// Below `MIN_PASSTHROUGH_BYTES` (20): DP mode cannot be used, so the whole
/// prefix is emitted via plain block mode (a partial trailing group is fine
/// since this prefix is the entire, final buffer).
#[test]
fn below_min_passthrough_bytes_uses_block_mode() {
    let data = varied(19);
    let encoded = encode(&data);
    // 4 full 4-byte blocks (20 chars) + 1 partial block of 3 leftover bytes
    // (4 chars) = 24 chars; the MIN_PASSTHROUGH_BYTES gate fails outright.
    assert_eq!(encoded.len(), 24, "encoded = {encoded:?}");
    assert_eq!(decode(&encoded).unwrap(), data);
}

/// Exactly at `MIN_PASSTHROUGH_BYTES` (20): DP becomes eligible, and for pure
/// Alphabet-N content it ties with block mode -- which spec section 6.1
/// step 3's `<=` resolves in DP's favour.
#[test]
fn exactly_min_passthrough_bytes_uses_dp_mode() {
    let data = varied(MIN_PASSTHROUGH_BYTES);
    let encoded = encode(&data);
    assert_eq!(encoded.len(), 5 + MIN_PASSTHROUGH_BYTES, "encoded = {encoded:?}");
    assert_eq!(decode(&encoded).unwrap(), data);
}

/// One byte above `MIN_PASSTHROUGH_BYTES`.
#[test]
fn one_above_min_passthrough_bytes_uses_dp_mode() {
    let data = varied(21);
    let encoded = encode(&data);
    assert_eq!(encoded.len(), 26, "encoded = {encoded:?}");
    assert_eq!(decode(&encoded).unwrap(), data);
}

/// `MAX_DP_ANALYSIS_BYTES` bounds a candidate prefix, so exactly 2048
/// representable bytes are one segment and one more needs a second.
#[test]
fn analysis_window_bounds_a_segment() {
    let exact = varied(MAX_DP_ANALYSIS_BYTES);
    let encoded = encode(&exact);
    assert_eq!(encoded.len(), MAX_DP_ANALYSIS_BYTES + 5, "encoded len");
    assert_eq!(decode(&encoded).unwrap(), exact);

    let over = varied(MAX_DP_ANALYSIS_BYTES + 1);
    let encoded = encode(&over);
    // A full window plus its signal, then a single leftover byte in block
    // mode, which costs 2 characters.
    assert_eq!(encoded.len(), MAX_DP_ANALYSIS_BYTES + 5 + 2, "encoded len");
    assert_eq!(decode(&encoded).unwrap(), over);
}

/// A run several windows long is carried by several signals, each 1:1.
#[test]
fn long_run_uses_one_signal_per_window() {
    let data = varied(3 * MAX_DP_ANALYSIS_BYTES + 100);
    let encoded = encode(&data);
    assert_eq!(encoded.len(), data.len() + 4 * 5, "encoded = {}", encoded.len());
    assert_eq!(decode(&encoded).unwrap(), data);
}

/// Section 6.1 step 1: a run of identical bytes is a Fill signal from
/// `MIN_FILL_BYTES` up, whatever the byte is.
#[test]
fn fill_threshold_and_cap() {
    for byte in [0u8, b' ', b'a', 0xff] {
        // One below the threshold: block mode, and no signal.
        let short = vec![byte; MIN_FILL_BYTES - 1];
        let encoded = encode(&short);
        assert_eq!(encoded.len(), 5, "{byte:#04x}: {encoded:?}");
        assert_eq!(decode(&encoded).unwrap(), short);

        // At the threshold: one signal and nothing else.
        let at = vec![byte; MIN_FILL_BYTES];
        let encoded = encode(&at);
        assert_eq!(encoded.len(), 5, "{byte:#04x}: {encoded:?}");
        assert_eq!(decode(&encoded).unwrap(), at);

        // At the cap: still one signal.
        let cap = vec![byte; MAX_FILL_BYTES];
        let encoded = encode(&cap);
        assert_eq!(encoded.len(), 5, "{byte:#04x}: {encoded:?}");
        assert_eq!(decode(&encoded).unwrap(), cap);

        // One past it: a second signal takes over, since a run of one is
        // below the Fill threshold -- 1 byte costs 2 characters in block mode.
        let over = vec![byte; MAX_FILL_BYTES + 1];
        let encoded = encode(&over);
        assert_eq!(encoded.len(), 7, "{byte:#04x}: {encoded:?}");
        assert_eq!(decode(&encoded).unwrap(), over);

        // Two full runs.
        let two = vec![byte; 2 * MAX_FILL_BYTES];
        assert_eq!(encode(&two).len(), 10);
        assert_eq!(decode(&encode(&two)).unwrap(), two);
    }
}

/// Fill is checked before DP at every iteration, so a run inside otherwise
/// passthrough-able text ends the segment before it and starts a new one after.
#[test]
fn fill_interrupts_text() {
    let mut data = Vec::new();
    data.extend_from_slice(&varied(40));
    data.extend(std::iter::repeat_n(b'=', 300));
    data.extend_from_slice(&varied(40));
    let encoded = encode(&data);
    // 5 + 40 for the first segment, 5 for the run, 5 + 40 for the second.
    assert_eq!(encoded.len(), 5 + 40 + 5 + 5 + 40, "encoded = {encoded:?}");
    assert_eq!(decode(&encoded).unwrap(), data);
}

/// Transitions in both directions, including runs of bytes that DP could not
/// have carried anyway.
#[test]
fn fill_dp_and_block_transitions() {
    let mut data = Vec::new();
    data.extend(std::iter::repeat_n(0u8, 64));
    data.extend_from_slice(b"{\"name\": \"Ada Lovelace\", \"id\": 42}");
    data.extend(std::iter::repeat_n(0xffu8, 7));
    data.extend_from_slice(&[0x89, 0x50, 0x4e, 0x47, 0x0d, 0x1a, 0x0a]);
    data.extend(std::iter::repeat_n(b' ', 2050));
    data.extend_from_slice(b"trailing text that is long enough for a segment");
    roundtrip(&data);
}

/// Input containing every possible byte value 0-255.
#[test]
fn all_byte_values_present() {
    let data: Vec<u8> = (0u16..=255).map(|b| b as u8).collect();
    roundtrip(&data);

    let mut data2 = Vec::new();
    for _ in 0..8 {
        data2.extend((0u16..=255).map(|b| b as u8));
    }
    roundtrip(&data2);
}

/// A DP prefix scan that starts with an unrepresentable byte must yield an
/// empty candidate prefix, falling through to a raw `min(4, len)` block.
#[test]
fn unrepresentable_first_byte_falls_back_to_small_block() {
    let mut data = vec![0xFFu8, 0x00, 0x01, 0x02, 0x03];
    data.extend(varied(30));
    roundtrip(&data);

    roundtrip(&[0xFFu8]);
    roundtrip(&[0xFFu8, 0xFE, 0xFD]);
}

/// Every R-Set character has to survive a segment that names it.
#[test]
fn every_rset_character_round_trips() {
    for &r in RSET_ASCII.iter() {
        let mut data = Vec::new();
        while data.len() < 3 * MIN_PASSTHROUGH_BYTES {
            data.extend_from_slice(b"word");
            data.push(r);
        }
        roundtrip(&data);
    }
}

/// A literal donor character is representable while the segment does not spend
/// it. Pairing each with a space -- which every profile pays for with its rank-0
/// donor -- forces the scan either to pick a profile that ranks the literal
/// beyond `k`, or to break the segment.
#[test]
fn literal_donor_characters_round_trip() {
    for donor in all_donors() {
        let mut data = varied(25);
        data.push(b' ');
        data.push(donor);
        data.push(b' ');
        data.extend(varied(25));
        roundtrip(&data);
    }
}

/// All 13 R-Set characters at once: `k` reaches 13, so the whole of some
/// profile is spent, and the segment can hold no literal from it.
#[test]
fn all_rset_characters_at_once_is_one_dp_segment() {
    let mut data = Vec::new();
    for _ in 0..3 {
        data.extend_from_slice(&RSET_ASCII);
    }
    let encoded = encode(&data);
    assert_eq!(encoded.len(), data.len() + 5, "encoded = {encoded:?}");
    assert_eq!(decode(&encoded).unwrap(), data);

    // The same, with a literal that profile 0 ranks last (`-`, rank 12) and
    // which therefore cannot survive alongside all 13 substitutions: the
    // segment has to break rather than mis-encode it.
    let mut mixed = data.clone();
    mixed.push(b'-');
    mixed.extend_from_slice(&RSET_ASCII);
    roundtrip(&mixed);
}

/// Whatever the input, the encoder emits Alphabet-N and nothing else.
#[test]
fn output_is_always_alphabet_n() {
    let samples: Vec<Vec<u8>> = vec![
        (0u16..=255).map(|b| b as u8).collect(),
        b"Hello, World!\r\n\t".repeat(10),
        br#"{"a": "b", "c": [1, 2]}"#.repeat(5),
        b"<p>x &amp; y</p>".repeat(5),
        RSET_ASCII.repeat(5),
        ALPHABET_N.repeat(4),
        vec![0u8; 5000],
    ];
    for data in samples {
        let encoded = encode(&data);
        assert!(
            encoded.bytes().all(|c| ALPHABET_N.contains(&c)),
            "encoder emitted a character outside Alphabet-N: {encoded:?}"
        );
    }
}

/// Every character any profile can spend as a donor.
pub(crate) fn all_donors() -> Vec<u8> {
    let mut v: Vec<u8> = PROFILES.iter().flat_map(|p| p.iter().copied()).collect();
    v.sort_unstable();
    v.dedup();
    v
}

/// The profile identifier field is 3 bits wide and every value is defined.
#[test]
fn there_are_exactly_eight_profiles_of_thirteen_distinct_donors() {
    assert_eq!(PROFILES.len(), NUM_PROFILES);
    assert_eq!(NUM_PROFILES, 8);
    assert_eq!(RSET_ASCII.len(), RSET_LEN);
    for profile in PROFILES.iter() {
        assert_eq!(profile.len(), RSET_LEN);
        for (i, &c) in profile.iter().enumerate() {
            assert!(ALPHABET_N.contains(&c), "donor outside Alphabet-N");
            assert!(!profile[..i].contains(&c), "duplicate donor in a profile");
        }
    }
    // The R-Set and Alphabet-N are disjoint (spec section 4.1).
    for &r in RSET_ASCII.iter() {
        assert!(!ALPHABET_N.contains(&r), "R-Set character in Alphabet-N");
    }
}
