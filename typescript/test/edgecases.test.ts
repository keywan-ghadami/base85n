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
  MAX_DP_SEGMENT_CHARS,
  MAX_FILL_BYTES,
  MIN_FILL_BYTES,
  MIN_PASSTHROUGH_BYTES,
  PROFILES,
  R_SET_ASCII,
} from "../src/index.js";

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

  it("uses exactly one DP signal at the 2048-byte window and two just above it", () => {
    expect(MAX_DP_SEGMENT_CHARS).toBe(2048);
    expect(MAX_DP_ANALYSIS_BYTES).toBe(2048);
    // All-literal bytes -> one output character per input byte.
    const literalCycle = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    function literalBytes(len: number): Uint8Array {
      const out = new Uint8Array(len);
      for (let i = 0; i < len; i++) out[i] = literalCycle.charCodeAt(i % literalCycle.length);
      return out;
    }

    const exact = literalBytes(2048);
    // One signal (5 chars) + 2048 data chars; DP is more compact than block mode.
    expect(encode(exact).length).toBe(5 + 2048);
    assertRoundTrip(exact);

    // 2049 bytes: a full window plus a single leftover byte, which block mode
    // spends 2 characters on.
    const over = literalBytes(2049);
    expect(encode(over).length).toBe(5 + 2048 + 2);
    assertRoundTrip(over);

    // Two full windows and a remainder, one signal each.
    const long = literalBytes(5000);
    expect(encode(long).length).toBe(3 * 5 + 5000);
    assertRoundTrip(long);
  });

  it("keeps a literal donor character distinct from the R-Set byte it could stand for", () => {
    // A literal donor is representable while the segment does not spend it. With
    // a space in the run every profile pays for the space with its rank-0 donor,
    // so the scan either moves to a profile that ranks the literal beyond k, or
    // breaks the segment.
    const donors = new Set<string>();
    for (const profile of PROFILES) for (const d of profile) donors.add(d);

    for (const donor of donors) {
      const text = "a".repeat(25) + " " + donor + " " + "b".repeat(25);
      const data = new Uint8Array(text.length);
      for (let i = 0; i < text.length; i++) data[i] = text.charCodeAt(i);
      assertRoundTrip(data);
    }
  });

  it("carries every R-Set character", () => {
    for (const ascii of R_SET_ASCII) {
      const bytes: number[] = [];
      while (bytes.length < 3 * MIN_PASSTHROUGH_BYTES) {
        for (const c of "word") bytes.push(c.charCodeAt(0));
        bytes.push(ascii);
      }
      assertRoundTrip(new Uint8Array(bytes));
    }
  });

  it("carries a run of identical bytes in one Fill signal", () => {
    for (const byte of [0, 0x20, 0x61, 0xff]) {
      // One below the threshold is block mode; at it and at the cap, one signal
      // carries the whole run.
      expect(encode(new Uint8Array(MIN_FILL_BYTES - 1).fill(byte)).length).toBe(5);
      for (const n of [MIN_FILL_BYTES, MIN_FILL_BYTES + 1, MAX_FILL_BYTES]) {
        const data = new Uint8Array(n).fill(byte);
        expect(encode(data).length).toBe(5);
        assertRoundTrip(data);
      }
      // One past the cap needs a second signal for the leftover byte.
      const over = new Uint8Array(MAX_FILL_BYTES + 1).fill(byte);
      expect(encode(over).length).toBe(7);
      assertRoundTrip(over);
    }
  });

  it("breaks a passthrough segment around a long run", () => {
    const varied = (n: number) =>
      Array.from({ length: n }, (_, i) => "abcdefghijklmnopqrstuvwxyz".charCodeAt(i % 26));
    const bytes = [...varied(40), ...new Array<number>(300).fill(0x3d), ...varied(40)];
    const data = new Uint8Array(bytes);
    // 5+40 for the first segment, 5 for the run, 5+40 for the second.
    expect(encode(data).length).toBe(5 + 40 + 5 + 5 + 40);
    assertRoundTrip(data);
  });

  it("carries all 13 R-Set characters at once in a single segment", () => {
    // k reaches 13, so a whole profile is spent and the segment can hold no
    // literal from it -- but it is still one segment.
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
