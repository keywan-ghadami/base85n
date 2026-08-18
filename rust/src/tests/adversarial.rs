// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Adversarial decode vectors (`testvectors/adversarial_vectors.json`):
//! multi-byte Unicode input at various positions (character-position vs.
//! storage-unit discrepancies), signal-range boundaries, undefined signals,
//! Fill expansion bounds and non-canonical final blocks. See
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
    let data = super::read_vector_file("adversarial_vectors.json");
    serde_json::from_str(&data).expect("valid adversarial_vectors.json")
}

/// Whether `err` belongs to the same error category as the shared
/// `error_code` string (one of the conditions in spec Section 10, shared
/// verbatim across every language implementation).
fn matches_error_code(err: &DecodeError, code: &str) -> bool {
    matches!(
        (err, code),
        (DecodeError::InvalidCharacter { .. }, "invalid_character")
            | (DecodeError::UnexpectedEndOfStream, "unexpected_end_of_stream")
            | (DecodeError::UndefinedSignal { .. }, "undefined_signal")
            | (DecodeError::InvalidFinalBlock { .. }, "invalid_final_block")
    )
}

#[test]
fn adversarial_vectors() {
    let vectors = load_adversarial_vectors();
    assert!(vectors.len() >= 15, "expected a non-trivial adversarial vector set");

    for v in &vectors {
        // The vectors are byte-level, because the format is: a decoder's
        // input is whatever arrives on the wire, and much of what arrives is
        // not valid UTF-8. This crate's own `decode` takes a `&str`, so a
        // vector reaches it through the same byte-to-character mapping the C
        // ABI uses -- one character per byte, the identity on ASCII.
        //
        // This used to be `from_utf8` and to panic otherwise, which quietly
        // confined the shared vector set to inputs Rust could express. Every
        // vector written before this one is UTF-8-valid for that reason, not
        // by choice.
        let input_bytes = hex_decode(&v.input_hex);
        let owned: String = input_bytes.iter().map(|&b| b as char).collect();
        let input = owned.as_str();

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
