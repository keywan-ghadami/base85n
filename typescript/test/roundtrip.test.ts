/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

import { describe, expect, it } from "vitest";
import { decode, encode, PROFILES } from "../src/index.js";
import { mulberry32, randInt, randomBytes } from "./helpers.js";

const ALPHABET_N_CHARS =
  "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ.-:+=^!/*?`_~()[]{}@%$#";
const R_SET_CHARS = [32, 34, 39, 44, 59, 92, 124, 60, 62, 38, 9, 10, 13]; // space " ' , ; \ | < > & \t \n \r
/** Every character any donor profile can spend as a stand-in (spec 4.2). */
const DONOR_CHARS = Array.from(new Set(PROFILES.join(""))).map((c) => c.charCodeAt(0));

function randomAlphabetNByte(rng: () => number): number {
  const ch = ALPHABET_N_CHARS[randInt(rng, ALPHABET_N_CHARS.length)] as string;
  return ch.charCodeAt(0);
}

function randomRSetByte(rng: () => number): number {
  return R_SET_CHARS[randInt(rng, R_SET_CHARS.length)] as number;
}

/**
 * Build a random byte sequence mixing raw random bytes, Alphabet-N literal bytes,
 * R-Set characters, and donor characters, per the requested distribution.
 */
function randomMixedBytes(rng: () => number, length: number): Uint8Array {
  const out = new Uint8Array(length);
  for (let i = 0; i < length; i++) {
    const kind = randInt(rng, 10);
    if (kind < 4) {
      out[i] = randInt(rng, 256); // raw random byte
    } else if (kind < 7) {
      out[i] = randomAlphabetNByte(rng); // Alphabet-N literal byte
    } else if (kind < 9) {
      out[i] = randomRSetByte(rng); // R-Set character
    } else {
      out[i] = DONOR_CHARS[randInt(rng, DONOR_CHARS.length)] as number; // donor character
    }
  }
  return out;
}

function assertRoundTrip(data: Uint8Array, label: string): void {
  const encoded = encode(data);
  const decoded = decode(encoded);
  expect(Array.from(decoded), `${label}: round-trip mismatch`).toEqual(Array.from(data));
}

describe("round-trip property tests (seeded PRNG)", () => {
  const lengths = [
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 15, 19, 20, 21, 25, 30, 50, 63, 100, 127, 128, 200, 255, 256,
    300, 500, 511, 512, 513, 600, 999, 1000, 1024, 2000, 3000,
  ];

  it("round-trips mixed random content across many lengths and seeds", () => {
    const rng = mulberry32(0xc0ffee);
    let trials = 0;
    for (const len of lengths) {
      for (let trial = 0; trial < 5; trial++) {
        const data = randomMixedBytes(rng, len);
        assertRoundTrip(data, `mixed len=${len} trial=${trial}`);
        trials++;
      }
    }
    expect(trials).toBeGreaterThan(100);
  });

  it("round-trips pure random binary content across many lengths and seeds", () => {
    const rng = mulberry32(0x5eed5eed);
    for (const len of lengths) {
      for (let trial = 0; trial < 3; trial++) {
        const data = randomBytes(rng, len);
        assertRoundTrip(data, `random len=${len} trial=${trial}`);
      }
    }
  });

  it("round-trips pure Alphabet-N literal content (heavy DP-mode usage)", () => {
    const rng = mulberry32(12345);
    for (const len of lengths) {
      const out = new Uint8Array(len);
      for (let i = 0; i < len; i++) out[i] = randomAlphabetNByte(rng);
      assertRoundTrip(out, `literal len=${len}`);
    }
  });

  it("round-trips content dense with R-Set characters", () => {
    const rng = mulberry32(999);
    for (const len of lengths) {
      const out = new Uint8Array(len);
      for (let i = 0; i < len; i++) {
        out[i] = rng() < 0.5 ? randomRSetByte(rng) : randomAlphabetNByte(rng);
      }
      assertRoundTrip(out, `rset-dense len=${len}`);
    }
  });

  it("round-trips content dense with donor characters", () => {
    const rng = mulberry32(424242);
    for (const len of lengths) {
      const out = new Uint8Array(len);
      for (let i = 0; i < len; i++) {
        out[i] =
          rng() < 0.4
            ? (DONOR_CHARS[randInt(rng, DONOR_CHARS.length)] as number)
            : randomAlphabetNByte(rng);
      }
      assertRoundTrip(out, `donor-dense len=${len}`);
    }
  });
});
