// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Test suite for the `base85n` crate.
//!
//! - `vectors`: golden cross-language test vectors from
//!   `testvectors/vectors.json`.
//! - `roundtrip`: randomized round-trip property tests.
//! - `edge_cases`: explicit boundary-condition tests (partial blocks, the
//!   MIN_PASSTHROUGH_BYTES and Fill thresholds, multi-segment DP, donor
//!   literals, all byte values).
//! - `errors`: deliberately malformed input, asserting `decode` returns
//!   `Err` (never panics).
//! - `complexity`: guards the linear-time encoding requirement of spec
//!   Section 6.6.
//! - `parallel`: the multi-threaded encoder produces the sequential
//!   encoder's output, at every thread count and seam position (spec
//!   Section 11.3).

mod adversarial;
mod complexity;
mod edge_cases;
mod errors;
mod parallel;
mod roundtrip;
mod vectors;

use serde::Deserialize;

#[derive(Deserialize)]
pub(crate) struct Vector {
    pub name: String,
    pub input_hex: String,
    pub output: String,
}

/// Decode a hex string (as found in `testvectors/vectors.json`) into bytes.
pub(crate) fn hex_decode(s: &str) -> Vec<u8> {
    assert_eq!(s.len() % 2, 0, "hex string must have even length");
    (0..s.len())
        .step_by(2)
        .map(|i| u8::from_str_radix(&s[i..i + 2], 16).expect("valid hex byte"))
        .collect()
}

/// Load and parse `testvectors/vectors.json` from the repository root.
pub(crate) fn load_vectors() -> Vec<Vector> {
    let path = concat!(env!("CARGO_MANIFEST_DIR"), "/../testvectors/vectors.json");
    let data = std::fs::read_to_string(path)
        .unwrap_or_else(|e| panic!("failed to read {}: {}", path, e));
    serde_json::from_str(&data).expect("valid vectors.json")
}

#[test]
fn hex_decode_roundtrips() {
    assert_eq!(hex_decode(""), Vec::<u8>::new());
    assert_eq!(hex_decode("61626330"), b"abc0");
}
