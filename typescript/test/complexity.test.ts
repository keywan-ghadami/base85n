/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * Guard against the rescanning encoder of spec Section 6.6.
 *
 * Step 1 scans up to MAX_DP_ANALYSIS_BYTES bytes for each of the eight alphabets,
 * while step 2.b may consume as few as 4 bytes, so an encoder that redoes those scans
 * on every iteration performs 2048 byte inspections per input byte. Bounded lookahead
 * keeps that linear rather than quadratic -- unlike version 0.2.0 -- but a constant
 * factor of 2048 is still what this section exists to prevent.
 *
 * Pseudorandom bytes are the worst case: no alphabet reaches MIN_PASSTHROUGH_BYTES, so
 * every iteration takes the block-mode branch and advances 4 bytes, while a naive
 * implementation rescans the full window each time.
 *
 * Both tests here are timing-based, which on a shared CI runner means they have to be
 * built to tolerate interference. Two things make them stable: every duration is the
 * *minimum* of several runs, since scheduling noise only ever adds time and never
 * removes it, and the thresholds sit far from the values a healthy encoder produces.
 */
import { describe, expect, it } from "vitest";

import { decode, encode } from "../src/index.js";
import { mulberry32, randomBytes } from "./helpers.js";

const SCAN_DENSE_SIZE = 128 * 1024;
const TIME_LIMIT_MS = 20_000;

/** Sizes for the growth check, and how many times each is measured. */
const SMALL_SIZE = 32 * 1024;
const LARGE_SIZE = 64 * 1024;
const REPEATS = 5;

/** Below this, a measurement is too short for its ratio to mean anything. */
const MEASURABLE_MS = 1;

/** Linear predicts ~2.0, quadratic ~4.0. Halfway between is the decision point. */
const MAX_GROWTH = 3.0;

/** Input on which no alphabet ever reaches MIN_PASSTHROUGH_BYTES. */
function scanDense(n: number): Uint8Array {
  return randomBytes(mulberry32(0x5ca4de45 ^ n), n);
}

/** Fastest of `repeats` encodes of `n` scan-dense bytes, in milliseconds. */
function bestEncodeMs(n: number, repeats: number): number {
  const data = scanDense(n);
  let best = Infinity;
  for (let i = 0; i < repeats; i++) {
    const start = performance.now();
    encode(data);
    best = Math.min(best, performance.now() - start);
  }
  return best;
}

describe("encoding complexity (spec Section 6.6)", () => {
  it("encodes scan-dense input in linear time", () => {
    const data = scanDense(SCAN_DENSE_SIZE);

    const start = performance.now();
    const encoded = encode(data);
    const elapsed = performance.now() - start;

    expect(decode(encoded)).toEqual(data);
    expect(
      elapsed,
      `encoding ${SCAN_DENSE_SIZE} scan-dense bytes took ${elapsed.toFixed(0)}ms; ` +
        "this is the signature of the per-iteration rescan that spec Section 6.6 forbids",
    ).toBeLessThan(TIME_LIMIT_MS);
  }, 60_000);

  it("does not grow quadratically as the input doubles", () => {
    bestEncodeMs(4096, 1); // warm up

    const small = bestEncodeMs(SMALL_SIZE, REPEATS);
    const large = bestEncodeMs(LARGE_SIZE, REPEATS);

    if (small < MEASURABLE_MS) {
      return; // too fast to time meaningfully; the ceiling test still applies
    }

    const growth = large / small;
    expect(
      growth,
      `doubling the input multiplied encoding time by ${growth.toFixed(1)} ` +
        `(${small.toFixed(1)}ms -> ${large.toFixed(1)}ms); expected about 2 ` +
        "for a linear encoder",
    ).toBeLessThan(MAX_GROWTH);
  }, 60_000);
});
