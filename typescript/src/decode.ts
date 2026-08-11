/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * Base85N decoder. Implements spec Section 7 (General Decoding Principles) and
 * Section 10 (Error Handling).
 */
import {
  BLOCK_VALUE_LIMIT,
  CHAR_TO_VALUE,
  IGNORED_WHITESPACE,
  LENGTH_FIELD_DIVISOR,
  R_SET_ASCII,
  SIGNAL_PAYLOAD_MAX,
  replacementIndexForChar,
} from "./constants.js";
import { base85DigitsToValue, uint32ToBytesBE } from "./digits.js";
import { Base85NDecodeError } from "./errors.js";

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
 */
function decodeDpSegment(chars: readonly string[], segStart: number, length: number, mask: number): number[] {
  const decoded: number[] = [];
  let idx = 0;
  while (idx < length) {
    const absPos = segStart + idx;
    const char1 = chars[idx] as string;
    charValue(char1, absPos); // validate membership in Alphabet-N

    if (char1 === "~") {
      idx++;
      if (idx >= length) {
        throw new Base85NDecodeError(
          "dangling_escape_character",
          "escape character '~' at end of Dynamic Passthrough data segment",
          { position: segStart + idx },
        );
      }
      const char2 = chars[idx] as string;
      charValue(char2, segStart + idx); // must also be a valid Alphabet-N character
      decoded.push(char2.charCodeAt(0));
      idx++;
      continue;
    }

    const replIdx = replacementIndexForChar(char1);
    if (replIdx !== -1 && (mask & (1 << replIdx)) !== 0) {
      decoded.push(R_SET_ASCII[replIdx] as number);
      idx++;
      continue;
    }

    decoded.push(char1.charCodeAt(0));
    idx++;
  }
  return decoded;
}

/**
 * Decode a Base85N string into the original bytes.
 * @throws {Base85NDecodeError} if the input is not a valid Base85N encoding.
 */
export function decode(s: string): Uint8Array {
  // Section 7.1: whitespace (space, tab, LF, CR) between Base85N constructs is ignored.
  // Since every DP data segment consists exclusively of Alphabet-N characters (R-Set bytes
  // and the escape character are always substituted/escaped by the encoder), stripping
  // whitespace globally before parsing is equivalent to ignoring it only "between" constructs.
  const chars: string[] = [];
  for (const ch of s) {
    if (!IGNORED_WHITESPACE.has(ch)) {
      chars.push(ch);
    }
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
      const mask = Math.floor(signalPayload / LENGTH_FIELD_DIVISOR);
      const length = signalPayload % LENGTH_FIELD_DIVISOR;

      if (i + length > total) {
        throw new Base85NDecodeError(
          "unexpected_end_of_stream",
          `DP signal declares ${length} data characters but only ${total - i} remain in the stream`,
          { position: i },
        );
      }

      const segChars = chars.slice(i, i + length);
      const segBytes = decodeDpSegment(segChars, i, length, mask);
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

  return Uint8Array.from(out);
}
