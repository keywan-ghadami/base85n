import { describe, expect, it } from "vitest";
import { decode, encode, MAX_DP_OUTPUT_CHARS_PER_SIGNAL, MIN_PASSTHROUGH_BYTES } from "../src/index.js";

function assertRoundTrip(data: Uint8Array): void {
  const encoded = encode(data);
  const decoded = decode(encoded);
  expect(Array.from(decoded)).toEqual(Array.from(data));
}

describe("edge cases", () => {
  it("handles empty input", () => {
    expect(encode(new Uint8Array(0))).toBe("");
    expect(Array.from(decode(""))).toEqual([]);
  });

  it("handles lengths 1, 2, 3, 4 (partial block boundaries)", () => {
    for (let len = 1; len <= 4; len++) {
      const data = new Uint8Array(len);
      for (let i = 0; i < len; i++) data[i] = (0x10 + i * 0x11) & 0xff;
      assertRoundTrip(data);
      // Block-mode partial groups are 2/3/4 chars for 1/2/3 trailing bytes; a 4-byte input
      // is a full block (5 chars) rather than a "partial" group.
      const encoded = encode(data);
      const expectedLen = len === 4 ? 5 : len + 1;
      expect(encoded.length).toBe(expectedLen);
    }
  });

  it("handles input length exactly at MIN_PASSTHROUGH_BYTES and one below/above", () => {
    expect(MIN_PASSTHROUGH_BYTES).toBe(20);
    for (const len of [MIN_PASSTHROUGH_BYTES - 1, MIN_PASSTHROUGH_BYTES, MIN_PASSTHROUGH_BYTES + 1]) {
      const data = new Uint8Array(len);
      for (let i = 0; i < len; i++) data[i] = "0123456789abcdefghij".charCodeAt(i % 20);
      assertRoundTrip(data);
    }
  });

  it("uses exactly one DP signal at the 511-char boundary and two just above it", () => {
    expect(MAX_DP_OUTPUT_CHARS_PER_SIGNAL).toBe(511);
    // All-literal bytes -> transformed_length === byte length (no escaping needed).
    const literalCycle = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    function literalBytes(len: number): Uint8Array {
      const out = new Uint8Array(len);
      for (let i = 0; i < len; i++) out[i] = literalCycle.charCodeAt(i % literalCycle.length);
      return out;
    }

    const exact511 = literalBytes(511);
    const encoded511 = encode(exact511);
    // One signal (5 chars) + 511 data chars, no fallback to block mode since DP is more compact.
    expect(encoded511.length).toBe(5 + 511);
    assertRoundTrip(exact511);

    const over511 = literalBytes(600);
    const encodedOver = encode(over511);
    // Must require 2 DP signals (511 + 89 chars): ceil(600/511) = 2 signals.
    expect(encodedOver.length).toBe(2 * 5 + 600);
    assertRoundTrip(over511);
  });

  it("triggers the MAX_CONSECUTIVE_ESCAPES(=3) scan-termination heuristic mid-stream", () => {
    // 30 plain literal bytes, then 4 consecutive '~' (escape) bytes -- the 4th exceeds
    // MAX_CONSECUTIVE_ESCAPES and forces the DP scan to terminate before it -- then 30 more
    // plain literal bytes that must still round-trip correctly (likely via a later block/DP
    // segment or a subsequent loop iteration).
    const prefix = new Uint8Array(30);
    for (let i = 0; i < 30; i++) prefix[i] = "abcdefghijklmnopqrstuvwxyz012345".charCodeAt(i % 26);
    const tildes = new Uint8Array(4).fill(0x7e); // '~'
    const suffix = new Uint8Array(30);
    for (let i = 0; i < 30; i++) suffix[i] = "ZYXWVUTSRQPONMLKJIHGFEDCBA987654".charCodeAt(i % 26);

    const data = new Uint8Array(prefix.length + tildes.length + suffix.length);
    data.set(prefix, 0);
    data.set(tildes, prefix.length);
    data.set(suffix, prefix.length + tildes.length);

    assertRoundTrip(data);
  });

  it("round-trips every byte value 0-255", () => {
    const data = new Uint8Array(256);
    for (let i = 0; i < 256; i++) data[i] = i;
    assertRoundTrip(data);
  });

  it("round-trips every byte value 0-255 repeated (bigger, still exhaustive)", () => {
    const data = new Uint8Array(256 * 4);
    for (let i = 0; i < data.length; i++) data[i] = i % 256;
    assertRoundTrip(data);
  });
});
