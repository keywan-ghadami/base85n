/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * Base85N decoder. Implements spec Section 7 (General Decoding Principles) and
 * Section 10 (Error Handling).
 *
 * Decoding runs in two tiers. `scan` below does the work: it reads the input
 * string directly through `charCodeAt`, answers every question about a
 * character with one typed-array lookup, and writes into a buffer sized once
 * from the exact bound. It reports *that* an input is malformed, not what is
 * wrong with it. `decodeReportingErrors` is the original character-at-a-time
 * implementation, kept verbatim for the failure path, where it produces the
 * error code and position that `decode` throws. Nothing but a rejected input
 * pays for it.
 */
import {
  BLOCK_VALUE_LIMIT,
  CHAR_TO_VALUE,
  DEC_INVALID,
  DEC_XLAT,
  IGNORED_WHITESPACE,
  LENGTH_FIELD_DIVISOR,
  POW85_2,
  POW85_3,
  POW85_4,
  R_SET_ASCII,
  SIGNAL_PAYLOAD_MAX,
  VALUE_BY_CHAR_CODE,
} from "./constants.js";
import { base85DigitsToValue, uint32ToBytesBE } from "./digits.js";
import { Base85NDecodeError } from "./errors.js";

/** Signals "this input is malformed"; the caller re-runs the reporting path. */
const REJECTED = -1;

/**
 * Decode `s`, which must already be free of the inter-token whitespace Section 7.1
 * allows, into `out`, and return the number of bytes produced -- or REJECTED if the
 * input is malformed. `out` must have room for `s.length` bytes: a 5-character group
 * yields 4 bytes and a DP segment at most 1 byte per character, so no input character
 * ever yields more than one byte, and the loop never has to test capacity.
 */
function scan(s: string, out: Uint8Array): number {
  const n = s.length;
  let i = 0;
  let w = 0;

  while (i < n) {
    const remaining = n - i;

    if (remaining >= 5) {
      // Every Alphabet-N character is ASCII, so anything at or above 128 is
      // rejected without a lookup. VALUE_BY_CHAR_CODE holds -1 for the rest,
      // which makes one sign test on the OR cover all five characters.
      const c0 = s.charCodeAt(i), c1 = s.charCodeAt(i + 1), c2 = s.charCodeAt(i + 2);
      const c3 = s.charCodeAt(i + 3), c4 = s.charCodeAt(i + 4);
      if ((c0 | c1 | c2 | c3 | c4) >= 128) return REJECTED;
      const v0 = VALUE_BY_CHAR_CODE[c0] as number;
      const v1 = VALUE_BY_CHAR_CODE[c1] as number;
      const v2 = VALUE_BY_CHAR_CODE[c2] as number;
      const v3 = VALUE_BY_CHAR_CODE[c3] as number;
      const v4 = VALUE_BY_CHAR_CODE[c4] as number;
      if ((v0 | v1 | v2 | v3 | v4) < 0) return REJECTED;

      // Weighing the digits directly keeps them independent, where Horner's rule
      // would chain five multiplies. All of this stays well inside 2^53.
      const decodedValue = v0 * POW85_4 + (v1 * POW85_3 + v2 * POW85_2 + v3 * 85 + v4);
      i += 5;

      if (decodedValue < BLOCK_VALUE_LIMIT) {
        // Standard Base85N block: 4 bytes, Big-Endian.
        out[w] = (decodedValue / 16777216) & 0xff;
        out[w + 1] = (decodedValue / 65536) & 0xff;
        out[w + 2] = (decodedValue / 256) & 0xff;
        out[w + 3] = decodedValue & 0xff;
        w += 4;
        continue;
      }

      const signalPayload = decodedValue - BLOCK_VALUE_LIMIT;
      if (signalPayload > SIGNAL_PAYLOAD_MAX) return REJECTED;
      const alphabet = Math.floor(signalPayload / LENGTH_FIELD_DIVISOR);
      // Section 9: the length field is stored biased by one.
      const length = (signalPayload % LENGTH_FIELD_DIVISOR) + 1;
      if (i + length > n) return REJECTED;

      // Section 7.1.e: one DEC_XLAT lookup per character answers membership and
      // substitution together, and the two sides are the same length.
      const xlat = DEC_XLAT[alphabet] as Uint16Array;
      const end = i + length;
      while (i < end) {
        const c = s.charCodeAt(i);
        i++;
        if (c >= 128) return REJECTED;
        const t = xlat[c] as number;
        if ((t & DEC_INVALID) !== 0) return REJECTED;
        out[w] = t & 0xff;
        w++;
      }
      continue;
    }

    // Fewer than 5 characters remain: this must be the trailing partial final block.
    if (remaining === 1) return REJECTED;

    let value = 0;
    for (let k = 0; k < remaining; k++) {
      const c = s.charCodeAt(i + k);
      if (c >= 128) return REJECTED;
      const v = VALUE_BY_CHAR_CODE[c] as number;
      if (v < 0) return REJECTED;
      value = value * 85 + v;
    }
    for (let k = remaining; k < 5; k++) value = value * 85 + 84; // '#' == value 84
    if (value >= BLOCK_VALUE_LIMIT) return REJECTED;

    const outputByteCount = remaining - 1; // 2/3/4 chars -> 1/2/3 bytes
    if (outputByteCount > 0) out[w] = (value / 16777216) & 0xff;
    if (outputByteCount > 1) out[w + 1] = (value / 65536) & 0xff;
    if (outputByteCount > 2) out[w + 2] = (value / 256) & 0xff;
    w += outputByteCount;
    i += remaining;
  }

  return w;
}

