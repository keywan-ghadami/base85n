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

const utf8Decoder = new TextDecoder("utf-8", { fatal: true });

function hexToUtf8String(hex: string): string {
  return utf8Decoder.decode(hexToBytes(hex));
}

describe("adversarial decode vectors", () => {
  it("loads a non-trivial number of vectors", () => {
    expect(vectors.length).toBeGreaterThanOrEqual(15);
  });

  for (const v of vectors) {
    it(`${v.category}: ${v.name}`, () => {
      const input = hexToUtf8String(v.input_hex);

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
