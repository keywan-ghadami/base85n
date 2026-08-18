/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/*
 * The container claims in README.md § "Embedding: where the output can be
 * pasted verbatim" are properties of Alphabet-N, so they are testable -- and
 * they were wrong once: the README claimed for several versions that the
 * alphabet contained no backtick, which value 72 is. These tests pin every
 * cell of that table to the alphabet itself, in both directions: the
 * characters the documentation promises are absent, and the ones it warns are
 * present. A future alphabet change cannot quietly invalidate the docs.
 */

import { describe, expect, it } from "vitest";
import { encode } from "../src/index.js";
import { ALPHABET_N_CHARS_STR } from "../src/constants.js";

const ALPHABET = new Set(ALPHABET_N_CHARS_STR);

/** Every character the encoder can emit at position 0, over many lengths. */
function leadingCharacters(): Set<string> {
  const seen = new Set<string>();
  let seed = 0x2545f491;
  const nextByte = (): number => {
    seed ^= seed << 13;
    seed ^= seed >>> 17;
    seed ^= seed << 5;
    return (seed >>> 0) & 0xff;
  };
  for (let length = 1; length <= 48; length++) {
    for (let trial = 0; trial < 400; trial++) {
      const data = new Uint8Array(length);
      for (let i = 0; i < length; i++) data[i] = nextByte();
      const out = encode(data);
      if (out.length > 0) seen.add(out[0] as string);
    }
  }
  return seen;
}

describe("container safety claims", () => {
  it("excludes every character JSON, XML and HTML would force an escape for", () => {
    // JSON: quote and backslash. XML/HTML: the markup trio. Plus the
    // apostrophe, so single-quoted attributes and SQL literals are safe too.
    for (const ch of ['"', "'", "\\", "<", ">", "&"]) {
      expect(ALPHABET.has(ch), `alphabet must not contain ${ch}`).toBe(false);
    }
  });

  it("excludes whitespace and control characters", () => {
    for (const ch of ALPHABET) {
      const code = ch.codePointAt(0) as number;
      expect(code).toBeGreaterThan(0x20); // no control character, no space
      expect(code).toBeLessThan(0x7f); // no DEL, nothing non-ASCII
    }
  });

  it("does contain the characters the documentation warns about", () => {
    // Not a defect -- these are free in JSON, XML and HTML, which is why they
    // are in the alphabet. They are exactly why a JavaScript template literal,
    // an unquoted HTML attribute and a double-quoted shell word are called out
    // as containers that need care.
    for (const ch of ["`", "$", "{", "="]) {
      expect(ALPHABET.has(ch), `alphabet is documented as containing ${ch}`).toBe(true);
    }
  });

  it("emits backticks and ${ often enough to matter, so the warning is not theoretical", () => {
    const data = new Uint8Array(120000);
    let seed = 0x9e3779b9;
    for (let i = 0; i < data.length; i++) {
      seed ^= seed << 13;
      seed ^= seed >>> 17;
      seed ^= seed << 5;
      data[i] = (seed >>> 0) & 0xff;
    }
    const out = encode(data);
    expect(out.split("`").length - 1).toBeGreaterThan(0);
    expect(out.split("${").length - 1).toBeGreaterThan(0);
  });

  it("can begin with the characters that make a plain YAML scalar or a CSV field unsafe", () => {
    const leading = leadingCharacters();
    // YAML indicators, and the leads a spreadsheet reads as a formula.
    for (const ch of ["%", "{", "[", ":", "-", "?", "!", "*", "@", "=", "+"]) {
      expect(leading.has(ch), `output is documented as able to start with ${ch}`).toBe(true);
    }
  });

  it("round-trips through a JSON string with no escaping", () => {
    const data = Uint8Array.from({ length: 4096 }, (_, i) => (i * 37 + (i >> 3)) & 0xff);
    const out = encode(data);
    expect(JSON.parse(`{"v":"${out}"}`).v).toBe(out);
  });

  it("round-trips through a single-quoted JavaScript literal with no escaping", () => {
    const data = Uint8Array.from({ length: 1024 }, (_, i) => (i * 91) & 0xff);
    const out = encode(data);
    // eslint-disable-next-line no-eval
    expect(eval(`'${out}'`)).toBe(out);
  });
});
