# base85n

A Rust implementation of **Base85N**, an encoding for data that has to be
embedded in a text-based format — JSON, XML, HTML, configuration files — where
Base64 would otherwise be used and the size or the cleanliness of the result
matters. It combines a dense 4-byte-to-5-character Base85 core with an adaptive
Dynamic Passthrough (DP) mode — 1:1 efficiency and partially human-readable output on
favourable input — and a Fill mode that carries a run of up to 2048 identical
bytes in five characters, or a short zero run together with the two bytes
beside it.

See [the specification](../spec/base85n-v0.5.0.md) for the full normative text,
in particular Section 4.2's donor profiles, Section 6's encoding procedure and
Section 6.6's linear-time bound, which this crate follows exactly and exercises
in its test suite.

It is also the implementation behind the Python bindings in
[`../python/`](../python/), so the two cannot drift.

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
pub fn encode_parallel(data: &[u8], threads: usize) -> String;
pub fn decode(s: &str) -> Result<Vec<u8>, DecodeError>;
```

`encode_parallel` returns exactly what `encode` returns — `threads` divides the
work and changes nothing about the result. The format allows that without a
chunk-size parameter because signals are self-describing and segment
boundaries are decided by the data, so encoders started at different offsets
converge and their output can be spliced; spec section 11.3 states the
procedure and the measured convergence distances that bound it. Inputs below a
couple of megabytes are encoded on the calling thread. On a four-core machine,
16 MiB of mixed input goes from 304 MB/s to 743 MB/s at four threads:

```sh
cargo run --release --example parallel [file] [repeats]
```

`DecodeError` implements `std::error::Error` and `Display`, with one variant
per error condition in the spec's Section 10: `InvalidCharacter`,
`UnexpectedEndOfStream`, `UndefinedSignal` (a group value in
`FUTURE_SIGNAL_SPACE`) and `InvalidFinalBlock`. `DecodeError::code()` returns
the shared error-code string the test vectors use, and `position()` the byte
offset where the condition names one.

The crate also re-exports the tables of Section 4 — `ALPHABET_N`, `RSET_ASCII`,
`PROFILES` — and the constants of Sections 6.4 and 9 under `constants`, so a
binding layer does not need a second copy of them.

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

The API is the C implementation's, deliberately: same three functions, same
status codes with the same numeric values, output buffers allocated with
`malloc()` and released by the caller with `free()`. The two libraries are
interchangeable — `capi/run.sh` compiles the same C program against both
headers and links it against this library, and CI runs it, so "drop-in" is a
checked claim.

**This is the build to bind to.** Everything below the three `extern "C"`
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

`src/ffi.rs` is the only `unsafe` in the crate: the two pointer-validating
entry points, `base85n_strerror`, and one `malloc`-and-copy helper, each with
its safety argument stated at the site. The encoder and decoder themselves contain none.

## Building and testing

```sh
cargo build
cargo test
./capi/run.sh   # the C ABI, exercised from C against both headers
cargo package   # the publishable tarball, tested as its own crate in CI

