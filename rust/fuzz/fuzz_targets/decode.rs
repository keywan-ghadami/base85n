// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The decoder against input it did not produce.
//!
//! Almost nothing a fuzzer generates is a valid stream, which is the point: the
//! decoder is the part that parses data the system did not produce, and spec
//! section 10 requires that malformed input be an error rather than a panic, a
//! read outside the buffer, or a process that ends. libFuzzer catches the
//! first; AddressSanitizer, which cargo-fuzz builds with, catches the second.
//!
//! Where the input *is* accepted, what it decodes to must survive a round trip
//! through the encoder. That is the property, and it is deliberately not the
//! stronger "re-encoding returns the input": a decoder accepts more than an
//! encoder emits, on purpose. Whitespace between tokens is ignored (spec 7.1),
//! so `"\n"` decodes to nothing and re-encodes to `""`. And every construct is
//! accepted wherever it appears, so a stream can spell a run as a solid Fill
//! where the encoder would have chosen the tail variant. Both decode to the
//! same bytes, which is what this checks.

#![no_main]

use libfuzzer_sys::fuzz_target;

fuzz_target!(|data: &[u8]| {
    let Ok(text) = std::str::from_utf8(data) else { return };
    if let Ok(bytes) = base85n::decode(text) {
        let canonical = base85n::encode(&bytes);
        let again = base85n::decode(&canonical)
            .expect("the encoder emitted a stream its own decoder rejects");
        assert_eq!(again, bytes, "re-encoding the decoded bytes changed them");
    }
});
