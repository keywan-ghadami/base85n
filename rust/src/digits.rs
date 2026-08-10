// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Base85 digit <-> value conversion (spec section 8).

use crate::alphabet::{char_to_value, value_to_char};

/// `Base85DigitsToValue`: combine 5 Alphabet-N digit values (each 0-84)
/// into a single integer, most-significant digit first.
pub fn digits_to_value(digits: &[u8; 5]) -> u64 {
    let mut val: u64 = 0;
    for &d in digits {
        val = val * 85 + d as u64;
    }
    val
}

/// `ValueToBase85Digits`: split `value` into 5 Alphabet-N digit values
/// (each 0-84), most-significant digit first. `value` must be `< 85^5`.
pub fn value_to_digits(mut value: u64) -> [u8; 5] {
    let mut digits = [0u8; 5];
    for i in (0..5).rev() {
        digits[i] = (value % 85) as u8;
        value /= 85;
    }
    digits
}

/// Encode `value` as a 5-character Alphabet-N string.
pub fn value_to_group(value: u64) -> String {
    let digits = value_to_digits(value);
    digits.iter().map(|&d| value_to_char(d)).collect()
}

/// Convert 5 already-validated Alphabet-N characters into their combined
/// value. Returns `None` if any character is not in Alphabet-N.
pub fn chars_to_value(chars: &[char]) -> Option<u64> {
    debug_assert_eq!(chars.len(), 5);
    let mut digits = [0u8; 5];
    for (i, &c) in chars.iter().enumerate() {
        digits[i] = char_to_value(c)?;
    }
    Some(digits_to_value(&digits))
}
