//! Test suite for the `base85n` crate.
//!
//! - `vectors`: golden cross-language test vectors from
//!   `testvectors/vectors.json`.
//! - `roundtrip`: randomized round-trip property tests.
//! - `edge_cases`: explicit boundary-condition tests called out in the
//!   task description (partial blocks, MIN_PASSTHROUGH_BYTES boundary,
//!   multi-segment DP, escape runs, all byte values).
//! - `errors`: deliberately malformed input, asserting `decode` returns
//!   `Err` (never panics).

mod edge_cases;
mod errors;
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
