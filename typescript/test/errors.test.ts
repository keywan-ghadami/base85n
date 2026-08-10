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
    // mask=0, length=10, but only 3 characters follow.
    const signal = valueToBase85Chars(BLOCK_VALUE_LIMIT + 10);
    expectDecodeError(signal + "abc", "unexpected_end_of_stream");
  });

  it("throws on a dangling escape character at the end of a DP segment", () => {
    // mask=0, length=1, single data character is '~' itself -> dangling.
    const signal = valueToBase85Chars(BLOCK_VALUE_LIMIT + 1);
    expectDecodeError(signal + "~", "dangling_escape_character");
  });

  it("throws on a dangling escape character when '~' is the last of a longer DP segment", () => {
    // mask=0, length=4, data = "ab~" + nothing after the trailing '~'.
    const signal = valueToBase85Chars(BLOCK_VALUE_LIMIT + 3);
    expectDecodeError(signal + "ab~", "dangling_escape_character");
  });

  it("throws on a signal payload in the reserved range above 2^22-1", () => {
    expect(SIGNAL_PAYLOAD_MAX).toBe(2 ** 22 - 1);
    const signal = valueToBase85Chars(BLOCK_VALUE_LIMIT + SIGNAL_PAYLOAD_MAX + 1);
    expectDecodeError(signal, "reserved_signal_value");
  });

  it("throws on the maximum possible reserved payload (85^5 - 1 - 2^32)", () => {
    const signal = valueToBase85Chars(85 ** 5 - 1);
    expectDecodeError(signal, "reserved_signal_value");
  });

  it("throws on an invalid single-character trailing group", () => {
    // "vpA.2" is a full, valid 5-char block; one extra valid Alphabet-N char is left dangling.
    expectDecodeError("vpA.2v", "invalid_partial_block_length");
  });

  it("accepts a zero-length DP segment (mask set, no data characters follow)", () => {
    // mask = 0x1FFF (all 13 R-Set bits set), length = 0.
    const signal = valueToBase85Chars(BLOCK_VALUE_LIMIT + 0x1fff * 512);
    expect(Array.from(decode(signal))).toEqual([]);
  });

  it("accepts the maximum valid signal payload (2^22 - 1, mask=0x1FFF, length=511)", () => {
    expect(SIGNAL_PAYLOAD_MAX).toBe(2 ** 22 - 1);
    const signal = valueToBase85Chars(BLOCK_VALUE_LIMIT + SIGNAL_PAYLOAD_MAX);
    const data = "a".repeat(511);
    expect(Array.from(decode(signal + data))).toEqual(Array.from(data).map((c) => c.charCodeAt(0)));
  });

  it("does not silently return garbage for malformed input -- it always throws", () => {
    const malformedInputs = ["abcd&", "vpA.2" + String.fromCharCode(233)];
    for (const input of malformedInputs) {
      expect(() => decode(input)).toThrow(Base85NDecodeError);
    }
  });
});
