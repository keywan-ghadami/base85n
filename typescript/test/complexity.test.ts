/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * Guard against the quadratic encoder of spec Section 6.6.
 *
 * Pass 1 scans to the end of a representable run while the main loop can consume as
 * little as 4 bytes of it, so an encoder that re-runs Pass 1 on every iteration is
 * O(n^2). A buffer of escape characters is the worst case: Pass 2 gives up after 3
 * bytes every time.
 *
 * The time limits are deliberately loose. A linear encoder handles this input in
 * milliseconds; the quadratic one these tests exist to catch needed minutes, so any
 * bound in between works and a generous one does not go flaky on a slow or loaded
 * machine.
 */
import { describe, expect, it } from "vitest";

import { decode, encode } from "../src/index.js";

const ESCAPE_DENSE_SIZE = 128 * 1024;
const TIME_LIMIT_MS = 20_000;

function escapeDense(n: number): Uint8Array {
  return new Uint8Array(n).fill(0x7e); // '~'
}

describe("encoding complexity (spec Section 6.6)", () => {
  it("encodes escape-dense input in linear time", () => {
    const data = escapeDense(ESCAPE_DENSE_SIZE);

    const start = performance.now();
    const encoded = encode(data);
    const elapsed = performance.now() - start;

    expect(decode(encoded)).toEqual(data);
    expect(
      elapsed,
      `encoding ${ESCAPE_DENSE_SIZE} escape characters took ${elapsed.toFixed(0)}ms; ` +
        "this is the signature of the quadratic Pass 1 rescan that spec Section 6.6 forbids",
    ).toBeLessThan(TIME_LIMIT_MS);
  }, 60_000);

  it("does not grow quadratically as the input doubles", () => {
    const timed = (n: number): number => {
      const data = escapeDense(n);
      const start = performance.now();
      encode(data);
      return performance.now() - start;
    };

    timed(4096); // warm up

    const small = timed(32 * 1024);
    const large = timed(64 * 1024);

    // Linear predicts ~2x, quadratic predicts ~4x. A 3x ceiling rules out quadratic
    // growth without being sensitive to ordinary timing noise.
    expect(
      large,
      `doubling the input multiplied encoding time by ${(large / small).toFixed(1)}; ` +
        "expected about 2 for a linear encoder",
    ).toBeLessThan(small * 3);
  }, 60_000);
});
