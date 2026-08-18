// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Encoding is total and decoding undoes it, for any input at all.
//!
//! The property the whole format rests on, stated as the fuzzer can check it:
//! `encode` never fails, its output is Alphabet-N throughout, and `decode`
//! returns the bytes that went in.

#![no_main]

use libfuzzer_sys::fuzz_target;

fuzz_target!(|data: &[u8]| {
    let encoded = base85n::encode(data);
    assert!(encoded.is_ascii(), "the encoder emitted a non-ASCII character");
    let decoded = base85n::decode(&encoded).expect("the encoder emitted something it cannot read");
    assert_eq!(decoded, data, "round trip changed the bytes");
});
