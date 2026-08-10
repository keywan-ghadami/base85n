import { describe, expect, it } from "vitest";
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";
import { decode, encode } from "../src/index.js";
import { hexToBytes } from "./helpers.js";

const __dirname = dirname(fileURLToPath(import.meta.url));

interface Vector {
  name: string;
  input_hex: string;
  output: string;
}

const vectorsPath = join(__dirname, "..", "..", "testvectors", "vectors.json");
const vectors: Vector[] = JSON.parse(readFileSync(vectorsPath, "utf-8"));

describe("golden test vectors", () => {
  it("loads a non-trivial number of vectors", () => {
    expect(vectors.length).toBeGreaterThan(0);
  });

  for (const v of vectors) {
    it(`encode: ${v.name}`, () => {
      const input = hexToBytes(v.input_hex);
      expect(encode(input)).toBe(v.output);
    });

    it(`decode: ${v.name}`, () => {
      const expectedBytes = hexToBytes(v.input_hex);
      const decoded = decode(v.output);
      expect(Array.from(decoded)).toEqual(Array.from(expectedBytes));
    });
  }
});