function isIgnorableWhitespace(code: number): boolean {
  return code === 32 || code === 9 || code === 10 || code === 13;
}

/**
 * Decode a Base85N string into the original bytes.
 * @throws {Base85NDecodeError} if the input is not a valid Base85N encoding.
 */
export function decode(s: string): Uint8Array {
  const out = new Uint8Array(s.length);
  let produced = scan(s, out);

  if (produced === REJECTED) {
    // Section 7.1 has the decoder ignore inter-token whitespace. Rather than copy every
    // input to strip characters a valid stream never contains, take the rejection as the
    // signal: none of the four whitespace characters is in Alphabet-N and every consumed
    // character is validated, so a stream with whitespace in it can never decode. Only
    // once it has failed is it worth building the filtered copy and decoding again.
    //
    // The retry is on any failure, not just an invalid character: whitespace also shifts
    // the group boundaries after it, so it can equally well surface as a truncated final
    // group or a short DP segment.
    let firstWs = 0;
    while (firstWs < s.length && !isIgnorableWhitespace(s.charCodeAt(firstWs))) firstWs++;
    if (firstWs === s.length) {
      return decodeReportingErrors(s); // no whitespace to blame: report and throw
    }
    let clean = s.slice(0, firstWs); // everything before the first whitespace copies wholesale
    for (let k = firstWs; k < s.length; k++) {
      if (!isIgnorableWhitespace(s.charCodeAt(k))) clean += s[k];
    }
    produced = scan(clean, out);
    if (produced === REJECTED) return decodeReportingErrors(clean);
  }

  return out.slice(0, produced);
}

/** Look up a character's Alphabet-N value, throwing Base85NDecodeError if it isn't one. */
function charValue(ch: string, position: number): number {
  const v = CHAR_TO_VALUE.get(ch);
  if (v === undefined) {
    throw new Base85NDecodeError(
      "invalid_character",
      `character ${JSON.stringify(ch)} is not part of Alphabet-N`,
      { position },
    );
  }
  return v;
}

/**
 * Decode a DP transformed_DP_data segment (Section 7.1.e) back into original bytes.
 * `segStart` is the absolute index (into `chars`) of the first character of the segment,
 * used only for error position reporting.
 *
 * One character in, one byte out: version 0.3.0's replacement alphabets are injective,
 * so there is no escape pair and no state to carry between characters.
 */
function decodeDpSegment(chars: readonly string[], segStart: number, length: number, alphabet: number): number[] {
  const xlat = DEC_XLAT[alphabet] as Uint16Array;
  const decoded: number[] = [];
  for (let idx = 0; idx < length; idx++) {
    const char1 = chars[idx] as string;
    charValue(char1, segStart + idx); // validate membership in Alphabet-N
    decoded.push((xlat[char1.charCodeAt(0)] as number) & 0xff);
  }
  return decoded;
}

/**
 * The character-at-a-time decoder, run only after `scan` has rejected the input. It
 * applies exactly the same rules and exists to say *which* rule was broken and where,
 * so `decode` keeps reporting the same error code and position it always has.
 *
 * `s` must already be free of inter-token whitespace, so positions are offsets into the
 * stripped stream, as documented. It iterates by code point, so a non-BMP character
 * counts as one position -- and is rejected, since Alphabet-N is ASCII.
 */
