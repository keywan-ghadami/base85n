// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Deliberately malformed input: `decode` must return `Err`, never panic.

use crate::{decode, DecodeError};

#[test]
fn invalid_character_outside_alphabet_n() {
    // '|' is not a member of Alphabet-N (R-Set characters never appear
    // literally in valid encoded output -- they're substituted).
    let err = decode("abc|").unwrap_err();
    assert!(matches!(err, DecodeError::InvalidCharacter { character: '|', .. }), "{err:?}");

    // A full 5-character group containing an invalid character.
    let err = decode("abc|e").unwrap_err();
    assert!(matches!(err, DecodeError::InvalidCharacter { character: '|', .. }), "{err:?}");

    // A non-ASCII character.
    let err = decode("abcdé").unwrap_err();
    assert!(matches!(err, DecodeError::InvalidCharacter { character: 'é', .. }), "{err:?}");
}

#[test]
fn dp_signal_declared_length_overruns_available_input() {
    // Build a real DP-encoded string, then chop characters off the end of
    // its data segment so the signal's declared length can't be
    // satisfied.
    let data: Vec<u8> = (0..40u8).map(|i| b'a' + (i % 26)).collect();
    let encoded = crate::encode(&data);
    assert!(encoded.len() > 5, "expected a DP signal + data, got {encoded:?}");

    // Drop the last 3 characters (still leaves the 5-char signal intact,
    // since the data segment here is much longer than that).
    let truncated = &encoded[..encoded.len() - 3];
    let err = decode(truncated).unwrap_err();
    assert!(matches!(err, DecodeError::UnexpectedEndOfStream), "{err:?}");
}

#[test]
fn trailing_lone_escape_at_end_of_dp_segment() {
    // Manually construct a DP signal (mask=0, length=1) followed by a
    // single '~' as the entire (1-character) data segment: the decoder
    // must see the escape character with nothing left to escape.
    let payload: u64 = 1; // mask=0, len9=1
    let signal = crate::digits::value_to_group((1u64 << 32) + payload);
    let encoded = format!("{signal}~");
    let err = decode(&encoded).unwrap_err();
    assert!(matches!(err, DecodeError::DanglingEscapeCharacter), "{err:?}");
}

#[test]
fn signal_payload_in_reserved_range() {
    // payload = 2^22 (one past the maximum valid value of 2^22 - 1).
    let value = (1u64 << 32) + (1u64 << 22);
    let encoded = crate::digits::value_to_group(value);
    let err = decode(&encoded).unwrap_err();
    match err {
        DecodeError::ReservedSignalValue { payload } => assert_eq!(payload, 1u64 << 22),
        other => panic!("expected ReservedSignalValue, got {other:?}"),
    }
}

#[test]
fn invalid_single_character_trailing_group() {
    let err = decode("a").unwrap_err();
    assert!(matches!(err, DecodeError::InvalidPartialBlock { length: 1 }), "{err:?}");

    // Also after a valid leading full group.
    let data: Vec<u8> = vec![1, 2, 3, 4];
    let mut encoded = crate::encode(&data);
    encoded.push('a');
    let err = decode(&encoded).unwrap_err();
    assert!(matches!(err, DecodeError::InvalidPartialBlock { length: 1 }), "{err:?}");
}

#[test]
fn partial_block_overrun_value_out_of_range() {
    // Four maximal digits ('#' = value 84) padded with a fifth '#' would
    // reconstruct a value >= 2^32, which cannot represent a partial block.
    let err = decode("####").unwrap_err();
    assert!(matches!(err, DecodeError::InvalidPartialBlock { length: 4 }), "{err:?}");

    // The boundary itself, spec 7.1: "%nSb" pads to 2^32 - 2 and decodes,
    // "%nSc" is the very next group and pads to 2^32 + 83.
    assert_eq!(decode("%nSb").unwrap(), vec![0xff, 0xff, 0xff]);
    let err = decode("%nSc").unwrap_err();
    assert!(matches!(err, DecodeError::InvalidPartialBlock { length: 4 }), "{err:?}");

    // The 2- and 3-character forms take a different branch of the padding.
    for over_limit in ["##", "###"] {
        let err = decode(over_limit).unwrap_err();
        assert!(matches!(err, DecodeError::InvalidPartialBlock { .. }), "{over_limit}: {err:?}");
    }
}

#[test]
fn decode_never_panics_on_arbitrary_short_garbage() {
    // A grab-bag of malformed inputs that must produce `Err`, not panic.
    let cases = [
        "~",
        "~~~",
        "12345~",
        "\u{0}",
        "     ",
        "#####",
        "$$$$$",
        "vpA.2f!@~",
    ];
    for c in cases {
        let _ = decode(c); // must not panic
    }
}

#[test]
fn unexpected_end_of_stream_mid_five_char_group_is_not_a_panic() {
    // 4 valid-looking chars with no 5th: this is the partial-block path,
    // not literally "mid group", but must still resolve to a clean Err
    // for genuinely malformed padding-implied values, and Ok otherwise --
    // either way, never panic.
    for s in ["ab", "abc", "abcd"] {
        let _ = decode(s);
    }
}
