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
  C, linked against the built static library, after checking that both
  headers declare exactly the functions the library exports.
- Holds the encoder's *decisions* to the specification, which round trips do
  not: spec section 12.3's requirement that skipping ahead emits exactly
  what re-deciding emits (`tests::skip` builds the second encoder out of the
  first and compares, over runs at every offset modulo four and the
  thresholds around each mode), section 12.1's structural assertions, and
  the four table invariants the encoder reads as facts -- each named at the
  place that consumes it, in `src/alphabet.rs`.
- Checks that each vectorised kernel answers what the scalar code it stands
  in for answers (`src/simd.rs`, with `--features simd`), and that the
  encoder's output never outgrows the buffer it sizes up front.
- Guards the linear-time bound of section 6.6 with a timing check built to
  survive a busy machine -- and a second check that runs the same
  measurement against deliberately quadratic work, so that a guard which has
  stopped being able to fail fails.

Beyond the crate: `c/fuzz/` runs the C and Rust implementations against each
other in one process, on generated input, comparing encoder output character
for character and decoder verdicts including the error code -- once for the
default build and once for the `simd` one. CI runs both, packages the crate
and runs its suite from the unpacked tarball, and builds against the declared
minimum toolchain.

And beyond CI, because they cost minutes to hours rather than seconds:

```sh
tools/security-audit.sh            # the scoped set, a minute per fuzz target
tools/security-audit.sh --full 600 # every test under Miri, ten minutes each
```

Miri over the `unsafe` at the C boundary with strict provenance, the suite
under AddressSanitizer, the parallel encoder under ThreadSanitizer,
`cargo-deny` over the build tree, and six coverage-guided fuzz targets --
`rust/fuzz/` for the Rust API, including the parallel encoder's seams at a
chunk size a fuzz case can contain, and `c/fuzz/` for the C implementation and
the two against each other. Anything not installed is reported and skipped
rather than failing the run.

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

The crate is stable-only and portable by default. `--features simd` vectorises
the three per-byte loops the encoder spends its time in, and **requires
nightly** -- it is the only thing in the crate that does.

```toml
base85n = { version = "0.5", features = ["simd"] }
```

```sh
# The whole recipe. All three parts are load-bearing; see below.
RUSTFLAGS="-C target-feature=+avx2" \
  cargo +nightly build --release --features simd \
  -Z build-std=std,panic_abort --target x86_64-unknown-linux-gnu
```

Encoding throughput against the default stable build, measured in the same
interleaved run so the ratios are the reading that travels between machines
(`bench/throughput/run.sh`, whose header carries the environment variables for
the second and third columns):

| input | `simd`, built as above | `simd`, nightly alone |
|---|---|---|
| text of Alphabet-N characters only | **3.42x** | 1.29x |
| text (this README, tiled) | **1.70x** | 0.77x |
| mixed | **1.22x** | 0.79x |
| random bytes | **1.09x** | 1.09x |

**Take the third column seriously.** `-Z build-std` is not a refinement of the
recipe, it is part of it: without it the specialisation of `swizzle_dyn` stays
in precompiled `core`, built for baseline x86-64, and the shuffle the prefix
scan is built on compiles to a scalar fallback. The binary contains no `pshufb`
at all; nothing warns you, and the feature you turned on for speed costs you
almost a quarter of your text encoding. (The other two kernels use compile-time
shuffles and comparisons, which is why the last row survives that build.) If you
cannot build the standard library, do not turn this on.

### What it vectorises

Three loops, in the order they were worth doing:

- **The prefix scan's fast path.** Sixteen bytes at a time, the scan asks "do
  these change the state or open a run?" as a nibble-pair lookup -- the technique
  simdjson uses -- against the same set of accounted-for classes the scalar loop
  carries in a `u64`. This is what encoding text is mostly made of.
