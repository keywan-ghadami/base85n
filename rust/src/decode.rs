// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Decoding algorithm (spec section 7).
//!
//! Decoding runs in two tiers. [`scan`] does the work: it reads the input's
//! bytes directly, answers every question about a character with one table
//! lookup, and writes into a buffer that only grows where a Fill signal makes
//! it necessary. It reports *that* an input is malformed, not what is wrong
//! with it. [`report_error`] is the character-at-a-time implementation, kept
//! for the failure path, where it produces the error variant and position that
//! [`decode`] returns. Nothing but a rejected input pays for it.
//!
//! `scan` is written so that the compiler can discharge its bounds checks
//! rather than emit them. Two shapes do that:
//!
//! - A run of standard block groups is a uniform map -- 5 characters in, 4
//!   bytes out, no decision that changes the shape. Sizing both sides to a
//!   whole number of groups up front lets `as_chunks` carry the lengths into
//!   the loop, whose body then indexes nothing.
//! - A DP segment is an exact map as well: `L` characters produce exactly `L`
//!   bytes, because the substitution spends one character per byte and there is
//!   no escape pair. Narrowing both sides to the segment therefore gives the
//!   loop a length rather than a bound.
//!
//! Solid Fill is the one construct whose output is not bounded by its input, so
//! it is also the only one that can force the output buffer to grow: five
//! characters can name up to [`crate::constants::MAX_FILL_BYTES`] bytes. The buffer starts at the
//! exact bound for a stream without Fill in it and is extended per signal,
//! which keeps the peak proportional to what the stream actually decodes to
//! rather than to the format's worst case.

use crate::alphabet::{char_to_value, donors, ALPHABET_VALUE, DEC_BASE, DEC_INVALID};
use crate::constants::{DP_SIGNAL_BASE, FILL_SIGNAL_BASE, FUTURE_SIGNAL_BASE, TAIL_SIGNAL_BASE};
use crate::digits::{chars_to_value, value_to_5chars_32, POW85_2, POW85_3, POW85_4};
use crate::error::DecodeError;

fn is_ignorable_ws(c: u8) -> bool {
    c == b' ' || c == b'\t' || c == b'\n' || c == b'\r'
}

/// Split a DP signal payload into profile, mask and the character length of
/// the segment that follows (spec section 7.3).
///
/// The length field is stored biased by one, so the smallest segment a signal
/// can name is one character and the largest is
/// [`crate::constants::MAX_DP_SEGMENT_CHARS`].
#[inline]
fn split_dp_payload(payload: u64) -> (usize, u16, usize) {
    let length = (payload & 0x7FF) as usize + 1;
    let mask = ((payload >> 11) & 0x1FFF) as u16;
    let profile = ((payload >> 24) & 0x7) as usize;
    (profile, mask, length)
}

/// Split a Fill signal payload, solid variant, into the repeated byte and its
/// count (spec section 7.4).
#[inline]
fn split_fill_payload(payload: u64) -> (u8, usize) {
    let length = (payload & 0x7FF) as usize + 1;
    let byte = ((payload >> 11) & 0xFF) as u8;
    (byte, length)
}

/// Split a Fill signal payload, tail variant, into the number of zeros, the
/// order bit and the two literals (spec section 7.4).
#[inline]
fn split_tail_payload(payload: u64) -> (usize, u8, [u8; 2]) {
    let zeros = ((payload >> 16) & 0x1F) as usize + 1;
    let order = ((payload >> 21) & 1) as u8;
    let lit = [((payload >> 8) & 0xFF) as u8, (payload & 0xFF) as u8];
    (zeros, order, lit)
}

/// The decoding table for a DP segment: [`DEC_BASE`] with the segment's donors
/// patched to the R-Set characters they stand for (spec section 4.3).
fn dp_table(profile: usize, mask: u16) -> [u16; 256] {
    let mut table = DEC_BASE;
    let (pairs, k) = donors(profile, mask);
    for &(rset, donor) in &pairs[..k] {
        table[donor as usize] = rset as u16;
    }
    table
}

/// Grow `out` so that `need` more bytes can be written at `w`.
#[inline]
fn reserve(out: &mut Vec<u8>, w: usize, need: usize) {
    if need > out.len() - w {
        let want = w + need;
        out.resize(want + want / 4, 0);
    }
}

