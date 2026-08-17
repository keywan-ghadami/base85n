# base85n

A TypeScript implementation of **Base85N**, an encoding for data that has to be
embedded in a text-based format — JSON, XML, HTML attributes, configuration files
— where Base64 would otherwise be used and the size or the cleanliness of the
result matters. It uses a single 85-character alphabet (Alphabet-N) that excludes
`"`, `'`, `\`, `<`, `>`, `&` and all whitespace, so output goes into a JSON
string, an XML node or a quoted attribute with no second escaping layer, with an
adaptive Dynamic Passthrough (DP) mode for efficient, partially human-readable
representation of compatible byte sequences. See
[the specification](../spec/base85n-v0.5.0.md)
for the full normative text, in particular Section 4.2's eight replacement
alphabets and Section 6.1's single-scan Dynamic Passthrough
encoding procedure, which this package follows exactly.

> **Before you embed the output: four containers need the value quoted.**
> Alphabet-N does contain `` ` ``, `$`, `{` and `=` — free in JSON, XML and HTML,
> not free everywhere. So encoded output must not be pasted raw into a
> **JavaScript template literal** (`` ` `` ends it, `${` interpolates — both occur
> in ordinary output, a backtick about one character in 85), an **unquoted HTML
> attribute**, a **plain YAML scalar**, or a **double-quoted shell word**. An
> ordinary `'…'` or `"…"` string is always safe. Full table, checked against real
> parsers:
> [Embedding: where the output can be pasted verbatim](https://github.com/keywan-ghadami/base85n#embedding-where-the-output-can-be-pasted-verbatim).

## Install

```bash
npm install base85n
```

Or, to work on this package inside a clone of the repository, `npm install` with
no arguments from this directory.

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
  literals, R-Set characters, and donor characters).
- Exercises explicit edge cases: empty input, 1-4 byte inputs, the
  `MIN_PASSTHROUGH_BYTES` boundary, multi-segment DP output
  (`> MAX_DP_OUTPUT_CHARS_PER_SIGNAL` characters), the
  `MAX_DP_ANALYSIS_BYTES` window boundary, and every byte value
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
    | "undefined_signal"
    | "invalid_final_block";
  readonly position: number | undefined; // index into the whitespace-stripped input, if known
}
```

Also exported: the `ALPHABET_N_CHARS_STR`, `MIN_PASSTHROUGH_BYTES`,
`MAX_DP_OUTPUT_CHARS_PER_SIGNAL`, `MAX_DP_ANALYSIS_BYTES` and
`REPLACEMENT_ALPHABETS` from
the specification.

## Versioning

The major and minor version track the specification version this package
implements — `0.5.x` implements specification v0.5.0, whose wire format is
stable. The patch level is this package's own: packaging and documentation
fixes that change no encoded output. A later specification version — 0.6.0 is
planned, adding *flavors* in the reserved signal range — would arrive here as
`0.6.x`, so the version you pin says which format you get.