- **The substitution of section 4.3.** The table the scalar loop reads is the
  identity with the segment's `k` donors patched in, and `k` is one to three on
  ordinary text, because a segment spends a donor per *distinct* R-Set character.
  So it is not a table to vectorise but a handful of "replace this byte with that
  one": a comparison and a blend each, sixteen bytes at a time. On a segment of
  only Alphabet-N characters this is most of the remaining work.
- **The block-mode skip of section 11.1.** Eight groups settled per vector
  against two per word: the two Fill gates are collected by one shuffle each and
  stay exact, and the passthrough gate becomes "are eight bytes in range from
  this group start", folded out of the in-range bits of all forty bytes in three
  shifts. This is the one that moves binary input, where the skip is 39 % of the
  encode.

### Why it cannot change the output

Each of the three answers a question, never a decision.

The scan's step returns a count of bytes it has *proved* the scalar loop would
walk past without doing anything; the scalar loop then handles the byte that
stopped it and everything after. The skip's window may only fail to rule a group
out -- the exact per-group tests decide behind it, unchanged. And the
substitution is the same mapping the table holds, applied by comparison instead
of by lookup.

So a wrong "keep going" is not expressible in the first two, and the third is
checked against the table it replaces. All of it is checked rather than argued:
the crate's own suite runs unchanged under the feature and adds contract tests
for each kernel against the scalar predicate it stands in for, the section 12.3
skip differential runs in this build too, and the differential harness in
`c/fuzz/` was run against the C implementation with the feature on -- 200,000
generated inputs, byte-identical output.

### What is not vectorised

The Base85 conversion in block mode, which is what random binary spends its
remaining time on: five digits per four bytes needs division by 85 in lanes and
a shuffle to pack five-byte groups, and it is a project rather than a kernel.
Nothing for the decoder, which is already ahead of the C implementation.

### Threads and vectors multiply

They are independent: `encode_parallel` runs the same encoder on each chunk, so
the feature applies inside every worker. Measured on text, 16 MiB, same machine:

| | 1 thread | 2 threads | 4 threads |
|---|---|---|---|
| stable | 240 MB/s | 355 MB/s | 628 MB/s |
| `simd` | 402 MB/s | 513 MB/s | 893 MB/s |
| | 1.68x | 1.44x | 1.42x |

Threads alone buy 2.6x on four cores, the feature 1.7x on one, and together
3.7x. The vector gain narrows as threads are added, which is what a workload
approaching memory bandwidth looks like.

**What does not work**, since it is the obvious next thought: running several
*encoders* in lanes and splicing their output the way threads do. The splice
works for threads because each one runs the whole branchy encoder independently
and the format lets outputs be joined; lanes cannot, because they execute one
instruction between them. Two lanes in different modes, with different segment
lengths and different amounts of output, are not a vector operation -- they are
eight state machines wearing one. What vectorises is the work *inside* one
encoder's phases, which is what the three kernels above are.

## Versioning

The major and minor version track the specification version this crate
implements — `0.5.x` implements specification v0.5.0, whose wire format is
frozen. The patch level is this crate's own: packaging, provenance and
documentation changes that alter no encoded output. Anything that would change
the wire format would change the specification's version first.

## Provenance

Every `.crate` published from this repository is uploaded by
[`.github/workflows/release.yml`](https://github.com/keywan-ghadami/base85n/blob/main/.github/workflows/release.yml), never from
anyone's machine, and carries a Sigstore-signed
[SLSA build provenance](https://slsa.dev/spec/v1.0/provenance) attestation — a
statement that this repository, this workflow and one particular commit
produced exactly those bytes. It is keyless: no signing key exists for longer
than the job that made it, and there is no public key to fetch.

crates.io has no signature mechanism of its own, so the attestation lives in
this repository's attestation store. That still lets you check the file cargo
downloaded, because `cargo publish` re-packages deterministically and the
checksum the workflow logs before uploading is the one crates.io records:

```sh
gh attestation verify ~/.cargo/registry/cache/*/base85n-<version>.crate \
  --repo keywan-ghadami/base85n
```

Each tagged release also appears under
[Releases](https://github.com/keywan-ghadami/base85n/releases) with that exact
file and its checksum.
