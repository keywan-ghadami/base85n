/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

import { describe, expect, it } from "vitest";
import { decode } from "../src/index.js";
import { Base85NDecodeError } from "../src/errors.js";
import { BLOCK_VALUE_LIMIT, SIGNAL_PAYLOAD_MAX } from "../src/constants.js";
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

  it("decodes under every one of the eight alphabet identifiers", () => {
    const body = "^@%$?!~#abcdefghijkl";
    for (let a = 0; a < 8; a++) {
      const signal = valueToBase85Chars(BLOCK_VALUE_LIMIT + a * 1024 + (body.length - 1));
      expect(decode(signal + body).length).toBe(body.length);
    }
  });

  it("throws on a signal payload in the reserved range above 2^13-1", () => {
    expect(SIGNAL_PAYLOAD_MAX).toBe(2 ** 13 - 1);
    const signal = valueToBase85Chars(BLOCK_VALUE_LIMIT + SIGNAL_PAYLOAD_MAX + 1);
    expectDecodeError(signal, "reserved_signal_value");
  });

  it("throws on the maximum possible reserved payload (85^5 - 1)", () => {
    const signal = valueToBase85Chars(85 ** 5 - 1);
    expectDecodeError(signal, "reserved_signal_value");
  });

  it("throws on an invalid single-character trailing group", () => {
    // "vpA.2" is a full, valid 5-char block; one extra valid Alphabet-N char is left dangling.
    expectDecodeError("vpA.2v", "invalid_partial_block_length");
  });

  it("pins the partial-block padding boundary at 2^32", () => {
    // Spec 7.1: a trailing group is padded with '#' and the result must be below 2^32.
    // "%nSb" pads to 2^32 - 2, "%nSc" to 2^32 + 83 -- adjacent groups either side of the line.
    expect(Array.from(decode("%nSb"))).toEqual([0xff, 0xff, 0xff]);
    expectDecodeError("%nSc", "invalid_partial_block_length");
    // The 2- and 3-character forms take a different branch of the padding.
    expectDecodeError("##", "invalid_partial_block_length");
    expectDecodeError("###", "invalid_partial_block_length");
  });

  it("accepts the maximum valid signal payload (2^13 - 1, alphabet=7, length=1024)", () => {
    expect(SIGNAL_PAYLOAD_MAX).toBe(2 ** 13 - 1);
    const signal = valueToBase85Chars(BLOCK_VALUE_LIMIT + SIGNAL_PAYLOAD_MAX);
    const data = "a".repeat(1024);
    expect(Array.from(decode(signal + data))).toEqual(Array.from(data).map((c) => c.charCodeAt(0)));
  });

  it("does not silently return garbage for malformed input -- it always throws", () => {
    const malformedInputs = ["abcd&", "vpA.2" + String.fromCharCode(233)];
    for (const input of malformedInputs) {
      expect(() => decode(input)).toThrow(Base85NDecodeError);
    }
  });
});