cargo +nightly test --features simd   # the optional vectorised scan, below
```

Rust 1.88 or newer (`rust-version` in `Cargo.toml`): the decoder and block mode
use `slice::as_chunks`, which stabilised there. CI builds against that exact
version as well as stable and beta.

`LICENSE` and `testvectors/` in this directory are symbolic links to the single
copies at the repository root; `cargo package` resolves them, so the tarball
carries the licence text and the golden vectors as real files and the test
suite above runs from an unpacked crate with no repository around it. CI does
exactly that on every push, which is what keeps "publishable" a checked claim
rather than an assumption.

The test suite:

- Loads and verifies every golden vector in
  [`../testvectors/vectors.json`](../testvectors/vectors.json) for both
  `encode` and `decode`.
- Runs randomized, fixed-seed round-trip property tests over a mix of
  arbitrary bytes, Alphabet-N literals, R-Set characters, donor characters
  and runs of identical bytes, across a wide range of input lengths --
  including every donor paired with every R-Set character, which is what
  decides which profile can carry a run and where it has to break.
- Exercises explicit edge cases: empty input, partial-block boundary
  lengths (1-4 bytes), the `MIN_PASSTHROUGH_BYTES` (20) boundary, the
  `MAX_DP_ANALYSIS_BYTES` (2048) window boundary, the Fill thresholds and
  the 2048-byte Fill cap, transitions between all three modes, all 13 R-Set
  characters in one segment, and every byte value 0-255.
- Feeds `decode` deliberately malformed input (invalid characters,
  truncated DP segments, values in `FUTURE_SIGNAL_SPACE`, the biased length
  fields' boundaries, non-canonical final blocks) and asserts it returns
  `Err`, never panics -- and that a stream of Fill signals expands by no
  more than the ratio Section 13 states.
- Calls the C entry points as a foreign caller would (`src/ffi.rs`):
  round trips through them, null and zero-length arguments, non-UTF-8
  input, and the rule that a rejected call leaves the caller's
  out-parameters untouched. `capi/run.sh` then repeats that from actual
  C, linked against the built static library.

## How it compares to the C implementation

Both implementations follow the same specification and produce byte-identical
output -- 280,000 generated inputs are run through both and compared before any
speed number here is taken seriously (`c/fuzz/`). What differs is how fast they
produce it.

Throughput on 4 MB inputs, best of three interleaved rounds of twelve
(`bench/throughput/run.sh`), Intel Xeon at 2.1 GHz, gcc 13 `-O2` against the
default release profile:

| input | encode | decode |
|---|---|---|
| random bytes | 0.87x C | **1.56x C** |
| text | **1.04x C** | 1.07x C |
| mixed | **1.12x C** | 1.02x C |

**Decoding -- the side that parses data your system did not produce -- is
ahead of C throughout, by half again on high-entropy input.** That is what
matters for the recommendation in
[SECURITY.md](../SECURITY.md#recommended-bind-the-rust-build-not-the-c-one) to
link this build rather than the C one from an FFI: the memory safety is not
bought with decoder throughput.

**Encoding is at parity, except on high-entropy binary, where C is about 15 %
ahead.** Both encoders spend nearly all of their time deciding what mode to use
rather than writing output, and both are built around the same three ideas: a
fused classification table with a 64-bit "already accounted for" set that
retires every repeated character in one bit test, a block-mode skip that asks a
whole word whether eight bytes could begin a passthrough segment, and a
substitution table kept across segments rather than rebuilt. None of that needs
`unsafe`: the encoder and decoder in this crate contain none, and all of the
crate's `unsafe` is the three C-ABI entry points in `src/ffi.rs`.

### Instruction counts are the wrong instrument here

`bench/instructions/run.sh` counts instructions under callgrind, which is
deterministic and reproduces on any machine where wall-clock numbers do not.
That makes it the right tool for comparing two *specification* versions, where
the difference is in what has to be computed. It is the wrong tool for this
encoder, and the 2026-08 optimisation pass shows why:

| random-byte encoding | instructions | throughput |
|---|---|---|
| before the pass | 3.42 M (1.74x C) | 261 MB/s (0.24x C) |
| with only the word gates in | 3.57 M (1.81x C) | 545 MB/s |
| after the pass | 2.21 M (1.12x C) | 948 MB/s (0.87x C) |

The middle row is the point. Asking a whole word whether eight bytes could
begin a passthrough segment, instead of asking the table about one byte, *added*
4 % to the instruction count and made the encode 2.1 times faster. What it
removed was a branch nothing could predict -- roughly a third of byte values are
representable, so on binary that test is a coin flip resolved once per four
bytes of the file. An instruction count charges nothing for a mispredict and
full price for the arithmetic that avoids one.

Read `bench/throughput` for how fast the code is, and `bench/instructions` for
how much work it does.

## Build-time options, measured

The library is written so that the compiler discharges its bounds checks rather
than emits them (see the module comment in `src/decode.rs`, and the padded
tables in `src/digits.rs`), which is what keeps it at the C implementation's
speed without any `unsafe` in the codec. Beyond that, the remaining levers are build settings -- and most of
them are the *consumer's* to set, since a `[profile]` in this crate does not
apply to a downstream binary.

Measured as instruction counts under callgrind on 200 kB inputs, against the
default release profile:

| option | effect |
|---|---|
| `lto = "fat"`, `codegen-units = 1` | none (0 to -1 %). Everything hot is one crate and already inlines. |
| Profile-guided optimisation | **4-6 % better** on text and mixed input, 4 % worse on random encoding. The gain is in branch layout, which is why it shows up where the mode decision is doing real work and not where the encoder is one straight line. |
| `-C target-cpu=native` | not measurable here -- it emits instructions valgrind cannot interpret. It is also a deployment choice, not a library one. |

One thing that sounds like it should help and does not: an `assert!` relating
the input and output lengths at the top of `scan`, as a hint, changes nothing --
the checks it would discharge are already gone.

Vectorising is a build setting too, and the one with the largest effect; it has
a section of its own below.

## A feature flag for more speed: `simd`

The crate is stable-only and portable by default. `--features simd` adds one
vectorised step to the Dynamic Passthrough prefix scan, which is the loop that
dominates encoding text, and **requires nightly** -- it is the only thing in the
crate that does.

```toml
base85n = { version = "0.5", features = ["simd"] }
```

```sh
# The whole recipe. All three parts are load-bearing; see below.
RUSTFLAGS="-C target-feature=+avx2" \
  cargo +nightly build --release --features simd \
  -Z build-std=std,panic_abort --target x86_64-unknown-linux-gnu
