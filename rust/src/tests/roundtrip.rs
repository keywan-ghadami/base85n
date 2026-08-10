// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Randomized round-trip property tests. All randomness is derived from a
//! fixed seed so runs are deterministic and reproducible.

use crate::{decode, encode};
use rand::rngs::StdRng;
use rand::{Rng, SeedableRng};

use crate::alphabet::{ALPHABET_N, ESCAPE_CHAR, RSET_ASCII};

/// Generate one random byte drawn from a mix of "interesting" pools:
/// arbitrary bytes, Alphabet-N literals, R-Set characters, and the escape
/// character, weighted so all pools get meaningful coverage.
fn random_byte(rng: &mut StdRng) -> u8 {
    match rng.gen_range(0..100) {
        0..=39 => rng.gen::<u8>(), // arbitrary byte, full 0-255 range
        40..=74 => {
            // Alphabet-N literal byte.
            let idx = rng.gen_range(0..ALPHABET_N.len());
            ALPHABET_N[idx]
        }
        75..=94 => {
            // R-Set character.
            let idx = rng.gen_range(0..RSET_ASCII.len());
            RSET_ASCII[idx]
        }
        _ => ESCAPE_CHAR, // 95..=99: escape character '~'
    }
}

fn random_bytes(rng: &mut StdRng, len: usize) -> Vec<u8> {
    (0..len).map(|_| random_byte(rng)).collect()
}

fn assert_roundtrip(data: &[u8], context: &str) {
    let encoded = encode(data);
    match decode(&encoded) {
        Ok(decoded) => assert_eq!(
            decoded, data,
            "roundtrip mismatch for {} (len={}); encoded = {:?}",
            context,
            data.len(),
            encoded
        ),
        Err(e) => panic!(
            "decode failed for {} (len={}): {:?}; encoded = {:?}",
            context,
            data.len(),
            e,
            encoded
        ),
    }
}

#[test]
fn roundtrip_random_mixed_content_varied_lengths() {
    let mut rng = StdRng::seed_from_u64(0xB05E_5EED_u64);

    let lengths: Vec<usize> = {
        let mut v = vec![0, 1, 2, 3, 4, 5, 6, 7, 8, 16, 19, 20, 21, 63, 64, 65, 100, 255, 256, 511, 512, 513, 1000, 2000];
        for _ in 0..80 {
            v.push(rng.gen_range(0..4000));
        }
        v
    };

    for (i, &len) in lengths.iter().enumerate() {
        let data = random_bytes(&mut rng, len);
        assert_roundtrip(&data, &format!("mixed-content case #{i} (len {len})"));
    }
}

#[test]
fn roundtrip_pure_random_bytes() {
    let mut rng = StdRng::seed_from_u64(42);
    for i in 0..60 {
        let len = rng.gen_range(0..3000);
        let data: Vec<u8> = (0..len).map(|_| rng.gen::<u8>()).collect();
        assert_roundtrip(&data, &format!("pure-random case #{i}"));
    }
}

#[test]
fn roundtrip_pure_alphabet_n_literals() {
    let mut rng = StdRng::seed_from_u64(7);
    for i in 0..30 {
        let len = rng.gen_range(0..2000);
        let data: Vec<u8> = (0..len).map(|_| ALPHABET_N[rng.gen_range(0..ALPHABET_N.len())]).collect();
        assert_roundtrip(&data, &format!("pure-alphabet case #{i}"));
    }
}

#[test]
fn roundtrip_rset_heavy() {
    let mut rng = StdRng::seed_from_u64(99);
    for i in 0..30 {
        let len = rng.gen_range(20..1500);
        let data: Vec<u8> = (0..len)
            .map(|_| {
                if rng.gen_bool(0.7) {
                    RSET_ASCII[rng.gen_range(0..RSET_ASCII.len())]
                } else {
                    ALPHABET_N[rng.gen_range(0..ALPHABET_N.len())]
                }
            })
            .collect();
        assert_roundtrip(&data, &format!("rset-heavy case #{i}"));
    }
}

#[test]
fn roundtrip_escape_heavy() {
    let mut rng = StdRng::seed_from_u64(123);
    for i in 0..30 {
        let len = rng.gen_range(20..1500);
        let data: Vec<u8> = (0..len)
            .map(|_| if rng.gen_bool(0.6) { ESCAPE_CHAR } else { rng.gen::<u8>() })
            .collect();
        assert_roundtrip(&data, &format!("escape-heavy case #{i}"));
    }
}

#[test]
fn roundtrip_single_bytes_all_values() {
    for b in 0u16..=255 {
        let data = [b as u8];
        assert_roundtrip(&data, &format!("single byte {b:#04x}"));
    }
}
