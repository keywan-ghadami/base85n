// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Decoding algorithm (spec section 7).
//!
//! Decoding runs in two tiers. [`scan`] does the work: it reads the input's
//! bytes directly, answers every question about a character with one packed
//! table lookup, and writes into a buffer sized once from the exact bound. It
//! reports *that* an input is malformed, not what is wrong with it.
//! [`report_error`] is the character-at-a-time implementation, kept for the
//! failure path, where it produces the error variant and position that
//! [`decode`] returns. Nothing but a rejected input pays for it.
//!
//! `scan` is written so that the compiler can discharge its bounds checks
//! rather than emit them. Two shapes do that, and they are the reason this
//! matches the C implementation's instruction count on binary input:
//!
//! - A run of standard block groups is a uniform map -- 5 characters in, 4
//!   bytes out, no decision that changes the shape. Sizing both sides to a
//!   whole number of groups up front lets `as_chunks` carry the lengths into
//!   the loop, whose body then indexes nothing.
//! - A DP segment's bound is local once both sides are narrowed to it: `len9`
//!   characters produce at most `len9` bytes, because an escape pair spends two
//!   characters on one byte and every other character spends one.
//!
//! What does not work, measured: expressing the same loop through a trait so
//! that one copy could also decode in place. Behind a `&mut`, the destination
//! pointer spills to the stack and is reloaded every group, which costs more
//! than the checks it saves (15-50% depending on input shape). Writing through
//! `Cell` to allow the aliasing safely costs more still, since it gives up
//! `noalias` and rules out `as_chunks`. So the whitespace retry allocates its
//! filtered copy instead of decoding in place -- see [`decode`].

use crate::alphabet::{
    char_to_value, replacement_index_for_byte, ALPHABET_VALUE, DEC_ESCAPE, DEC_INVALID, DEC_SUB,
    RSET_ASCII,
};
use crate::constants::{DP_SIGNAL_BASE, MAX_SIGNAL_PAYLOAD};
use crate::digits::{chars_to_value, POW85_2, POW85_3, POW85_4};
use crate::error::DecodeError;

fn is_ignorable_ws(c: u8) -> bool {
    c == b' ' || c == b'\t' || c == b'\n' || c == b'\r'
}

