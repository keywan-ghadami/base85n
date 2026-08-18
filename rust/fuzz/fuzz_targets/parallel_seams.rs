// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The parallel encoder's seams, at a chunk size a fuzzer can afford.
//!
//! `encode_parallel` splits the input, encodes the pieces speculatively and
//! joins them where the chains meet (spec section 11.3). Every such join is a
//! place where the output could differ from what one encoder produces, and at
//! the shipped chunk size of a megabyte a fuzz case would have to be megabytes
//! long to contain one. So the chunking is handed in here: sixteen bytes of
//! input can carry a seam, and a kilobyte carries dozens.
//!
//! The property is the one the parallel encoder promises: the same string as
//! the sequential encoder, for every input and every chunking.

#![no_main]

use libfuzzer_sys::fuzz_target;

fuzz_target!(|data: &[u8]| {
    if data.len() < 2 {
        return;
    }
    // The first byte chooses the chunking, so the fuzzer can steer it.
    let chunk = 1 + usize::from(data[0]) * 3;
    let body = &data[1..];

    let sequential = base85n::encode(body);
    let parallel = base85n::__fuzz_encode_parallel_chunked(body, chunk);
    assert_eq!(
        parallel, sequential,
        "chunk {chunk}: the seam changed the output"
    );
});