function decodeReportingErrors(s: string): never {
  const chars: string[] = [];
  for (const ch of s) {
    if (!IGNORED_WHITESPACE.has(ch)) chars.push(ch);
  }

  const out: number[] = [];
  let i = 0;
  const total = chars.length;

  while (i < total) {
    const remaining = total - i;

    if (remaining >= 5) {
      const digits: number[] = [
        charValue(chars[i] as string, i),
        charValue(chars[i + 1] as string, i + 1),
        charValue(chars[i + 2] as string, i + 2),
        charValue(chars[i + 3] as string, i + 3),
        charValue(chars[i + 4] as string, i + 4),
      ];
      const decodedValue = base85DigitsToValue(digits);
      const groupStart = i;
      i += 5;

      if (decodedValue < BLOCK_VALUE_LIMIT) {
        // Standard Base85N block.
        const bytes = uint32ToBytesBE(decodedValue);
        out.push(bytes[0], bytes[1], bytes[2], bytes[3]);
        continue;
      }

      // Dynamic Passthrough (DP) signal.
      const signalPayload = decodedValue - BLOCK_VALUE_LIMIT;
      if (signalPayload > SIGNAL_PAYLOAD_MAX) {
        throw new Base85NDecodeError(
          "reserved_signal_value",
          `DP signal payload ${signalPayload} exceeds the valid range 0..${SIGNAL_PAYLOAD_MAX}`,
          { position: groupStart },
        );
      }
      const alphabet = Math.floor(signalPayload / LENGTH_FIELD_DIVISOR);
      // Section 9: the length field is stored biased by one.
      const length = (signalPayload % LENGTH_FIELD_DIVISOR) + 1;

      if (i + length > total) {
        throw new Base85NDecodeError(
          "unexpected_end_of_stream",
          `DP signal declares ${length} data characters but only ${total - i} remain in the stream`,
          { position: i },
        );
      }

      const segChars = chars.slice(i, i + length);
      const segBytes = decodeDpSegment(segChars, i, length, alphabet);
      for (const b of segBytes) out.push(b);
      i += length;
      continue;
    }

    // Fewer than 5 characters remain: this must be a valid partial final block (Section 7.1's
    // last bullet), i.e. exactly 2, 3, or 4 characters at the very end of the stream. Anything
    // else (remaining === 1, or a value that overruns due to bad chars) is an error.
    if (remaining === 1) {
      // Validate the single leftover char first so a genuinely invalid character is reported
      // as such rather than masked by the partial-length error.
      charValue(chars[i] as string, i);
      throw new Base85NDecodeError(
        "invalid_partial_block_length",
        "a trailing group of exactly 1 Alphabet-N character cannot form a valid partial final block",
        { position: i },
      );
    }

    // remaining is 2, 3, or 4.
    const realDigits: number[] = [];
    for (let k = 0; k < remaining; k++) {
      realDigits.push(charValue(chars[i + k] as string, i + k));
    }
    const padCount = 5 - remaining;
    const paddedDigits = realDigits.concat(new Array<number>(padCount).fill(84)); // '#' == value 84
    const value = base85DigitsToValue(paddedDigits);
    if (value >= BLOCK_VALUE_LIMIT) {
      // Spec 7.1: the padded group's value must be below 2^32. The encoder truncates a group
      // whose value already is, and re-padding with '#' raises it by at most 614124, so a group
      // that crosses 2^32 cannot be this format's output. Reducing it modulo 2^32 instead would
      // accept several character sequences as encodings of the same bytes.
      throw new Base85NDecodeError(
        "invalid_partial_block_length",
        `a trailing group of ${remaining} characters pads to ${value}, which is not below 2^32`,
        { position: i },
      );
    }
    const bytes = uint32ToBytesBE(value);
    const outputByteCount = remaining - 1; // 2/3/4 chars -> 1/2/3 bytes
    for (let k = 0; k < outputByteCount; k++) {
      out.push(bytes[k] as number);
    }
    i += remaining;
  }

  // `scan` rejected this input, so one of the checks above must have thrown. Reaching
  // here would mean the two tiers disagree about what is valid.
  throw new Base85NDecodeError(
    "invalid_character",
    "internal error: the decoder rejected an input its error reporter accepts",
    { position: 0 },
  );
}
