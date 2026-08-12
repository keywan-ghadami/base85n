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

/// A 5-character DP signal for an alphabet and a real character length.
/// Spec section 9 stores the length biased by one.
fn signal(alphabet: u64, length: u64) -> String {
    crate::digits::value_to_group((1u64 << 32) + ((alphabet << 10) | (length - 1)))
}

#[test]
fn length_field_is_biased_by_one() {
    // The smallest segment a signal can name is one character, not zero. A
    // decoder that forgets the bias reads nothing here and then misparses
    // whatever follows.
    assert_eq!(decode(&format!("{}a", signal(0, 1))).unwrap(), b"a");
    let err = decode(&signal(0, 1)).unwrap_err();
    assert!(matches!(err, DecodeError::UnexpectedEndOfStream), "{err:?}");
}

#[test]
fn signal_payload_in_reserved_range() {
    // payload = 2^13 (one past the maximum valid value of 2^13 - 1).
    let value = (1u64 << 32) + (1u64 << 13);
    let encoded = crate::digits::value_to_group(value);
    let err = decode(&format!("{}{}", encoded, "a".repeat(1024))).unwrap_err();
    match err {
        DecodeError::ReservedSignalValue { payload } => assert_eq!(payload, 1u64 << 13),
        other => panic!("expected ReservedSignalValue, got {other:?}"),
    }
}

#[test]
fn maximum_signal_payload_is_still_valid() {
    // The adjacent legal case, so the two together pin the boundary: payload
    // 2^13 - 1 is alphabet 7 carrying a 1024-character segment.
    let value = (1u64 << 32) + ((1u64 << 13) - 1);
    let encoded = crate::digits::value_to_group(value);
    let data = "a".repeat(1024);
    assert_eq!(decode(&format!("{encoded}{data}")).unwrap(), data.as_bytes());
}

#[test]
fn every_alphabet_identifier_decodes() {
    // All eight values of the 3-bit field are defined; none is reserved.
    for a in 0..8u64 {
        let body = "^@%$?!~#abcdefghijkl";
        let encoded = format!("{}{}", signal(a, body.len() as u64), body);
        let decoded = decode(&encoded).unwrap_or_else(|e| panic!("alphabet {a}: {e:?}"));
        assert_eq!(decoded.len(), body.len(), "alphabet {a} changed the length");
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
        &signal(7, 1024),
        &format!("{}xxxxx", signal(3, 40)),
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
