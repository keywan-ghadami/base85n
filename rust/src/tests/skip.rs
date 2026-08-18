// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Spec section 12.3, last item: "An encoder that skips ahead (Section 11.1)
//! emits exactly what one that does not emits, on inputs whose zero runs sit at
//! every offset modulo four."
//!
//! This is the check the skip needs most, because the skip is where the
//! encoder's decision rules are written down a second time. `window_may_hold`
//! answers for two groups at word level, `decision_at` for one group exactly,
//! and the tail loop of `next_decision_point` for the positions near the end of
//! the input -- three restatements of "could step 1, 2 or 3 begin here", none of
//! which shares code with `choose_fill` or `scan_dp`, which decide it for real.
//! Any of them may say *maybe* where the real rules say no; none may say *no*
//! where the real rules say yes. The second kind of divergence does not fail a
//! round trip and does not fail the golden vectors unless the vectors happen to
//! contain it: the output stays decodable, it is simply no longer the one every
//! other implementation produces (spec section 6.5, rule 6).
//!
//! So the comparison is against the encoder without the skip, which
//! `set_skip_enabled` builds out of the same code, over inputs shaped like the
//! decisions the skip has to get right: runs of zeros and of other bytes at
//! every offset modulo four, lengths at the thresholds that decide each mode,
//! and transitions between text and binary.

use crate::encode::{encode_range, set_skip_enabled};
use crate::{decode, encode};

/// Encodes `data` twice, once with the skip and once without, and returns the
/// two strings.
fn both_ways(data: &[u8]) -> (String, String) {
    let with = encode(data);
    let was = set_skip_enabled(false);
    let without = encode(data);
    set_skip_enabled(was);
    (with, without)
}

fn assert_same(data: &[u8], what: &str) {
    let (with, without) = both_ways(data);
    assert_eq!(
        with, without,
        "the skip changed the output on {what} ({} bytes)",
        data.len()
    );
    assert_eq!(decode(&with).expect("decodes"), data, "round trip on {what}");
}

/// A deterministic byte source, so a failure names a case that can be rerun.
struct Rng(u64);

impl Rng {
    fn next(&mut self) -> u64 {
        self.0 ^= self.0 << 13;
        self.0 ^= self.0 >> 7;
        self.0 ^= self.0 << 17;
        self.0
    }

    fn byte(&mut self) -> u8 {
        (self.next() >> 24) as u8
    }

    fn below(&mut self, n: usize) -> usize {
        (self.next() % n as u64) as usize
    }
}

/// The case the spec names: a run of zeros at every offset modulo four, at
/// every length that crosses a threshold, in a filler the skip runs over.
#[test]
fn zero_runs_at_every_offset_modulo_four() {
    for offset in 0..16 {
        for run in 0..40 {
            for filler in [0x80u8, b'x', 0xFF] {
                let mut data = vec![filler; offset];
                data.extend(std::iter::repeat_n(0u8, run));
                data.extend(std::iter::repeat_n(filler, 64));
                assert_same(&data, &format!("offset {offset}, {run} zeros, filler {filler:#04x}"));
            }
        }
    }
}

/// The same for runs of a byte that is not zero, which reach Fill but not the
/// tail variant, and for runs of a representable byte, which a passthrough
/// segment may swallow instead.
#[test]
fn equal_byte_runs_at_every_offset_modulo_four() {
    for offset in 0..16 {
        for run in [1usize, 2, 3, 4, 5, 6, 15, 16, 17, 19, 20, 21, 32] {
            for value in [b'a', 0x80, 0xFF, b' '] {
                let mut data: Vec<u8> = (0..offset).map(|i| 0x80u8.wrapping_add(i as u8)).collect();
                data.extend(std::iter::repeat_n(value, run));
                data.extend((0..48).map(|i| 0x90u8.wrapping_add(i as u8)));
                assert_same(&data, &format!("offset {offset}, {run}x{value:#04x}"));
            }
        }
    }
}

/// Text and binary meeting at every offset: the skip's gate is exactly the
/// question of whether a passthrough segment could start here, so the boundary
/// between a stretch it can and one it cannot is where it decides.
#[test]
fn text_and_binary_transitions_at_every_offset() {
    let text = b"the quick brown fox jumps over the lazy dog and keeps going for a while";
    for cut in 0..40 {
        for tail in [4usize, 20, 21, 64] {
            let mut data: Vec<u8> = text[..cut.min(text.len())].to_vec();
            data.extend((0..tail).map(|i| (i as u8).wrapping_mul(37).wrapping_add(0x81)));
            data.extend_from_slice(&text[..text.len().min(40)]);
            assert_same(&data, &format!("cut {cut}, {tail} binary bytes"));
        }
    }
}

/// Randomised, over the mixture the skip meets in practice: mostly bytes it
/// steps over, punctuated by the constructs it must not step past.
#[test]
fn randomised_mixtures() {
    let mut rng = Rng(0x51D4_2A17_9C3E_0001);
    for case in 0..400 {
        let len = 1 + rng.below(3000);
        let mut data = Vec::with_capacity(len);
        while data.len() < len {
            match rng.below(10) {
                0..=3 => data.push(rng.byte()),
                4 => {
                    let run = 1 + rng.below(40);
                    let b = if rng.below(2) == 0 { 0 } else { rng.byte() };
                    for _ in 0..run {
                        data.push(b);
                    }
                }
                5..=7 => {
                    let word = b"lorem ipsum dolor sit amet, consectetur 0123456789";
                    let take = 1 + rng.below(word.len());
                    data.extend_from_slice(&word[..take]);
                }
                _ => {
                    let zeros = 1 + rng.below(8);
                    data.extend(std::iter::repeat_n(0u8, zeros));
                }
            }
        }
        data.truncate(len);
        assert_same(&data, &format!("random case {case} (seed 0x51D42A179C3E0001)"));
    }
}

/// The skip is bounded by `stop` so that a parallel worker does not run a
/// block-mode stretch to the end of the file; that bound is a second place the
/// two encoders could part company.
#[test]
fn bounded_ranges_agree_too() {
    let mut rng = Rng(0x0BAD_C0DE_1234_5678);
    let data: Vec<u8> = (0..8000)
        .map(|i| match i % 7 {
            0 => 0,
            1 | 2 => rng.byte(),
            _ => b"text and more text "[i % 19],
        })
        .collect();

    for &(start, stop) in &[(0usize, 100usize), (37, 2000), (1000, 1001), (4096, 8000), (0, 8000)] {
        let with = encode_range(&data, start, stop, None, false);
        let was = set_skip_enabled(false);
        let without = encode_range(&data, start, stop, None, false);
        set_skip_enabled(was);
        assert_eq!(with.out, without.out, "range {start}..{stop}");
        assert_eq!(with.end, without.end, "range {start}..{stop} ended elsewhere");
    }
}
