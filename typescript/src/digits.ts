/**
 * Section 8: Value/Digit conversion helpers.
 *
 * All values handled here are well within Number.MAX_SAFE_INTEGER (2^53-1) --
 * the largest possible value is 2^32 + 2^22 - 1 (~4.3e9) -- so plain floating
 * point integer arithmetic (never bitwise operators, which truncate to 32
 * bits in JavaScript) is used throughout.
 */
import { VALUE_TO_CHAR } from "./constants.js";

/** ValueToBase85Digits: convert an integer value to exactly 5 Alphabet-N characters. */
export function valueToBase85Chars(value: number): string {
  const digits = new Array<number>(5);
  let v = value;
  for (let i = 4; i >= 0; i--) {
    digits[i] = v % 85;
    v = Math.floor(v / 85);
  }
  let out = "";
  for (let i = 0; i < 5; i++) {
    out += VALUE_TO_CHAR[digits[i] as number] as string;
  }
  return out;
}

/** Base85DigitsToValue: convert 5 digit values (0-84) to their combined integer value. */
export function base85DigitsToValue(digits: readonly number[]): number {
  let val = 0;
  for (let i = 0; i < digits.length; i++) {
    val = val * 85 + (digits[i] as number);
  }
  return val;
}

/** Convert a 32-bit unsigned integer to 4 bytes, Big-Endian. */
export function uint32ToBytesBE(value: number): [number, number, number, number] {
  return [
    Math.floor(value / 16777216) % 256,
    Math.floor(value / 65536) % 256,
    Math.floor(value / 256) % 256,
    value % 256,
  ];
}

/** Convert up to 4 bytes (Big-Endian, zero-padded on the right if fewer than 4) to an unsigned 32-bit integer. */
export function bytesBEToUint32(b0: number, b1: number, b2: number, b3: number): number {
  return b0 * 16777216 + b1 * 65536 + b2 * 256 + b3;
}
