// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Deliberately malformed input: `decode` must return `Err`, never panic.

use crate::constants::{
    DP_SIGNAL_BASE, FILL_SIGNAL_BASE, FUTURE_SIGNAL_BASE, MAX_DP_SEGMENT_CHARS, MAX_FILL_BYTES,
    MAX_TAIL_ZEROS, TAIL_SIGNAL_BASE,
};
use crate::digits::value_to_group;
use crate::{decode, DecodeError};

/// A 5-character DP signal (spec section 9). The length is the real character
/// count, 1..=2048; the field stores it biased by one.
fn dp_signal(profile: u64, mask: u64, length: u64) -> String {
    value_to_group(DP_SIGNAL_BASE + ((profile << 24) | (mask << 11) | (length - 1)))
}

/// A 5-character Solid Fill signal for `byte` repeated `length` times.
fn fill_signal(byte: u64, length: u64) -> String {
    value_to_group(FILL_SIGNAL_BASE + ((byte << 11) | (length - 1)))
}

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
    // Build a real DP-encoded string, then chop characters off the end of its
    // data segment so the signal's declared length can't be satisfied.
    let data: Vec<u8> = (0..40u8).map(|i| b'a' + (i % 26)).collect();
    let encoded = crate::encode(&data);
    assert!(encoded.len() > 5, "expected a DP signal + data, got {encoded:?}");

    let truncated = &encoded[..encoded.len() - 3];
    let err = decode(truncated).unwrap_err();
    assert!(matches!(err, DecodeError::UnexpectedEndOfStream), "{err:?}");
}

/// A truncated segment that also carries an invalid character reports the
/// truncation, not the character.
///
/// The two conditions are decided in that order because the declared length is
/// a property of the signal and the character only of what happened to arrive:
/// spec section 10 lists both, and the C implementation checks the length
/// first, so this is where the implementations have to agree. It is also the
/// one ordering the streaming error reporter has to go out of its way to
/// preserve -- it sees the character before it knows the segment is short --
/// which is why it is pinned here rather than left to the differential fuzzer.
#[test]
fn a_truncated_segment_with_a_bad_character_reports_the_truncation() {
    let data: Vec<u8> = (0..40u8).map(|i| b'a' + (i % 26)).collect();
    let encoded = crate::encode(&data);
    let mut truncated: String = encoded[..encoded.len() - 3].to_string();
    // Replace a character *inside* the segment with one no profile can carry.
    let at = truncated.len() - 4;
    truncated.replace_range(at..at + 1, "|");

    let err = decode(&truncated).unwrap_err();
    assert!(
        matches!(err, DecodeError::UnexpectedEndOfStream),
        "the declared length is checked before the characters that arrived: {err:?}"
    );
}

#[test]
fn length_field_is_biased_by_one() {
    // The smallest segment a signal can name is one character, not zero. A
    // decoder that forgets the bias reads nothing here and then misparses
    // whatever follows.
    assert_eq!(decode(&format!("{}a", dp_signal(0, 0, 1))).unwrap(), b"a");
    let err = decode(&dp_signal(0, 0, 1)).unwrap_err();
    assert!(matches!(err, DecodeError::UnexpectedEndOfStream), "{err:?}");

    // The same for Fill, where it decides between one byte and none at all.
    assert_eq!(decode(&fill_signal(0x41, 1)).unwrap(), b"A");
}

#[test]
fn future_signal_space_is_rejected() {
    // One past the last Solid Fill signal.
    let encoded = value_to_group(FUTURE_SIGNAL_BASE);
    let err = decode(&format!("{}{}", encoded, "a".repeat(2048))).unwrap_err();
    match err {
        DecodeError::UndefinedSignal { value } => assert_eq!(value, FUTURE_SIGNAL_BASE),
        other => panic!("expected UndefinedSignal, got {other:?}"),
    }

    // The very top of the 5-character space: '#####' = 85^5 - 1.
    let err = decode("#####").unwrap_err();
    assert!(matches!(err, DecodeError::UndefinedSignal { .. }), "{err:?}");
}

