//! Decoding algorithm (spec section 7).

use crate::alphabet::{char_to_value, replacement_index_for_byte, RSET_ASCII};
use crate::constants::{DP_SIGNAL_BASE, MAX_SIGNAL_PAYLOAD};
use crate::digits::chars_to_value;
use crate::error::DecodeError;

/// A single input character together with its byte offset in the original
/// `&str`, used for error reporting.
#[derive(Clone, Copy)]
struct PosChar {
    c: char,
    pos: usize,
}

/// Decode a Base85N string back into the original bytes.
pub fn decode(s: &str) -> Result<Vec<u8>, DecodeError> {
    // Section 7.1: whitespace (space, tab, LF, CR) between Base85N
    // constructs is always ignorable. None of these four characters is a
    // member of Alphabet-N, and DP mode never emits them literally (R-Set
    // characters are always substituted before reaching the transformed
    // stream), so it is always correct to strip them up front.
    let chars: Vec<PosChar> = s
        .char_indices()
        .filter(|&(_, c)| !matches!(c, ' ' | '\t' | '\n' | '\r'))
        .map(|(pos, c)| PosChar { c, pos })
        .collect();

    let n = chars.len();
    let mut i = 0usize;
    let mut out = Vec::new();

    while i < n {
        let remaining = n - i;

        if remaining >= 5 {
            let decoded_value = value_of_group(&chars[i..i + 5])?;
            i += 5;

            if decoded_value < DP_SIGNAL_BASE {
                let v32 = decoded_value as u32;
                out.extend_from_slice(&v32.to_be_bytes());
            } else {
                let payload = decoded_value - DP_SIGNAL_BASE;
                if payload > MAX_SIGNAL_PAYLOAD {
                    return Err(DecodeError::ReservedSignalValue { payload });
                }
                let mask13 = ((payload >> 9) & 0x1FFF) as u16;
                let len9 = (payload & 0x1FF) as usize;

                if i + len9 > n {
                    return Err(DecodeError::UnexpectedEndOfStream);
                }
                let seg = &chars[i..i + len9];
                decode_dp_segment(seg, mask13, &mut out)?;
                i += len9;
            }
        } else if remaining == 0 {
            break;
        } else {
            // Section 7.1, final bullet: a trailing group of 2, 3, or 4
            // characters decodes to 1, 2, or 3 bytes respectively. A
            // single leftover character cannot be a valid partial block.
            if remaining == 1 {
                return Err(DecodeError::InvalidPartialBlock { length: remaining });
            }

            let mut digits = [84u8; 5]; // pad with '#' (value 84)
            for k in 0..remaining {
                let pc = chars[i + k];
                digits[k] = char_to_value(pc.c)
                    .ok_or(DecodeError::InvalidCharacter { character: pc.c, position: pc.pos })?;
            }
            let decoded_value = crate::digits::digits_to_value(&digits);
            if decoded_value >= (1u64 << 32) {
                // The reconstructed 32-bit number overflowed: not a valid
                // partial block encoding.
                return Err(DecodeError::InvalidPartialBlock { length: remaining });
            }
            let v32 = decoded_value as u32;
            let bytes = v32.to_be_bytes();
            out.extend_from_slice(&bytes[..remaining - 1]);
            i += remaining;
        }
    }

    Ok(out)
}

fn value_of_group(group: &[PosChar]) -> Result<u64, DecodeError> {
    debug_assert_eq!(group.len(), 5);
    let cs: Vec<char> = group.iter().map(|pc| pc.c).collect();
    match chars_to_value(&cs) {
        Some(v) => Ok(v),
        None => {
            // Find and report the first offending character.
            for pc in group {
                if char_to_value(pc.c).is_none() {
                    return Err(DecodeError::InvalidCharacter { character: pc.c, position: pc.pos });
                }
            }
            unreachable!("chars_to_value failed but no invalid character found")
        }
    }
}

/// Section 7.1.e: convert a DP segment's transformed characters back into
/// original bytes.
fn decode_dp_segment(seg: &[PosChar], mask13: u16, out: &mut Vec<u8>) -> Result<(), DecodeError> {
    let mut idx = 0usize;
    while idx < seg.len() {
        let pc = seg[idx];
        char_to_value(pc.c)
            .ok_or(DecodeError::InvalidCharacter { character: pc.c, position: pc.pos })?;

        if pc.c == '~' {
            idx += 1;
            if idx >= seg.len() {
                return Err(DecodeError::DanglingEscapeCharacter);
            }
            let pc2 = seg[idx];
            char_to_value(pc2.c)
                .ok_or(DecodeError::InvalidCharacter { character: pc2.c, position: pc2.pos })?;
            out.push(pc2.c as u8);
            idx += 1;
        } else if let Some(j) = replacement_index_for_byte(pc.c as u8) {
            if mask13 & (1 << j) != 0 {
                out.push(RSET_ASCII[j as usize]);
            } else {
                out.push(pc.c as u8);
            }
            idx += 1;
        } else {
            out.push(pc.c as u8);
            idx += 1;
        }
    }
    Ok(())
}
