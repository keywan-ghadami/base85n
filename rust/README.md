# base85n

A Rust implementation of **Base85N**, a binary-to-text encoding scheme
combining a dense 4-byte-to-5-character Base85 core with an adaptive
Dynamic Passthrough (DP) mode for near 1:1-efficiency, partially
human-readable output on favorable input.

See [the specification](../spec/base85n-v0.3.0.md) for the full normative text,
in particular Section 4.2's eight replacement alphabets and Section 6.1's
single-scan Dynamic Passthrough prefix identification, which this crate follows
exactly and exercises in its test suite.

## Usage

```rust
let data = b"hello, world!";
let encoded = base85n::encode(data);
let decoded = base85n::decode(&encoded).unwrap();
assert_eq!(decoded, data);
```

## API

```rust
pub fn encode(data: &[u8]) -> String;
pub fn decode(s: &str) -> Result<Vec<u8>, DecodeError>;
```

`DecodeError` implements `std::error::Error` and `Display`, with variants
for each error condition described in the spec's Section 10 (invalid
character, unexpected end of stream, reserved/undefined DP signal payload,
and invalid partial trailing block).

## Building and testing

```sh
cargo build
cargo test
```

The test suite:

- Loads and verifies every golden vector in
  [`../testvectors/vectors.json`](../testvectors/vectors.json) for both
  `encode` and `decode`.
- Runs randomized, fixed-seed round-trip property tests over a mix of
  arbitrary bytes, Alphabet-N literals, R-Set characters, and donor
  characters, across a wide range of input lengths -- including every
  donor paired with every R-Set character, which is what decides which
  alphabet can carry a run and where it has to break.
- Exercises explicit edge cases: empty input, partial-block boundary
  lengths (1-4 bytes), the `MIN_PASSTHROUGH_BYTES` (20) boundary, the
  `MAX_DP_ANALYSIS_BYTES` (1024) window boundary, runs long enough to need
  several signals, each of the eight alphabets carrying its own R-Set
  characters, and every byte value 0-255.
- Feeds `decode` deliberately malformed input (invalid characters,
  truncated DP segments, reserved signal payloads, the biased length
  field's boundaries, invalid partial trailing groups) and asserts it
  returns `Err`, never panics.

## Build-time options, measured

The library is written so that the compiler discharges its bounds checks
rather than emits them (see the module comment in `src/decode.rs`), which puts
it within a few percent of the C implementation's instruction count and ahead
of it on binary decoding. Beyond that, the remaining levers are build settings
-- and most of them are the *consumer's* to set, since a `[profile]` in this
crate does not apply to a downstream binary.

Measured as instruction counts under callgrind on 200 kB inputs, against the
default release profile (`bench/instructions/run.sh` runs the C comparison):

| option | effect |
|---|---|
| `lto = "fat"`, `codegen-units = 1` | none (0 to -1 %). Everything hot is one crate and already inlines. |
| Profile-guided optimisation | **4-6 % better** on text and mixed input, 4 % worse on random encoding. The gain is in branch layout, which is why it shows up where the mode decision is doing real work and not where the encoder is one straight line. |
| `-C target-cpu=native` | not measurable here -- it emits instructions valgrind cannot interpret. It is also a deployment choice, not a library one. |

Two things that sound like they should help and do not. An `assert!` relating
the input and output lengths at the top of `scan`, as a hint, changes nothing:
the checks it would discharge are already gone. And `std::simd` is a trap
unless you go all the way -- see below.

### On SIMD

The classification both `RunState::scan` and `first_dp_capable_run` are built
on -- "is this byte representable" -- vectorises well. A nibble-pair lookup
(the technique simdjson uses) over 16 lanes measures **3.2x** against the
scalar table: 1.0 instructions per byte against 3.25.

That number needs three things at once, and it is worth being explicit about
them because two of the three are easy to miss:

1. Nightly, for `#![feature(portable_simd)]`.
2. `-C target-feature=+avx2` (or whatever the shuffle needs).
3. `-Z build-std`. Without it the specialisation of `swizzle_dyn` stays in
   precompiled `core`, built for baseline x86-64, and the "SIMD" version
   compiles to a scalar fallback that is **1.5-3x slower than the scalar
   code it replaces**. The binary contains no `pshufb` at all; the only way
   to notice is to look.

Classification is around a sixth of encoding a text-shaped input, so 3.2x on
it is worth roughly 11 % overall -- real, but not what the ceiling looks like
from the kernel number alone. The larger prizes, a shuffle-based DP
translation and a vectorised Base85 conversion, are correspondingly larger
projects.

None of this is in the crate. It stays stable-only, `unsafe`-free and
portable, and the numbers above are here so that a consumer who controls their
own build can decide otherwise with evidence.