/// Decode `src`, which must already be free of the inter-token whitespace
/// section 7.1 allows, into `out`, and return the number of bytes produced --
/// or `None` if the input is malformed.
///
/// `out` must start out at least `src.len()` bytes long, which is the exact
/// bound for every construct but Solid Fill; the Fill branch extends it.
fn scan(src: &[u8], out: &mut Vec<u8>) -> Option<usize> {
    let n = src.len();
    let mut pos = 0usize;
    let mut w = 0usize;

    while pos < n {
        let remaining = n - pos;

        if remaining >= 5 {
            // The run of block groups. `run` is limited by the input left and
            // the output room, so both slicings below are valid by construction
            // and are checked once for the whole run rather than once per group.
            let run = ((n - pos) / 5).min((out.len() - w) / 4);
            if run > 0 {
                let (ins, _) = src[pos..pos + 5 * run].as_chunks::<5>();
                let (outs, _) = out[w..w + 4 * run].as_chunks_mut::<4>();
                let mut done = 0usize;
                for (g, o) in ins.iter().zip(outs.iter_mut()) {
                    // ALPHABET_VALUE's -1 reads back as 0xFF and every real
                    // digit value is below 0x80, so one test covers all five.
                    let v0 = ALPHABET_VALUE[g[0] as usize] as u8;
                    let v1 = ALPHABET_VALUE[g[1] as usize] as u8;
                    let v2 = ALPHABET_VALUE[g[2] as usize] as u8;
                    let v3 = ALPHABET_VALUE[g[3] as usize] as u8;
                    let v4 = ALPHABET_VALUE[g[4] as usize] as u8;
                    if (v0 | v1 | v2 | v3 | v4) & 0x80 != 0 {
                        return None;
                    }
                    // Horner's rule would chain five multiplies end to end;
                    // weighing the digits directly leaves them independent.
                    // Only the top term can leave 32 bits.
                    let value = v0 as u64 * POW85_4
                        + (v1 as u32 * POW85_3 + v2 as u32 * POW85_2 + v3 as u32 * 85 + v4 as u32)
                            as u64;
                    if value >= DP_SIGNAL_BASE {
                        break; // a signal: the general path below takes it
                    }
                    *o = (value as u32).to_be_bytes();
                    done += 1;
                }
                if done > 0 {
                    pos += 5 * done;
                    w += 4 * done;
                    continue;
                }
            }

            // A signal, or a group the run could not take. Decode it alone.
            let g: &[u8; 5] = src[pos..pos + 5].try_into().expect("five bytes");
            let v0 = ALPHABET_VALUE[g[0] as usize] as u8;
            let v1 = ALPHABET_VALUE[g[1] as usize] as u8;
            let v2 = ALPHABET_VALUE[g[2] as usize] as u8;
            let v3 = ALPHABET_VALUE[g[3] as usize] as u8;
            let v4 = ALPHABET_VALUE[g[4] as usize] as u8;
            if (v0 | v1 | v2 | v3 | v4) & 0x80 != 0 {
                return None;
            }
            let decoded_value = v0 as u64 * POW85_4
                + (v1 as u32 * POW85_3 + v2 as u32 * POW85_2 + v3 as u32 * 85 + v4 as u32) as u64;
            pos += 5;

            if decoded_value < DP_SIGNAL_BASE {
                // Only reachable when the run above had no room left.
                reserve(out, w, 4);
                out[w..w + 4].copy_from_slice(&(decoded_value as u32).to_be_bytes());
                w += 4;
                continue;
            }

            if decoded_value >= FUTURE_SIGNAL_BASE {
                return None; // FUTURE_SIGNAL_SPACE
            }

            if decoded_value >= TAIL_SIGNAL_BASE {
                // Section 7.4, tail variant: zeros and two literals, in the
                // order the payload's top bit names. No characters are read to
                // construct any of it either.
                let (zeros, order, lit) = split_tail_payload(decoded_value - TAIL_SIGNAL_BASE);
                reserve(out, w, zeros + 2);
                let (first, second) = if order == 0 { (zeros, 0) } else { (0, zeros) };
                out[w..w + first].fill(0);
                out[w + first..w + first + 2].copy_from_slice(&lit);
                out[w + first + 2..w + first + 2 + second].fill(0);
                w += zeros + 2;
                continue;
            }

            if decoded_value >= FILL_SIGNAL_BASE {
                // Section 7.4, solid variant: no characters are read to
                // construct the data.
                let (byte, length) = split_fill_payload(decoded_value - FILL_SIGNAL_BASE);
                reserve(out, w, length);
                out[w..w + length].fill(byte);
                w += length;
                continue;
            }

            let (profile, mask, length) = split_dp_payload(decoded_value - DP_SIGNAL_BASE);
            if n - pos < length {
                return None; // checked before reading, per section 7.3
            }
            reserve(out, w, length);

            // Section 4.3: one lookup per character answers membership and
            // substitution together, and the two sides are the same length.
            let table = dp_table(profile, mask);
            let seg = &src[pos..pos + length];
            let out_seg = &mut out[w..w + length];
            for (o, &c) in out_seg.iter_mut().zip(seg.iter()) {
                let t = table[c as usize];
                if t & DEC_INVALID != 0 {
                    return None;
                }
                *o = t as u8;
            }
            pos += length;
            w += length;
            continue;
        }

        // Fewer than 5 characters remain: this must be the trailing partial
        // block for the whole stream (section 7.5). A lone character cannot be
        // one, since 2 characters are the minimum for 1 byte.
        if remaining == 1 {
            return None;
        }

        let mut digits = [84u8; 5]; // conceptually padded with '#'
        for (d, &c) in digits.iter_mut().zip(&src[pos..pos + remaining]) {
            let v = ALPHABET_VALUE[c as usize];
            if v < 0 {
                return None;
            }
            *d = v as u8;
        }
        let value = crate::digits::digits_to_value(&digits);
        // Section 7.5: the padded group's value must be below 2^32.
        if value >= DP_SIGNAL_BASE {
            return None;
        }
        let produced = remaining - 1;
        let bytes = (value as u32).to_be_bytes();
        // Section 7.5, canonical enforcement: the characters must be exactly
        // what encoding those bytes zero-padded to four would have produced.
        // Without this, several character sequences decode to the same bytes.
        let mut padded = [0u8; 4];
        padded[..produced].copy_from_slice(&bytes[..produced]);
        let canonical = value_to_5chars_32(u32::from_be_bytes(padded));
        if canonical[..remaining] != src[pos..pos + remaining] {
            return None;
        }

        reserve(out, w, produced);
        out[w..w + produced].copy_from_slice(&bytes[..produced]);
        w += produced;
        pos += remaining;
    }

    Some(w)
}

