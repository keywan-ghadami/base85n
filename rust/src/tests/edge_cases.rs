// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Explicit boundary-condition tests.

use crate::alphabet::{ALPHABET_N, NUM_ALPHABETS, REPLACEMENT_ALPHABETS, RSET_ASCII};
use crate::constants::{MAX_DP_ANALYSIS_BYTES, MIN_PASSTHROUGH_BYTES};
use crate::{decode, encode};

fn roundtrip(data: &[u8]) {
    let encoded = encode(data);
    let decoded = decode(&encoded)
        .unwrap_or_else(|e| panic!("decode failed: {e:?} (encoded = {encoded:?})"));
    assert_eq!(decoded, data, "roundtrip mismatch; encoded = {encoded:?}");
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

/// Below `MIN_PASSTHROUGH_BYTES` (20): DP mode cannot be used, so the
/// whole prefix is emitted via plain block mode (a partial trailing group
/// is fine since this prefix is the entire, final buffer).
#[test]
fn below_min_passthrough_bytes_uses_block_mode() {
    let data: Vec<u8> = (0..19u8).map(|i| b'0' + (i % 10)).collect();
    assert_eq!(data.len(), 19);
    let encoded = encode(&data);
    // 4 full 4-byte blocks (20 chars) + 1 partial block of 3 leftover
    // bytes (4 chars) = 24 chars; DP mode is never even considered since
    // the MIN_PASSTHROUGH_BYTES gate fails outright.
    assert_eq!(encoded.len(), 24, "encoded = {encoded:?}");
    assert_eq!(decode(&encoded).unwrap(), data);
}

/// Exactly at `MIN_PASSTHROUGH_BYTES` (20): DP mode becomes eligible, and
/// for pure Alphabet-N literal content it ties with (and per spec 6.1
/// step 2.a's `<=` rule, wins over) block mode.
#[test]
fn exactly_min_passthrough_bytes_uses_dp_mode() {
    let data: Vec<u8> = (0..20u8).map(|i| b'a' + (i % 26)).collect();
    assert_eq!(data.len(), 20);
    let encoded = encode(&data);
    // 1 DP signal (5 chars) + 20 literal data chars = 25 chars.
    assert_eq!(encoded.len(), 25, "encoded = {encoded:?}");
    assert_eq!(decode(&encoded).unwrap(), data);
}

/// One byte above `MIN_PASSTHROUGH_BYTES`.
#[test]
fn one_above_min_passthrough_bytes_uses_dp_mode() {
    let data: Vec<u8> = (0..21u8).map(|i| b'a' + (i % 26)).collect();
    let encoded = encode(&data);
    // 1 DP signal (5 chars) + 21 literal data chars = 26 chars.
    assert_eq!(encoded.len(), 26, "encoded = {encoded:?}");
    assert_eq!(decode(&encoded).unwrap(), data);
}

/// `MAX_DP_ANALYSIS_BYTES` bounds a candidate prefix, so exactly 1024
/// representable bytes are one segment and one more needs a second.
#[test]
fn analysis_window_bounds_a_segment() {
    let exact = vec![b'x'; MAX_DP_ANALYSIS_BYTES];
    let encoded = encode(&exact);
    assert_eq!(encoded.len(), MAX_DP_ANALYSIS_BYTES + 5, "encoded len");
    assert_eq!(decode(&encoded).unwrap(), exact);

    let over = vec![b'x'; MAX_DP_ANALYSIS_BYTES + 1];
    let encoded = encode(&over);
    // 1024 characters plus a signal, then a single leftover byte in block
    // mode, which costs 2 characters.
    assert_eq!(encoded.len(), MAX_DP_ANALYSIS_BYTES + 5 + 2, "encoded len");
    assert_eq!(decode(&encoded).unwrap(), over);
}

/// A run several windows long is carried by several signals, each 1:1.
#[test]
fn long_run_uses_one_signal_per_window() {
    let data: Vec<u8> = (0..3000u32).map(|i| b'0' + (i % 10) as u8).collect();
    let encoded = encode(&data);
    // 2 full windows of 1024 plus a 952-byte remainder, each with a signal.
    assert_eq!(encoded.len(), 3000 + 3 * 5, "encoded = {}", encoded.len());
    assert_eq!(decode(&encoded).unwrap(), data);
}

/// Input containing every possible byte value 0-255.
#[test]
fn all_byte_values_present() {
    let data: Vec<u8> = (0u16..=255).map(|b| b as u8).collect();
    assert_eq!(data.len(), 256);
    roundtrip(&data);

    let mut data2 = Vec::new();
    for _ in 0..8 {
        data2.extend((0u16..=255).map(|b| b as u8));
    }
    roundtrip(&data2);
}

/// A DP prefix scan that starts with an unrepresentable byte (bytes
/// outside Alphabet-N's ASCII range) must yield an empty candidate
/// prefix, falling through to a raw `min(4, len)` block.
#[test]
fn unrepresentable_first_byte_falls_back_to_small_block() {
    let mut data = vec![0xFFu8, 0x00, 0x01, 0x02, 0x03];
    data.extend(vec![b'a'; 30]);
    roundtrip(&data);

    roundtrip(&[0xFFu8]);
    roundtrip(&[0xFFu8, 0xFE, 0xFD]);
}

/// Each alphabet has to carry the R-Set characters it substitutes.
#[test]
fn every_alphabet_round_trips_its_own_rset_characters() {
    for subs in REPLACEMENT_ALPHABETS.iter() {
        if subs.is_empty() {
            continue;
        }
        let mut data = Vec::new();
        while data.len() < 3 * MIN_PASSTHROUGH_BYTES {
            for &(j, _donor) in subs.iter() {
                data.push(RSET_ASCII[j as usize]);
                data.extend_from_slice(b"word");
            }
        }
        roundtrip(&data);
    }
}

/// A literal donor character is representable under any alphabet that does not
/// spend it. With a space in the run, the alphabets that could carry the space
/// all spend `^` on it, so the run has to break at a literal `^` instead of
/// mis-encoding it.
#[test]
fn literal_donor_characters_round_trip() {
    let donors: Vec<u8> = REPLACEMENT_ALPHABETS
        .iter()
        .flat_map(|subs| subs.iter().map(|&(_, d)| d))
        .collect();
    for donor in donors {
        let mut data = vec![b'a'; 25];
        data.push(b' ');
        data.push(donor);
        data.push(b' ');
        data.extend(vec![b'b'; 25]);
        roundtrip(&data);
    }
}

/// Only alphabet 7 substitutes all 13 R-Set characters, so a run containing
/// every one of them can only be carried by that alphabet -- and must be,
/// rather than falling back to block mode.
#[test]
fn all_rset_characters_at_once_is_one_dp_segment() {
    let mut data = Vec::new();
    for _ in 0..3 {
        data.extend_from_slice(&RSET_ASCII);
    }
    let encoded = encode(&data);
    assert_eq!(encoded.len(), data.len() + 5, "encoded = {encoded:?}");
    assert_eq!(decode(&encoded).unwrap(), data);
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
    ];
    for data in samples {
        let encoded = encode(&data);
        assert!(
            encoded.bytes().all(|c| ALPHABET_N.contains(&c)),
            "encoder emitted a character outside Alphabet-N: {encoded:?}"
        );
    }
}

/// The alphabet identifier field is 3 bits wide and every value is defined.
#[test]
fn there_are_exactly_eight_alphabets() {
    assert_eq!(REPLACEMENT_ALPHABETS.len(), NUM_ALPHABETS);
    assert_eq!(NUM_ALPHABETS, 8);
    for subs in REPLACEMENT_ALPHABETS.iter() {
        // No R-Set index and no donor character may repeat within an alphabet,
        // which is what makes it injective and escaping unnecessary.
        for (x, &(jx, dx)) in subs.iter().enumerate() {
            for &(jy, dy) in subs.iter().skip(x + 1) {
                assert_ne!(jx, jy, "duplicate R-Set index in an alphabet");
                assert_ne!(dx, dy, "duplicate donor character in an alphabet");
            }
            assert!(ALPHABET_N.contains(&dx), "donor outside Alphabet-N");
        }
    }
}