```

Encoding throughput against the default stable build, same machine, same
inputs, best of three interleaved rounds:

| input | stable (default) | `simd`, built as above | `simd`, nightly alone |
|---|---|---|---|
| text (this README, tiled) | 489 MB/s | **566 MB/s** (1.16x) | 275 MB/s (0.56x) |
| text of Alphabet-N characters only | 553 MB/s | **915 MB/s** (1.65x) | 392 MB/s (0.71x) |
| mixed | 482 MB/s | 468 MB/s (0.97x) | 320 MB/s (0.66x) |
| random bytes | 947 MB/s | 935 MB/s (0.99x) | 920 MB/s (0.97x) |

Reproduce with `bench/throughput/run.sh`; its header carries the environment
variables for the second and third columns.

**Take the third column seriously.** `-Z build-std` is not a refinement of the
recipe, it is part of it: without it the specialisation of `swizzle_dyn` stays
in precompiled `core`, built for baseline x86-64, and the shuffle this feature
is built on compiles to a scalar fallback. The binary contains no `pshufb` at
all; nothing warns you, and the feature you turned on for speed costs you 44 %
of your text encoding. If you cannot build the standard library, do not turn
this on.

**And take the last row seriously too.** The feature does nothing for binary
input, where the scan is not where the time goes, and costs about 3 % there for
the test that finds that out. It is a switch for encoding text, not a switch
for going faster.

### What it does, and why it cannot change the output

`src/simd.rs` answers one question: *do the next sixteen bytes leave the scan's
state alone and open no run?* It is the same question the scalar loop asks one
byte at a time -- "is this byte's class already accounted for, and is it
different from its predecessor" -- as a nibble-pair lookup (the technique
simdjson uses) over 16 lanes, plus one comparison against the same bytes offset
by one.

The answer is a count of bytes the scan may step over, never a decision about
the encoding. The scalar loop still handles the byte that stops the skip and
everything after it, so a wrong "keep going" is impossible to express: the
vector step can only skip bytes it has proved the scalar loop would have walked
past without doing anything. Answering with the run rather than with a yes or no
is what keeps it worth doing on ordinary text, where something interesting turns
up every few bytes.

The membership set costs nothing to maintain. Each class the scan accounts for
is carried by exactly one byte value -- one R-Set character, or one donor -- so
accounting for it clears exactly one bit of the 16-byte table.

The crate's test suite runs unchanged under the feature, and the differential
harness in `c/fuzz/` was run against the C implementation with it on: 160,000
generated inputs, byte-identical output.

### What is not vectorised

The DP translation loop and the Base85 conversion in block mode are the two
larger prizes, and both are correspondingly larger projects: a shuffle-based
translation needs the 128-entry substitution split into nibble tables per
segment, and a vectorised base-85 conversion needs the division by 85 done in
lanes. Neither is here. Nor is anything for the decoder, which is already ahead
of the C implementation.