/// Decode a Base85N string back into the original bytes.
pub fn decode(s: &str) -> Result<Vec<u8>, DecodeError> {
    let bytes = s.as_bytes();
    // One allocation, sized by the bound `scan` documents; only Fill grows it.
    let mut out = vec![0u8; bytes.len()];

    if let Some(produced) = scan(bytes, &mut out) {
        out.truncate(produced);
        return Ok(out);
    }

    // Section 7.1 has the decoder ignore inter-token whitespace. Rather than
    // copy every input to strip characters that a valid stream never contains,
    // take the rejection as the signal: none of the four whitespace bytes is in
    // Alphabet-N, and `scan` validates every character it consumes, so a stream
    // with whitespace in it can never decode successfully. Only once it has
    // failed is it worth building the filtered copy and decoding again.
    //
    // The retry is on any failure, not just an invalid character: whitespace
    // also shifts the group boundaries after it, so it can equally well surface
    // as a truncated final group or a short DP segment.
    if bytes.iter().any(|&c| is_ignorable_ws(c)) {
        let filtered: Vec<u8> = bytes.iter().copied().filter(|&c| !is_ignorable_ws(c)).collect();
        if out.len() < filtered.len() {
            out.resize(filtered.len(), 0);
        }
        if let Some(produced) = scan(&filtered, &mut out) {
            out.truncate(produced);
            return Ok(out);
        }
    }

    Err(report_error(s))
}

/// A single input character together with its byte offset in the original
/// `&str`, used for error reporting.
#[derive(Clone, Copy)]
struct PosChar {
    c: char,
    pos: usize,
}