/// Decode `src`, which must already be free of the inter-token whitespace
/// section 7.1 allows, into `dst`, and return the number of bytes produced --
/// or `None` if the input is malformed.
///
/// `dst` must have room for `src.len()` bytes. The bound is exact: a
/// 5-character group yields 4 bytes and a DP segment at most 1 byte per
/// character, so no input character ever yields more than one byte.
fn scan(src: &[u8], dst: &mut [u8]) -> Option<usize> {
    let n = src.len();
    let mut pos = 0usize;
    let mut w = 0usize;

    while pos < n {
        let remaining = n - pos;

        if remaining >= 5 {
            // The run of block groups. `run` is limited by the input left and
            // the output room, so both slicings below are valid by construction
            // and are checked once for the whole run rather than once per group.
            let run = ((n - pos) / 5).min((dst.len() - w) / 4);
            if run > 0 {
                let (ins, _) = src[pos..pos + 5 * run].as_chunks::<5>();
                let (outs, _) = dst[w..w + 4 * run].as_chunks_mut::<4>();
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
                        + (v1 as u32 * POW85_3 + v2 as u32 * POW85_2 + v3 as u32 * 85
                            + v4 as u32) as u64;
                    if value >= DP_SIGNAL_BASE {
                        break; // a DP signal: the general path below takes it
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

            // A DP signal, or a group the run could not take. Decode it alone.
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
                dst[w..w + 4].copy_from_slice(&(decoded_value as u32).to_be_bytes());
                w += 4;
                continue;
            }

            let payload = decoded_value - DP_SIGNAL_BASE;
            if payload > MAX_SIGNAL_PAYLOAD {
                return None;
            }
            let mask13 = ((payload >> 9) as u32 & 0x1FFF) << 16;
            let len9 = (payload & 0x1FF) as usize;
            if n - pos < len9 || dst.len() - w < len9 {
                // The second half is unreachable for any stream -- `w <= pos`
                // and `len9 <= n - pos` -- but stating it here is what makes the
                // segment's bound local, and it costs one comparison per signal.
                return None;
            }

            // Section 7.1.e: one DEC_SUB lookup per character answers
            // membership, escaping and substitution together.
            let seg = &src[pos..pos + len9];
            let out_seg = &mut dst[w..w + len9];
            let mut i = 0usize;
            let mut o = 0usize;
            while i < len9 {
                let c = seg[i];
                let t = DEC_SUB[c as usize];
                i += 1;
                if t & (DEC_INVALID | DEC_ESCAPE) != 0 {
                    if t & DEC_INVALID != 0 {
                        return None;
                    }
                    // '~': the next character stands for itself.
                    if i >= len9 {
                        return None;
                    }
                    let c2 = seg[i];
                    i += 1;
                    if DEC_SUB[c2 as usize] & DEC_INVALID != 0 {
                        return None;
                    }
                    out_seg[o] = c2;
                    o += 1;
                    continue;
                }
                // A replacement character stands for its R-Set byte exactly
                // while the signal's mask says the window contained it.
                out_seg[o] = if t & mask13 != 0 { t as u8 } else { c };
                o += 1;
            }
            pos += len9;
            w += o;
            continue;
        }

        // Fewer than 5 characters remain: this must be the trailing partial
        // block for the whole stream (section 7.1, last bullet). A lone
        // character cannot be one, since 2 characters are the minimum for 1 byte.
        if remaining == 1 {
            return None;
        }

        let mut value: u64 = 0;
        for &c in &src[pos..pos + remaining] {
            let v = ALPHABET_VALUE[c as usize];
            if v < 0 {
                return None;
            }
            value = value * 85 + v as u64;
        }
        for _ in remaining..5 {
            value = value * 85 + 84; // pad with '#'
        }
        // Spec 7.1: the padded group's value must be below 2^32.
        if value >= DP_SIGNAL_BASE {
            return None;
        }
        let bytes = (value as u32).to_be_bytes();
        dst[w..w + remaining - 1].copy_from_slice(&bytes[..remaining - 1]);
        w += remaining - 1;
        pos += remaining;
    }

    Some(w)
}

/// Decode a Base85N string back into the original bytes.
pub fn decode(s: &str) -> Result<Vec<u8>, DecodeError> {
    let bytes = s.as_bytes();
    // One allocation, sized by the bound `scan` documents.
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
    //
    // The filtered copy is a second buffer, where the C implementation reuses
    // the output buffer and decodes in place. Doing that here needs the reader
    // and the writer to alias, which safe Rust expresses either through a `&mut`
    // abstraction or through `Cell` -- and both were measured to cost more on
    // *every* decode than this buffer costs on a decode that has already failed.
    // The peak is bounded at the input size plus the output size, and reaching
    // it requires the input to contain whitespace.
    if bytes.iter().any(|&c| is_ignorable_ws(c)) {
        let filtered: Vec<u8> = bytes.iter().copied().filter(|&c| !is_ignorable_ws(c)).collect();
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
/// broken and where, so `decode` keeps reporting the error variant and position
/// it always has -- positions being byte offsets into the original string, which
/// is why this walks the original rather than the filtered copy.
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

            if decoded_value >= DP_SIGNAL_BASE {
                let payload = decoded_value - DP_SIGNAL_BASE;
                if payload > MAX_SIGNAL_PAYLOAD {
                    return DecodeError::ReservedSignalValue { payload };
                }
                let mask13 = ((payload >> 9) & 0x1FFF) as u16;
                let len9 = (payload & 0x1FF) as usize;

                if i + len9 > n {
                    return DecodeError::UnexpectedEndOfStream;
                }
                if let Err(e) = check_dp_segment(&chars[i..i + len9], mask13) {
                    return e;
                }
                i += len9;
            }
        } else {
            // Section 7.1, final bullet: a trailing group of 2, 3, or 4
            // characters decodes to 1, 2, or 3 bytes respectively. A single
            // leftover character cannot be a valid partial block.
            // A lone trailing character is reported as a bad partial block even
            // when the character itself is not in Alphabet-N: the length is
            // decided first. (C, Go and Python agree; TypeScript reports the
            // invalid character instead. The spec does not order the two checks.)
            if remaining == 1 {
                return DecodeError::InvalidPartialBlock { length: remaining };
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
            // Spec 7.1: the padded group's value must be below 2^32. The encoder
            // truncates a group whose value already is, and re-padding with '#'
            // raises it by at most 614124, so a group that crosses 2^32 cannot
            // be this format's output. Reducing it modulo 2^32 instead would
            // accept several character sequences as encodings of the same bytes.
            if crate::digits::digits_to_value(&digits) >= DP_SIGNAL_BASE {
                return DecodeError::InvalidPartialBlock { length: remaining };
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
        None => Err(first_invalid(group)
            .expect("chars_to_value failed but no invalid character found")),
    }
}

/// Section 7.1.e's segment walk, for its errors only.
fn check_dp_segment(seg: &[PosChar], mask13: u16) -> Result<(), DecodeError> {
    let mut idx = 0usize;
    while idx < seg.len() {
        let pc = seg[idx];
        if char_to_value(pc.c).is_none() {
            return Err(DecodeError::InvalidCharacter {
                character: pc.c,
                position: pc.pos,
            });
        }

        if pc.c == '~' {
            idx += 1;
            if idx >= seg.len() {
                return Err(DecodeError::DanglingEscapeCharacter);
            }
            let pc2 = seg[idx];
            if char_to_value(pc2.c).is_none() {
                return Err(DecodeError::InvalidCharacter {
                    character: pc2.c,
                    position: pc2.pos,
                });
            }
            idx += 1;
        } else {
            // Kept for symmetry with `scan`: a replacement character in the mask
            // decodes to its R-Set byte, which is never an error.
            let _ = replacement_index_for_byte(pc.c as u8).map(|j| {
                if mask13 & (1 << j) != 0 {
                    RSET_ASCII[j as usize]
                } else {
                    pc.c as u8
                }
            });
            idx += 1;
        }
    }
    Ok(())
}
