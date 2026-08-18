// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The parallel encoder produces the sequential encoder's output, or it is not
//! an encoder for this format (spec section 11.3).
//!
//! Every case here compares against [`encode`] rather than against a fixture:
//! the property being tested is equality with the one canonical form, at every
//! thread count and every seam position.

use crate::encode::MIN_PARALLEL_CHUNK;
use crate::{decode, encode, encode_parallel};

/// A deterministic byte source, so a failure is reproducible.
struct Rng(u64);

impl Rng {
    fn next_u32(&mut self) -> u32 {
        self.0 ^= self.0 << 13;
        self.0 ^= self.0 >> 7;
        self.0 ^= self.0 << 17;
        (self.0 >> 32) as u32
    }
}

/// Input built out of the shapes the seam logic has to survive: zero runs
/// straddling chunk boundaries, other runs, passthrough text, and binary that
/// no construct matches.
fn mixed(len: usize, seed: u64) -> Vec<u8> {
    let mut rng = Rng(seed);
    let mut out = Vec::with_capacity(len);
    while out.len() < len {
        match rng.next_u32() % 5 {
            0 => {
                let n = 1 + rng.next_u32() % 40;
                out.extend(std::iter::repeat_n(0u8, n as usize));
            }
            1 => {
                let b = (rng.next_u32() % 256) as u8;
                let n = 1 + rng.next_u32() % 30;
                out.extend(std::iter::repeat_n(b, n as usize));
            }
            2 => {
                let text = b"the quick brown fox, jumps over; the lazy dog\n";
                out.extend_from_slice(text);
            }
            3 => {
                let text = b"    \"key\": [1, 2, 3],\n";
                out.extend_from_slice(text);
            }
            _ => {
                for _ in 0..1 + rng.next_u32() % 20 {
                    out.push((rng.next_u32() >> 3) as u8);
                }
            }
        }
    }
    out.truncate(len);
    out
}

#[test]
#[cfg_attr(miri, ignore)] // megabytes across threads: hours under an interpreter
fn parallel_output_is_the_sequential_output() {
    for &threads in &[2usize, 3, 4, 8] {
        for &seed in &[1u64, 0x2545_F491_4F6C_DD1D, 99] {
            let data = mixed(5 * MIN_PARALLEL_CHUNK + 12345, seed);
            let par = encode_parallel(&data, threads);
            assert_eq!(par, encode(&data), "threads={threads} seed={seed}");
            assert_eq!(decode(&par).unwrap(), data);
        }
    }
}

#[test]
#[cfg_attr(miri, ignore)] // megabytes across threads: hours under an interpreter
fn seams_land_on_every_kind_of_construct() {
    // A chunk boundary falls at a fixed multiple of MIN_PARALLEL_CHUNK, so
    // shifting the input by a byte walks the seam across whatever is there --
    // the middle of a zero run, of a DP segment, of a block-mode stretch.
    let base = mixed(3 * MIN_PARALLEL_CHUNK, 7);
    for shift in 0..8usize {
        let data = &base[shift..];
        assert_eq!(encode_parallel(data, 4), encode(data), "shift={shift}");
    }
}

#[test]
#[cfg_attr(miri, ignore)] // megabytes across threads: hours under an interpreter
fn uniform_inputs_that_defeat_convergence_still_agree() {
    // High-entropy input has no construct at all: every worker's chain is one
    // long block-mode run, and a seam can only be repaired by re-encoding to
    // the next four-byte boundary. Long passthrough is the mirror image.
    let mut rng = Rng(4242);
    let random: Vec<u8> = (0..3 * MIN_PARALLEL_CHUNK).map(|_| (rng.next_u32() >> 5) as u8).collect();
    assert_eq!(encode_parallel(&random, 4), encode(&random));

    let text: Vec<u8> = b"abcdefghijklmnopqrstuvwxyz "
        .iter()
        .copied()
        .cycle()
        .take(3 * MIN_PARALLEL_CHUNK)
        .collect();
    assert_eq!(encode_parallel(&text, 4), encode(&text));

    let zeros = vec![0u8; 3 * MIN_PARALLEL_CHUNK];
    assert_eq!(encode_parallel(&zeros, 4), encode(&zeros));
}

#[test]
#[cfg_attr(miri, ignore)] // megabytes across threads: hours under an interpreter
fn small_and_degenerate_inputs_fall_back() {
    for data in [vec![], vec![0u8], b"hello".to_vec(), vec![0xffu8; 100_000]] {
        for &threads in &[0usize, 1, 4] {
            assert_eq!(encode_parallel(&data, threads), encode(&data));
        }
    }
}