#[test]
fn the_signal_range_boundaries_are_where_the_spec_puts_them() {
    // Last DP signal: profile 7, all 13 mask bits, a 2048-character segment.
    let last_dp = FILL_SIGNAL_BASE - 1;
    assert_eq!(last_dp, DP_SIGNAL_BASE + (1 << 27) - 1);
    let data = "a".repeat(MAX_DP_SEGMENT_CHARS);
    let decoded = decode(&format!("{}{}", value_to_group(last_dp), data)).unwrap();
    assert_eq!(decoded.len(), MAX_DP_SEGMENT_CHARS);

    // Last solid Fill signal: byte 0xFF repeated 2048 times.
    let last_fill = TAIL_SIGNAL_BASE - 1;
    let decoded = decode(&value_to_group(last_fill)).unwrap();
    assert_eq!(decoded, vec![0xffu8; MAX_FILL_BYTES]);

    // First solid Fill signal: byte 0x00, once.
    let decoded = decode(&value_to_group(FILL_SIGNAL_BASE)).unwrap();
    assert_eq!(decoded, vec![0u8]);

    // First tail Fill signal: order 0, one zero, two NUL literals.
    let decoded = decode(&value_to_group(TAIL_SIGNAL_BASE)).unwrap();
    assert_eq!(decoded, vec![0u8; 3]);

    // Last tail Fill signal: order 1, 32 zeros, two 0xFF literals.
    let decoded = decode(&value_to_group(FUTURE_SIGNAL_BASE - 1)).unwrap();
    let mut expected = vec![0xffu8; 2];
    expected.extend(std::iter::repeat_n(0u8, MAX_TAIL_ZEROS));
    assert_eq!(decoded, expected);
}

#[test]
fn every_profile_identifier_decodes() {
    // All eight values of the 3-bit field are defined; none is reserved.
    for p in 0..8u64 {
        let body = "^@%$?!~#abcdefghijkl";
        let encoded = format!("{}{}", dp_signal(p, 0x1FFF, body.len() as u64), body);
        let decoded = decode(&encoded).unwrap_or_else(|e| panic!("profile {p}: {e:?}"));
        assert_eq!(decoded.len(), body.len(), "profile {p} changed the length");
    }
}

#[test]
fn invalid_single_character_trailing_group() {
    let err = decode("a").unwrap_err();
    assert!(matches!(err, DecodeError::InvalidFinalBlock { length: 1 }), "{err:?}");

    // Also after a valid leading full group.
    let mut encoded = crate::encode(&[1u8, 2, 3, 4]);
    encoded.push('a');
    let err = decode(&encoded).unwrap_err();
    assert!(matches!(err, DecodeError::InvalidFinalBlock { length: 1 }), "{err:?}");
}

#[test]
fn final_block_must_be_canonical() {
    // Section 7.5: a trailing group has to be exactly what encoding its bytes
    // produces. Every other character sequence that '#'-pads to the same bytes
    // is an alias, and is rejected.
    for bytes in [vec![0x61u8], vec![0x61, 0x62], vec![0xff, 0xff, 0xff]] {
        let canonical = crate::encode(&bytes);
        assert_eq!(decode(&canonical).unwrap(), bytes);

        // Nudge the last character up by one: the '#' padding absorbs the
        // difference, so a decoder without the canonicity check accepts it.
        let mut chars: Vec<char> = canonical.chars().collect();
        let last = chars.len() - 1;
        let v = crate::alphabet::char_to_value(chars[last]).unwrap();
        if v < 84 {
            chars[last] = crate::alphabet::value_to_char(v + 1);
            let alias: String = chars.into_iter().collect();
            let err = decode(&alias)
                .map(|d| format!("accepted, decoding to {d:?}"))
                .unwrap_err()
                .to_string();
            assert!(err.contains("final block"), "{alias:?}: {err}");
        }
    }
}

#[test]
fn partial_block_value_out_of_range() {
    // Four maximal digits ('#' = value 84) padded with a fifth '#' would
    // reconstruct a value >= 2^32, which cannot represent a partial block.
    for over_limit in ["##", "###", "####"] {
        let err = decode(over_limit).unwrap_err();
        assert!(
            matches!(err, DecodeError::InvalidFinalBlock { .. }),
            "{over_limit}: {err:?}"
        );
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
        &dp_signal(7, 0x1FFF, 2048),
        &format!("{}xxxxx", dp_signal(3, 0, 40)),
        &format!("{}~", fill_signal(0, 2048)),
    ];
    for c in cases {
        let _ = decode(c); // must not panic
    }
}

#[test]
fn unexpected_end_of_stream_mid_five_char_group_is_not_a_panic() {
    // 4 valid-looking chars with no 5th: this is the partial-block path, not
    // literally "mid group", but must still resolve to a clean Err for
    // genuinely malformed padding-implied values, and Ok otherwise -- either
    // way, never panic.
    for s in ["ab", "abc", "abcd"] {
        let _ = decode(s);
    }
}

/// Spec section 13: a Fill signal expands by at most 2048 bytes per five
/// characters, and a decoder must stay inside that bound rather than trusting
/// the stream.
#[test]
fn fill_expansion_stays_bounded() {
    let signals = 2000;
    let stream = fill_signal(0x5a, MAX_FILL_BYTES as u64).repeat(signals);
    let decoded = decode(&stream).unwrap();
    assert_eq!(decoded.len(), signals * MAX_FILL_BYTES);
    assert!(decoded.iter().all(|&b| b == 0x5a));
    // The ratio the security section quotes, and nothing above it.
    assert_eq!(decoded.len() / stream.len(), MAX_FILL_BYTES / 5);
}