/// The character-at-a-time decoder, run only after [`scan`] has rejected the
/// input. It applies exactly the same rules and exists to say *which* rule was
/// broken and where, so `decode` reports an error variant and a position --
/// positions being byte offsets into the original string, which is why this
/// walks the original rather than the filtered copy.
fn report_error(s: &str) -> DecodeError {
    let chars: Vec<PosChar> = s
        .char_indices()
        .filter(|&(_, c)| !matches!(c, ' ' | '\t' | '\n' | '\r'))
        .map(|(pos, c)| PosChar { c, pos })
        .collect();

    let n = chars.len();
    let mut i = 0usize;

    while i < n {
        let remaining = n - i;

        if remaining >= 5 {
            let decoded_value = match value_of_group(&chars[i..i + 5]) {
                Ok(v) => v,
                Err(e) => return e,
            };
            i += 5;

            if decoded_value >= FUTURE_SIGNAL_BASE {
                return DecodeError::UndefinedSignal { value: decoded_value };
            }
            if decoded_value >= FILL_SIGNAL_BASE {
                continue; // a Fill signal reads nothing and cannot fail
            }
            if decoded_value >= DP_SIGNAL_BASE {
                let (_profile, _mask, length) =
                    split_dp_payload(decoded_value - DP_SIGNAL_BASE);
                if i + length > n {
                    return DecodeError::UnexpectedEndOfStream;
                }
                // A donor never makes a character invalid that Alphabet-N
                // accepts, so membership is the only thing a segment can fail.
                if let Some(e) = first_invalid(&chars[i..i + length]) {
                    return e;
                }
                i += length;
            }
        } else {
            // Section 7.5: a trailing group of 2, 3, or 4 characters decodes to
            // 1, 2, or 3 bytes respectively. A single leftover character cannot
            // be a valid partial block.
            //
            // A lone trailing character has to be an Alphabet-N character
            // before its being alone is the complaint.
            //
            // Section 10 states both conditions and orders neither, and this
            // used to decide the length first, on the grounds that three
            // implementations did. Differential fuzzing showed that not to be
            // the state of things: this one already reported the invalid
            // character for a byte outside ASCII, because the scan rejects
            // those earlier, and the final block only for one inside it. So
            // the choice was never between two consistent rules -- and the
            // rule that needs no precedence caveat is that a character with
            // no digit value under Section 8 cannot be the trailing group
            // whose size is at issue. All four implementations now report
            // InvalidCharacter here.
            if remaining == 1 {
                let pc = chars[i];
                if char_to_value(pc.c).is_none() {
                    return DecodeError::InvalidCharacter {
                        character: pc.c,
                        position: pc.pos,
                    };
                }
                return DecodeError::InvalidFinalBlock { length: remaining };
            }

            let mut digits = [84u8; 5]; // pad with '#' (value 84)
            for k in 0..remaining {
                let pc = chars[i + k];
                match char_to_value(pc.c) {
                    Some(v) => digits[k] = v,
                    None => {
                        return DecodeError::InvalidCharacter {
                            character: pc.c,
                            position: pc.pos,
                        }
                    }
                }
            }
            let value = crate::digits::digits_to_value(&digits);
            if value >= DP_SIGNAL_BASE {
                return DecodeError::InvalidFinalBlock { length: remaining };
            }
            let produced = remaining - 1;
            let bytes = (value as u32).to_be_bytes();
            let mut padded = [0u8; 4];
            padded[..produced].copy_from_slice(&bytes[..produced]);
            let canonical = value_to_5chars_32(u32::from_be_bytes(padded));
            if canonical[..remaining]
                .iter()
                .zip(&chars[i..i + remaining])
                .any(|(&want, got)| want as char != got.c)
            {
                return DecodeError::InvalidFinalBlock { length: remaining };
            }
            i += remaining;
        }
    }

    // `scan` rejected this input, so one of the checks above must have fired.
    // Reaching here would mean the two tiers disagree about what is valid.
    unreachable!("the decoder rejected an input its error reporter accepts")
}

fn first_invalid(group: &[PosChar]) -> Option<DecodeError> {
    group.iter().find_map(|pc| {
        if char_to_value(pc.c).is_none() {
            Some(DecodeError::InvalidCharacter {
                character: pc.c,
                position: pc.pos,
            })
        } else {
            None
        }
    })
}

fn value_of_group(group: &[PosChar]) -> Result<u64, DecodeError> {
    debug_assert_eq!(group.len(), 5);
    let cs: Vec<char> = group.iter().map(|pc| pc.c).collect();
    match chars_to_value(&cs) {
        Some(v) => Ok(v),
        None => {
            Err(first_invalid(group).expect("chars_to_value failed but no invalid character found"))
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::alphabet::RSET_LEN;
    use crate::constants::{MAX_DP_SEGMENT_CHARS, MAX_FILL_BYTES};

    #[test]
    fn payload_fields_are_masked_to_their_widths() {
        // Every bit of a maximal DP payload lands in the field the spec assigns
        // it: 3 profile bits, 13 mask bits, 11 length bits.
        let payload = (7u64 << 24) | (0x1FFF << 11) | 0x7FF;
        assert_eq!(split_dp_payload(payload), (7, 0x1FFF, MAX_DP_SEGMENT_CHARS));
        assert_eq!(split_dp_payload(0), (0, 0, 1));
        assert_eq!(split_fill_payload(0), (0, 1));
        assert_eq!(
            split_fill_payload((0xFFu64 << 11) | 0x7FF),
            (0xFF, MAX_FILL_BYTES)
        );
        assert_eq!(RSET_LEN, 13);
    }
}
