# base85n

A Rust implementation of **Base85N**, a binary-to-text encoding scheme
combining a dense 4-byte-to-5-character Base85 core with an adaptive
Dynamic Passthrough (DP) mode for near 1:1-efficiency, partially
human-readable output on favorable input.

See [the specification](../spec/base85n-v0.4.0.md) for the full normative text,
in particular Section 4.2's donor profiles and Section 6.1's
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

## Using it from C, and other languages

The crate also exports a C ABI, so anything with an FFI — C, C++, Python
(`ctypes`/`cffi`), Ruby, Zig, Java, Node, Lua — can link this implementation
instead of writing or vendoring its own. `cargo build --release` produces
`target/release/libbase85n.so` (`.dylib`/`.dll`) and `target/release/libbase85n.a`
next to the Rust library; the header is [`include/base85n.h`](include/base85n.h).

```c
#include <base85n.h>

char *encoded = NULL;
size_t len = 0;
if (base85n_encode((const uint8_t *)"hello, world!", 13, &encoded, &len) != BASE85N_OK) { ... }
/* ... */
free(encoded);
```

```sh
cc app.c -Irust/include rust/target/release/libbase85n.a -lpthread -ldl -lm
```

The API is the C implementation's, deliberately: same four functions, same
status codes with the same numeric values, output buffers allocated with
`malloc()` and released by the caller with `free()`. The two libraries are
interchangeable — `capi/run.sh` compiles the same C program against both
headers and links it against this library, and CI runs it, so "drop-in" is a
checked claim.

**This is the build to bind to.** Everything below the four `extern "C"`
entry points is safe Rust, so decoding attacker-controlled input is
bounds-checked by the compiler rather than by review. That matters most for
exactly the callers who reach for a C library: see
[SECURITY.md](../SECURITY.md#recommended-bind-the-rust-build-not-the-c-one).

Two properties to know before you bind:

- A panic cannot unwind into your frames — an `extern "C"` function aborts
  instead — and no panic is expected: encoding is total and decoding returns
  its errors as status codes.
- Rust aborts on allocation failure inside the codec, where the C library
  would return `BASE85N_ERR_ALLOC`. That status is still returned when the
  caller-owned output buffer cannot be allocated.

`src/ffi.rs` is the only `unsafe` in the crate: four pointer-validating entry
points and one `malloc`-and-copy helper, each with its safety argument stated
at the site. The encoder and decoder themselves contain none.

## Building and testing

```sh
cargo build
cargo test
./capi/run.sh   # the C ABI, exercised from C against both headers
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
  `MAX_DP_ANALYSIS_BYTES` (2048) window boundary, runs long enough to need
  several signals, each of the eight alphabets carrying its own R-Set
  characters, and every byte value 0-255.
- Feeds `decode` deliberately malformed input (invalid characters,
  truncated DP segments, reserved signal payloads, the biased length
  field's boundaries, invalid partial trailing groups) and asserts it
  returns `Err`, never panics.
- Calls the C entry points as a foreign caller would (`src/ffi.rs`):
  round trips through them, null and zero-length arguments, non-UTF-8
  input, and the rule that a rejected call leaves the caller's
  out-parameters untouched. `capi/run.sh` then repeats that from actual
  C, linked against the built static library.

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

None of this is in the crate. It stays stable-only and portable, with the
codec itself free of `unsafe` (all of which lives in `src/ffi.rs`, at the C
boundary), and the numbers above are here so that a consumer who controls their
own build can decide otherwise with evidence.
