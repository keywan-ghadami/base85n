// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! # base85n
//!
//! A Rust implementation of Base85N, a binary-to-text encoding scheme with
//! a 4-byte-to-5-character Base85 core, an adaptive Dynamic Passthrough (DP)
//! mode for partially human-readable, 1:1-efficiency output on favorable
//! input, and a Solid Fill mode that carries a run of up to 2048 identical
//! bytes in five characters. See `spec/base85n-v0.4.0.md` in the repository
//! for the full specification.
//!
//! ```
//! let data = b"hello, world!";
//! let encoded = base85n::encode(data);
//! let decoded = base85n::decode(&encoded).unwrap();
//! assert_eq!(decoded, data);
//! ```

//! The crate also exports a C ABI (see [`ffi`]) so that callers in other
//! languages can link this implementation instead of a hand-written one;
//! `cargo build --release` produces `libbase85n.so`/`.dylib`/`.dll` and
//! `libbase85n.a` alongside the Rust library.

mod alphabet;
pub mod constants;
mod decode;
mod digits;
mod encode;
mod error;
pub mod ffi;

/// Alphabet-N, the R-Set and the donor profiles of spec section 4. They are
/// public because a binding layer -- the `python/` crate, say -- has to be able
/// to hand them to its own callers without a second copy of section 4.
pub use alphabet::{ALPHABET_N, PROFILES, RSET_ASCII};
pub use error::DecodeError;

/// Encode `data` as a Base85N string.
///
/// This never fails: any byte sequence, including the empty sequence, has
/// a valid Base85N encoding.
pub fn encode(data: &[u8]) -> String {
    encode::encode(data)
}

/// Encode `data` on up to `threads` threads, producing exactly what
/// [`encode`] produces.
///
/// Encoding is the slower direction, and it is the direction that
/// parallelises: signals are self-describing and segment boundaries are
/// decided by the data, so encoders started at different offsets converge on
/// the same decisions and their output can be spliced. There is no chunk-size
/// parameter in the format and no second canonical form -- `threads` changes
/// how the work is divided and nothing about the result. Spec section 11.3
/// describes the procedure and what bounds it.
///
/// Inputs below a couple of megabytes are encoded on the calling thread: the
/// seam repair costs more than the split saves.
pub fn encode_parallel(data: &[u8], threads: usize) -> String {
    encode::encode_parallel(data, threads)
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
