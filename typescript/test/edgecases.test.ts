/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

import { describe, expect, it } from "vitest";
import {
  decode,
  encode,
  MAX_DP_ANALYSIS_BYTES,
  MAX_DP_OUTPUT_CHARS_PER_SIGNAL,
  MIN_PASSTHROUGH_BYTES,
  REPLACEMENT_ALPHABETS,
} from "../src/index.js";
import { R_SET_ASCII } from "../src/constants.js";

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

  it("uses exactly one DP signal at the 1024-byte window and two just above it", () => {
    expect(MAX_DP_OUTPUT_CHARS_PER_SIGNAL).toBe(1024);
    expect(MAX_DP_ANALYSIS_BYTES).toBe(1024);
    // All-literal bytes -> one output character per input byte.
    const literalCycle = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    function literalBytes(len: number): Uint8Array {
      const out = new Uint8Array(len);
      for (let i = 0; i < len; i++) out[i] = literalCycle.charCodeAt(i % literalCycle.length);
      return out;
    }

    const exact = literalBytes(1024);
    // One signal (5 chars) + 1024 data chars; DP is more compact than block mode.
    expect(encode(exact).length).toBe(5 + 1024);
    assertRoundTrip(exact);

    // 1025 bytes: a full window plus a single leftover byte, which block mode
    // spends 2 characters on.
    const over = literalBytes(1025);
    expect(encode(over).length).toBe(5 + 1024 + 2);
    assertRoundTrip(over);

    // Two full windows and a remainder, one signal each.
    const long = literalBytes(3000);
    expect(encode(long).length).toBe(3 * 5 + 3000);
    assertRoundTrip(long);
  });

  it("breaks a run at a literal donor character rather than mis-encoding it", () => {
    // A literal donor is representable under any alphabet that does not spend it.
    // With a space in the run, the alphabets that could carry the space all spend
    // '^' on it, so the run has to break at the '^'.
    const donors = new Set<string>();
    for (const subs of REPLACEMENT_ALPHABETS) for (const [, d] of subs) donors.add(d);

    for (const donor of donors) {
      const text = "a".repeat(25) + " " + donor + " " + "b".repeat(25);
      const data = new Uint8Array(text.length);
      for (let i = 0; i < text.length; i++) data[i] = text.charCodeAt(i);
      assertRoundTrip(data);
    }
  });

  it("carries each alphabet's own R-Set characters", () => {
    for (const subs of REPLACEMENT_ALPHABETS) {
      if (subs.length === 0) continue;
      const bytes: number[] = [];
      while (bytes.length < 3 * MIN_PASSTHROUGH_BYTES) {
        for (const [j] of subs) {
          bytes.push(R_SET_ASCII[j] as number);
          for (const c of "word") bytes.push(c.charCodeAt(0));
        }
      }
      assertRoundTrip(new Uint8Array(bytes));
    }
  });

  it("carries all 13 R-Set characters at once in a single segment", () => {
    // Only alphabet 7 substitutes all of them, so this run can only be carried
    // by that alphabet -- and must be, rather than falling back to block mode.
    const bytes: number[] = [];
    for (let i = 0; i < 3; i++) for (const a of R_SET_ASCII) bytes.push(a);
    const data = new Uint8Array(bytes);
    expect(encode(data).length).toBe(data.length + 5);
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
