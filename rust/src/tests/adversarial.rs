// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Adversarial decode vectors (`testvectors/adversarial_vectors.json`):
//! multi-byte Unicode input at various positions (character-position vs.
//! storage-unit discrepancies), 0-length DP signals, invalid/reserved DP
//! signals, and deliberately malformed escaping. See
//! `testvectors/adversarial_vectors.json` for the shared, cross-language
//! source of truth these are generated from.

use serde::Deserialize;

use super::hex_decode;
use crate::{decode, DecodeError};

#[derive(Deserialize)]
struct AdversarialVector {
    name: String,
    kind: String,
    input_hex: String,
    error_code: Option<String>,
    expected_hex: Option<String>,
}

fn load_adversarial_vectors() -> Vec<AdversarialVector> {
    let path = concat!(env!("CARGO_MANIFEST_DIR"), "/../testvectors/adversarial_vectors.json");
    let data = std::fs::read_to_string(path)
        .unwrap_or_else(|e| panic!("failed to read {}: {}", path, e));
    serde_json::from_str(&data).expect("valid adversarial_vectors.json")
}

/// Whether `err` belongs to the same error category as the shared
/// `error_code` string (one of the five conditions in spec Section
/// 10, shared verbatim across all five language implementations).
fn matches_error_code(err: &DecodeError, code: &str) -> bool {
    matches!(
        (err, code),
        (DecodeError::InvalidCharacter { .. }, "invalid_character")
            | (DecodeError::UnexpectedEndOfStream, "unexpected_end_of_stream")
            | (DecodeError::DanglingEscapeCharacter, "dangling_escape_character")
            | (DecodeError::ReservedSignalValue { .. }, "reserved_signal_value")
            | (
                DecodeError::InvalidPartialBlock { .. },
                "invalid_partial_block_length"
            )
    )
}

#[test]
fn adversarial_vectors() {
    let vectors = load_adversarial_vectors();
    assert!(vectors.len() >= 15, "expected a non-trivial adversarial vector set");

    for v in &vectors {
        let input_bytes = hex_decode(&v.input_hex);
        let input = std::str::from_utf8(&input_bytes)
            .unwrap_or_else(|e| panic!("{}: input_hex is not valid UTF-8: {}", v.name, e));

        match v.kind.as_str() {
            "must_fail" => {
                let code = v.error_code.as_deref().unwrap_or_else(|| {
                    panic!("{}: must_fail vector missing error_code", v.name)
                });
                match decode(input) {
                    Ok(bytes) => panic!(
                        "{}: expected decode() to fail with {:?}, but it succeeded with {:?}",
                        v.name, code, bytes
                    ),
                    Err(e) => assert!(
                        matches_error_code(&e, code),
                        "{}: expected error category {:?}, got {:?}",
                        v.name,
                        code,
                        e
                    ),
                }
            }
            "valid" => {
                let expected = hex_decode(
                    v.expected_hex
                        .as_deref()
                        .unwrap_or_else(|| panic!("{}: valid vector missing expected_hex", v.name)),
                );
                match decode(input) {
                    Ok(bytes) => assert_eq!(
                        bytes, expected,
                        "{}: decoded bytes did not match expected_hex",
                        v.name
                    ),
                    Err(e) => panic!("{}: expected decode() to succeed, got error {:?}", v.name, e),
                }
            }
            other => panic!("{}: unknown vector kind {:?}", v.name, other),
        }
    }
}
