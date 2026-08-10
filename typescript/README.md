# base85n

A TypeScript implementation of **Base85N**, a binary-to-text encoding scheme
using a single 85-character alphabet (Alphabet-N) with an adaptive Dynamic
Passthrough (DP) mode for efficient, partially human-readable representation
of compatible byte sequences. See the repository root [`README.md`](../README.md)
for the full specification and [`NOTES.md`](../NOTES.md) for two mandatory
clarifications this implementation follows.

## Install

```bash
npm install
```

## Build

```bash
npm run build
```

Compiles `src/` to `dist/` (ESM, with `.d.ts` type declarations) using `tsc`.

## Test

```bash
npm test
```

Runs the [vitest](https://vitest.dev/) suite under `test/`, which:

- Verifies every golden vector in `../testvectors/vectors.json` round-trips
  through both `encode` and `decode`.
- Runs seeded (deterministic) random round-trip property tests across a wide
  range of input lengths and byte compositions (raw random bytes, Alphabet-N
  literals, R-Set characters, and the escape character `~`).
- Exercises explicit edge cases: empty input, 1-4 byte inputs, the
  `MIN_PASSTHROUGH_BYTES` boundary, multi-segment DP output
  (`> MAX_DP_OUTPUT_CHARS_PER_SIGNAL` characters), the
  `MAX_CONSECUTIVE_ESCAPES` scan-termination heuristic, and every byte value
  0-255.
- Asserts `decode()` throws a `Base85NDecodeError` (never returns garbage) on
  a variety of malformed inputs.

## Usage

```ts
import { encode, decode, Base85NDecodeError } from "base85n";

const bytes = new Uint8Array([72, 101, 108, 108, 111]);
const text = encode(bytes); // => Base85N-encoded string

try {
  const roundTripped = decode(text); // => Uint8Array, equal to `bytes`
} catch (err) {
  if (err instanceof Base85NDecodeError) {
    console.error(err.code, err.message);
  }
}
```

## API

```ts
function encode(data: Uint8Array): string;
function decode(s: string): Uint8Array; // throws Base85NDecodeError

class Base85NDecodeError extends Error {
  readonly code:
    | "invalid_character"
    | "unexpected_end_of_stream"
    | "dangling_escape_character"
    | "reserved_signal_value"
    | "invalid_partial_block_length";
  readonly position: number | undefined; // index into the whitespace-stripped input, if known
}
```

Also exported: the `ALPHABET_N_CHARS_STR`, `MIN_PASSTHROUGH_BYTES`,
`MAX_DP_OUTPUT_CHARS_PER_SIGNAL`, and `MAX_CONSECUTIVE_ESCAPES` constants from
the specification.
