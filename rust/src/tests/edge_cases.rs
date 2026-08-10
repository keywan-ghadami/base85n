// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Explicit boundary-condition tests.

use crate::{decode, encode};

fn roundtrip(data: &[u8]) {
    let encoded = encode(data);
    let decoded = decode(&encoded).unwrap_or_else(|e| panic!("decode failed: {e:?} (encoded = {encoded:?})"));
    assert_eq!(decoded, data, "roundtrip mismatch; encoded = {encoded:?}");
}

#[test]
fn empty_input() {
    assert_eq!(encode(&[]), "");
    assert_eq!(decode("").unwrap(), Vec::<u8>::new());
}

#[test]
fn lengths_one_through_four() {
    for len in 1..=4usize {
        let data: Vec<u8> = (0..len as u8).map(|i| b'a' + i).collect();
        roundtrip(&data);
    }
}

/// Below `MIN_PASSTHROUGH_BYTES` (20): DP mode cannot be used, so the
/// whole prefix is emitted via plain block mode (a partial trailing group
/// is fine since this prefix is the entire, final buffer).
#[test]
fn below_min_passthrough_bytes_uses_block_mode() {
    let data: Vec<u8> = (0..19u8).map(|i| b'0' + (i % 10)).collect();
    assert_eq!(data.len(), 19);
    let encoded = encode(&data);
    // 4 full 4-byte blocks (20 chars) + 1 partial block of 3 leftover
    // bytes (4 chars) = 24 chars; DP mode is never even considered since
    // the MIN_PASSTHROUGH_BYTES gate fails outright.
    assert_eq!(encoded.len(), 24, "encoded = {encoded:?}");
    assert_eq!(decode(&encoded).unwrap(), data);
}

/// Exactly at `MIN_PASSTHROUGH_BYTES` (20): DP mode becomes eligible, and
/// for pure Alphabet-N literal content it ties with (and per the spec,
/// 6.1.2.a's `<=` rule, wins over) block mode.
#[test]
fn exactly_min_passthrough_bytes_uses_dp_mode() {
    let data: Vec<u8> = (0..20u8).map(|i| b'a' + (i % 26)).collect();
    assert_eq!(data.len(), 20);
    let encoded = encode(&data);
    // 1 DP signal (5 chars) + 20 literal data chars = 25 chars.
    assert_eq!(encoded.len(), 25, "encoded = {encoded:?}");
    assert_eq!(decode(&encoded).unwrap(), data);
}

/// One byte above `MIN_PASSTHROUGH_BYTES`.
#[test]
fn one_above_min_passthrough_bytes_uses_dp_mode() {
    let data: Vec<u8> = (0..21u8).map(|i| b'a' + (i % 26)).collect();
    let encoded = encode(&data);
    // 1 DP signal (5 chars) + 21 literal data chars = 26 chars.
    assert_eq!(encoded.len(), 26, "encoded = {encoded:?}");
    assert_eq!(decode(&encoded).unwrap(), data);
}

/// A segment long enough that its transformed length exceeds
/// `MAX_DP_OUTPUT_CHARS_PER_SIGNAL` (511), forcing multiple consecutive
/// DP-signaled segments.
#[test]
fn long_segment_requires_multiple_dp_signals() {
    let data: Vec<u8> = (0..1000u32).map(|i| b'0' + (i % 10) as u8).collect();
    let encoded = encode(&data);
    // transformed length == 1000 (pure literals, 1:1); ceil(1000/511) = 2
    // signals * 5 chars + 1000 data chars = 1010.
    assert_eq!(encoded.len(), 1010, "encoded = {encoded:?}");
    assert_eq!(decode(&encoded).unwrap(), data);
}

/// A run of the escape character long enough to trigger the
/// `MAX_CONSECUTIVE_ESCAPES` (3) scan-termination heuristic mid-prefix,
/// followed by more data. Exercises spec section 6.1.b Case ii's
/// termination branch (Pass 2 of the two-pass DP scan).
#[test]
fn escape_run_triggers_scan_termination_heuristic() {
    let mut data = vec![b'a'; 25];
    data.extend(std::iter::repeat_n(b'~', 4)); // 4 > MAX_CONSECUTIVE_ESCAPES
    data.extend(vec![b'b'; 25]);
    roundtrip(&data);
}

/// Even longer / repeated escape runs, and escape runs positioned at the
/// very start and end of the buffer.
#[test]
fn escape_runs_various_positions() {
    roundtrip(&[b'~'; 4]);
    roundtrip(&[b'~'; 30]);

    let mut data = vec![b'~'; 5];
    data.extend(vec![b'x'; 25]);
    roundtrip(&data);

    let mut data = vec![b'x'; 25];
    data.extend(vec![b'~'; 5]);
    roundtrip(&data);
}

/// Input containing every possible byte value 0-255.
#[test]
fn all_byte_values_present() {
    let data: Vec<u8> = (0u16..=255).map(|b| b as u8).collect();
    assert_eq!(data.len(), 256);
    roundtrip(&data);

    // And repeated a few times to comfortably clear MIN_PASSTHROUGH_BYTES
    // and cross multiple DP-segment boundaries, in case the single pass
    // above happens to fall back to block mode throughout.
    let mut data2 = Vec::new();
    for _ in 0..8 {
        data2.extend((0u16..=255).map(|b| b as u8));
    }
    roundtrip(&data2);
}

/// A DP prefix scan that starts with an unrepresentable byte (bytes
/// outside Alphabet-N's ASCII range) must yield an empty candidate
/// prefix, falling through to a raw `min(4, len)` block.
#[test]
fn unrepresentable_first_byte_falls_back_to_small_block() {
    let mut data = vec![0xFFu8, 0x00, 0x01, 0x02, 0x03];
    data.extend(vec![b'a'; 30]);
    roundtrip(&data);

    // Also as the very first (and only) bytes.
    roundtrip(&[0xFFu8]);
    roundtrip(&[0xFFu8, 0xFE, 0xFD]);
}

/// R-Set characters interleaved with their own replacement characters
/// (the scenario the spec's Pass 1/Pass 2 procedure exists to handle
/// correctly): a `allowedPassthroughSafeReplacementCharacters[j]` byte
/// occurring *before* the first `R_Char[j]` occurrence in the same prefix.
#[test]
fn replacement_char_before_rset_char_same_prefix() {
    // ':' (replacement for space, j=0) appears before any space.
    let mut data = b":".to_vec();
    data.extend(vec![b'a'; 19]);
    data.push(b' '); // now activates j=0, retroactively requiring the
                      // earlier ':' to have been escaped in the transform.
    roundtrip(&data);

    // Same idea for several R-Set/replacement pairs at once.
    let mut data2 = b":+=^!/*?`()[]".to_vec(); // all 13 replacement chars, literal
    data2.extend(vec![b'a'; 10]);
    data2.extend(b" \"',;\\|<>&\t\n\r".to_vec()); // all 13 R-Set chars
    roundtrip(&data2);
}
