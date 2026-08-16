/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

import { describe, expect, it } from "vitest";
import { decode, encode } from "../src/index.js";
import { Base85NDecodeError } from "../src/errors.js";
import {
  BLOCK_VALUE_LIMIT,
  FILL_SIGNAL_BASE,
  FUTURE_SIGNAL_BASE,
  LENGTH_FIELD_DIVISOR,
  MASK_FIELD_DIVISOR,
  MAX_DP_SEGMENT_CHARS,
  MAX_FILL_BYTES,
  MAX_TAIL_ZEROS,
  TAIL_SIGNAL_BASE,
} from "../src/constants.js";
import { valueToBase85Chars } from "../src/digits.js";

function expectDecodeError(input: string, code: Base85NDecodeError["code"]): void {
  let thrown: unknown;
  try {
    decode(input);
  } catch (e) {
    thrown = e;
  }
  expect(thrown, `expected decode(${JSON.stringify(input)}) to throw`).toBeInstanceOf(Base85NDecodeError);
  expect((thrown as Base85NDecodeError).code).toBe(code);
}

describe("decode error handling", () => {
  it("throws on an invalid character (not part of Alphabet-N) in a full 5-char group", () => {
    // '&' (ASCII 38) is an R-Set character but is deliberately excluded from Alphabet-N itself.
    expectDecodeError("abcd&", "invalid_character");
  });

  it("throws on an invalid character in a trailing partial group", () => {
    expectDecodeError("ab&", "invalid_character");
  });

  it("throws when a DP signal declares more data characters than remain in the stream", () => {
    // alphabet=0, length=10 (stored as 9), but only 3 characters follow.
    const signal = valueToBase85Chars(BLOCK_VALUE_LIMIT + 9);
    expectDecodeError(signal + "abc", "unexpected_end_of_stream");
  });

  it("reads the length field biased by one", () => {
    // Section 9: the stored value is length - 1, so the smallest segment a signal
    // can name is one character. A decoder that forgets the bias reads nothing
    // here and then misparses whatever follows.
    const signal = valueToBase85Chars(BLOCK_VALUE_LIMIT + 0);
    expect(Array.from(decode(signal + "a"))).toEqual(["a".charCodeAt(0)]);
    expectDecodeError(signal, "unexpected_end_of_stream");
  });

  it("decodes under every one of the eight profile identifiers", () => {
    const body = "^@%$?!~#abcdefghijkl";
    for (let p = 0; p < 8; p++) {
      const payload =
        p * MASK_FIELD_DIVISOR * LENGTH_FIELD_DIVISOR +
        0x1fff * LENGTH_FIELD_DIVISOR +
        (body.length - 1);
      const signal = valueToBase85Chars(BLOCK_VALUE_LIMIT + payload);
      expect(decode(signal + body).length).toBe(body.length);
    }
  });

  it("throws on a group value in FUTURE_SIGNAL_SPACE", () => {
    expect(FUTURE_SIGNAL_BASE).toBe(2 ** 32 + 2 ** 27 + 2 ** 19 + 2 ** 22);
    expectDecodeError(valueToBase85Chars(FUTURE_SIGNAL_BASE), "undefined_signal");
  });

  it("throws on the maximum possible group value (85^5 - 1)", () => {
    expectDecodeError(valueToBase85Chars(85 ** 5 - 1), "undefined_signal");
  });

  it("throws on an invalid single-character trailing group", () => {
    // "vpA.2" is a full, valid 5-char block; one extra valid Alphabet-N char is left dangling.
    expectDecodeError("vpA.2v", "invalid_final_block");
  });

  it("pins the final-block padding boundary at 2^32", () => {
    // Section 7.5: a trailing group is padded with '#' and the result must be below
    // 2^32. "%nSc" pads to 2^32 + 83, past the line.
    expectDecodeError("%nSc", "invalid_final_block");
    // The 2- and 3-character forms take a different branch of the padding.
    expectDecodeError("##", "invalid_final_block");
    expectDecodeError("###", "invalid_final_block");
  });

  it("rejects a final block that is not the canonical encoding of its bytes", () => {
    // Section 7.5: "%nSb" pads to 2^32 - 2 and would yield ff ff ff, but those
    // bytes encode as something else, so it is an alias and is rejected.
    expectDecodeError("%nSb", "invalid_final_block");
    expect(Array.from(decode(encode(new Uint8Array([0xff, 0xff, 0xff]))))).toEqual([
      0xff, 0xff, 0xff,
    ]);
  });

  it("accepts the maximum valid DP signal (profile 7, full mask, 2048 characters)", () => {
    const payload =
      7 * MASK_FIELD_DIVISOR * LENGTH_FIELD_DIVISOR +
      0x1fff * LENGTH_FIELD_DIVISOR +
      (MAX_DP_SEGMENT_CHARS - 1);
    expect(BLOCK_VALUE_LIMIT + payload).toBe(FILL_SIGNAL_BASE - 1);
    const signal = valueToBase85Chars(BLOCK_VALUE_LIMIT + payload);
    const data = "a".repeat(MAX_DP_SEGMENT_CHARS);
    expect(Array.from(decode(signal + data))).toEqual(Array.from(data).map((c) => c.charCodeAt(0)));
  });

  it("expands a Fill signal without reading any character", () => {
    // The first solid Fill signal is one byte 0x00; the last is 2048 of 0xff.
    expect(Array.from(decode(valueToBase85Chars(FILL_SIGNAL_BASE)))).toEqual([0]);
    const last = decode(valueToBase85Chars(TAIL_SIGNAL_BASE - 1));
    expect(last.length).toBe(MAX_FILL_BYTES);
    expect(last.every((b) => b === 0xff)).toBe(true);
    // Five characters can name 2048 bytes and no more (Section 13).
    expect(last.length / 5).toBeLessThanOrEqual(MAX_FILL_BYTES / 5);

    // The tail variant's two ends: one zero with two NUL literals, and two
    // 0xff literals ahead of MAX_TAIL_ZEROS zeros.
    expect(Array.from(decode(valueToBase85Chars(TAIL_SIGNAL_BASE)))).toEqual([0, 0, 0]);
    const lastTail = decode(valueToBase85Chars(FUTURE_SIGNAL_BASE - 1));
    expect(Array.from(lastTail)).toEqual([
      0xff,
      0xff,
      ...new Array<number>(MAX_TAIL_ZEROS).fill(0),
    ]);
  });

  it("does not silently return garbage for malformed input -- it always throws", () => {
    const malformedInputs = ["abcd&", "vpA.2" + String.fromCharCode(233)];
    for (const input of malformedInputs) {
      expect(() => decode(input)).toThrow(Base85NDecodeError);
    }
  });
});
