//! # base85n
//!
//! A Rust implementation of Base85N, a binary-to-text encoding scheme with
//! a 4-byte-to-5-character Base85 core and an adaptive Dynamic Passthrough
//! (DP) mode for partially human-readable, near 1:1-efficiency output on
//! favorable input. See `README.md` and `NOTES.md` at the repository root
//! for the full specification and its clarifications.
//!
//! ```
//! let data = b"hello, world!";
//! let encoded = base85n::encode(data);
//! let decoded = base85n::decode(&encoded).unwrap();
//! assert_eq!(decoded, data);
//! ```

mod alphabet;
mod constants;
mod decode;
mod digits;
mod encode;
mod error;

pub use error::DecodeError;

/// Encode `data` as a Base85N string.
///
/// This never fails: any byte sequence, including the empty sequence, has
/// a valid Base85N encoding.
pub fn encode(data: &[u8]) -> String {
    encode::encode(data)
}

/// Decode a Base85N string `s` back into the original bytes.
///
/// # Errors
///
/// Returns a [`DecodeError`] if `s` is not a well-formed Base85N string;
/// see that type's variants for the specific error conditions detected.
pub fn decode(s: &str) -> Result<Vec<u8>, DecodeError> {
    decode::decode(s)
}

#[cfg(test)]
mod tests;
