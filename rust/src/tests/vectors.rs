//! Golden cross-language test vectors (`testvectors/vectors.json`).

use super::{hex_decode, load_vectors};
use crate::{decode, encode};

#[test]
fn golden_vectors_encode_and_decode() {
    let vectors = load_vectors();
    assert!(vectors.len() >= 40, "expected a substantial vector set, got {}", vectors.len());

    let mut failures = Vec::new();
    for v in &vectors {
        let input = hex_decode(&v.input_hex);

        let got_encoded = encode(&input);
        if got_encoded != v.output {
            failures.push(format!(
                "[{}] encode mismatch:\n  input_hex = {}\n  expected  = {:?}\n  got       = {:?}",
                v.name, v.input_hex, v.output, got_encoded
            ));
        }

        match decode(&v.output) {
            Ok(got_decoded) => {
                if got_decoded != input {
                    failures.push(format!(
                        "[{}] decode mismatch:\n  output     = {:?}\n  expected   = {}\n  got hex    = {}",
                        v.name,
                        v.output,
                        v.input_hex,
                        got_decoded.iter().map(|b| format!("{:02x}", b)).collect::<String>()
                    ));
                }
            }
            Err(e) => {
                failures.push(format!("[{}] decode errored: {:?} (output = {:?})", v.name, e, v.output));
            }
        }
    }

    assert!(
        failures.is_empty(),
        "{} of {} golden vectors failed:\n{}",
        failures.len(),
        vectors.len(),
        failures.join("\n\n")
    );
}
