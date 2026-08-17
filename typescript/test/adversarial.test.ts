/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

import { describe, expect, it } from "vitest";
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";
import { decode } from "../src/index.js";
import { Base85NDecodeError } from "../src/errors.js";
import { hexToBytes } from "./helpers.js";

// Adversarial decode vectors (testvectors/adversarial_vectors.json):
// multi-byte Unicode input at various positions (character-position vs.
// storage-unit discrepancies -- JS strings are UTF-16 code units, so this
// specifically exercises surrogate-pair handling), 0-length DP signals,
// invalid/reserved DP signals, and deliberately malformed escaping.

const __dirname = dirname(fileURLToPath(import.meta.url));

interface AdversarialVector {
  name: string;
  category: string;
  kind: "must_fail" | "valid";
  input_hex: string;
  error_code?: string;
  expected_hex?: string;
}

const vectorsPath = join(__dirname, "..", "..", "testvectors", "adversarial_vectors.json");
const vectors: AdversarialVector[] = JSON.parse(readFileSync(vectorsPath, "utf-8"));

// The vectors are byte-level, because the format is: a decoder's input is
// whatever arrives on the wire, and much of what arrives is not valid UTF-8.
// Each byte becomes the character of the same code unit -- the identity on
// ASCII, which is where Alphabet-N and the ignorable whitespace all live, so
// every byte from 0x80 up is one significant character outside the alphabet.
// That is the same mapping the C ABI and the Rust vector runner use.
//
// This used to be a fatal UTF-8 decoder, which quietly confined the shared
// vector set to inputs it could express. Every vector written before the
// error_precedence group is UTF-8-valid for that reason, not by choice.
function hexToByteString(hex: string): string {
  const bytes = hexToBytes(hex);
  let out = "";
  for (const b of bytes) out += String.fromCharCode(b);
  return out;
}

describe("adversarial decode vectors", () => {
  it("loads a non-trivial number of vectors", () => {
    expect(vectors.length).toBeGreaterThanOrEqual(15);
  });

  for (const v of vectors) {
    it(`${v.category}: ${v.name}`, () => {
      const input = hexToByteString(v.input_hex);

      if (v.kind === "must_fail") {
        let thrown: unknown;
        try {
          decode(input);
        } catch (e) {
          thrown = e;
        }
        expect(thrown, `expected decode() to throw for ${v.name}`).toBeInstanceOf(Base85NDecodeError);
        expect((thrown as Base85NDecodeError).code).toBe(v.error_code);
      } else {
        const expected = hexToBytes(v.expected_hex as string);
        expect(decode(input)).toEqual(expected);
      }
    });
  }
});
