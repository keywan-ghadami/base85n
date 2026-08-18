# Benchmarks

Base85N measured against four established encodings, on encoded size and
on throughput.

- **[results/RESULTS.md](results/RESULTS.md)** — the report: what was
  measured, the numbers, and what they mean, including a section on where
  the other Base85 variants beat Base85N. Start there.
- `results/size.md`, `results/size.json` — generated size tables and the
  raw measurements behind them.
- **[results/binary-flag.md](results/binary-flag.md)** — a decision, not a
  comparison: whether a proposed `--binary` encoder flag delivered enough
  to be worth a permanent second encoder. It is kept because the
  measurement found something the proposal did not predict, and because
  the four-encoder shape it uses is the way to attribute a speed
  difference to a step rather than to a mode.

## What is compared

| codec | implementation used |
|---|---|
| Base64 (RFC 4648) | Python `base64.b64encode`; scalar C in `speed/bench_speed.c` |
| Ascii85 (Adobe/btoa) | Python `base64.a85encode`; scalar C in `speed/bench_speed.c` |
| Z85 (ZeroMQ RFC 32) | written for this benchmark, in both languages |
| Base85 (RFC 1924) | Python `base64.b85encode` |
| Base85N | this repository (`c/` for both; the size benchmark drives it through the `base85n` Python bindings, which are the Rust crate) |

The size benchmark reports five numbers per file: the raw expansion
ratio, and what the same output costs once it is placed in a JSON string
literal, in a double-quoted HTML attribute, in XML character data, and in
a URL query string. The report leads with the embeddings and keeps raw as
the last table, because raw is the case that almost never happens and it
flatters every codec whose alphabet contains `"`, `\`, `<`, `>` or `&` --
that escaping is a real cost, paid where encoded payloads actually
travel. The URL table is kept for the opposite reason: it is the one
embedding Base85N is worst at, since five of its punctuation characters
are outside RFC 3986's unreserved set.

Every measurement is verified by a round trip before it is reported, in
both the size benchmark and the throughput benchmark, so a codec cannot
post a good number by losing data. The C harness is additionally run
under AddressSanitizer and UndefinedBehaviorSanitizer (`make -C speed
check`) — a heap bug in a benchmark corrupts the numbers silently rather
than crashing reliably, as this harness demonstrated during development.

## The corpus

Nothing is vendored into this repository. `corpus.py` downloads each
sample from a pinned package on PyPI or the npm registry and verifies the
archive against a recorded SHA-256, so a rerun either reproduces the same
bytes or fails loudly. Downloads land in `corpus/` (git-ignored).

| sample | class | source package |
|---|---|---|
| `sql-wasm.wasm` | WebAssembly | npm `sql.js` 1.14.1 |
| `_cffi_backend.so` | ELF x86-64 | PyPI `cffi` 1.17.1 |
| `DejaVuSans.ttf` | TrueType | PyPI `matplotlib` 3.9.2 |
| `requests-2.32.3.tar` | uncompressed tar | PyPI `requests` 2.32.3, gzip removed |
| `countries.json` | JSON, pretty | npm `world-countries` 5.1.0 |
| `countries.min.json` | JSON, minified | the same dataset |
| `lodash.js` | JavaScript source | npm `lodash` 4.17.21 |
| `bootstrap.css` | CSS bundle | npm `bootstrap` 5.3.3 |
| `requests-models.py` | Python source | PyPI `requests` 2.32.3 |
| `commonmark-spec.txt` | prose | PyPI `commonmark` 0.9.2 |
| `requests-history.md` | Markdown changelog | PyPI `requests` 2.32.3 |
| `grace_hopper.jpg` | JPEG | PyPI `matplotlib` 3.9.2 |
| `minduka_present.png` | PNG | PyPI `matplotlib` 3.9.2 |

The tar sample is the one file that is not extracted from an archive but *is*
one: gzip decompression of a pinned `.tar.gz` is deterministic, so the tar
inside it is pinned too. It is in the corpus because block-padded container
formats are where Solid Fill does its work, and because no other sample has
long runs of a byte that is not zero.

Short protocol fields — names, customer numbers, hex digests, phone
numbers, UUIDs, a JSON record, an HTTP header block, a JWT — are authored
in `wire_samples.py` and need no download. They matter because most
encoded payloads in a real system are small, and small inputs are where a
block encoding's rounding-up to whole groups costs the most.

An actual IETF RFC would have been the natural choice for the
specification sample. The environment these numbers were produced in
cannot reach `rfc-editor.org`, so the CommonMark Specification stands in
for one: comparable length, comparable shape — long-form English
technical prose interleaved with code blocks.

## Running it

```sh
python3 corpus.py                      # fetch and verify the corpus
python3 size_bench.py --markdown results/size.md --json results/size.json
make -C speed run                      # throughput, needs the corpus
make -C speed check                    # same harness under the sanitizers

make -C speed binary-flag              # the --binary decision, binary corpus
make -C speed binary-flag-all          # the same over every sample
make -C speed binary-flag-selftest     # its conforming variants are identical
make -C speed binary-flag-check        # both, under the sanitizers
speed/binary_flag_instructions.sh      # the same variants, in instructions
```

The `binary-flag` harness builds several encoders into one binary — the
shipped one and, under `-DBASE85N_BENCH_ENCODERS`, variants with one of
the encoder's steps removed or one of its gates narrowed. Most of those
variants are **not conforming Base85N encoders**: the specification makes
every step mandatory at every decision point, so an encoder that skips one
emits a stream that decodes but that nothing should produce. They exist to
be measured. The harness round-trips all of them and holds the ones that
do claim to be conforming to character-for-character equality with
`base85n_encode()`, over the corpus and over generated cases.

`python3 size_bench.py --no-corpus` runs only the short wire samples and
downloads nothing.

The size numbers are implementation-independent: all four Base85N
implementations in this repository produce identical output for identical
input, which the shared test vectors enforce. The throughput numbers are
specific to the C implementation, the machine and the compiler; they are
recorded in the report.

## Instruction counts

```sh
instructions/run.sh [bytes]            # C against Rust, needs valgrind
```

This counts the instructions each implementation executes for one encode and
one decode of the same input, and prints the ratio. It exists because
throughput is unmeasurable on a shared or virtualised machine at the
resolution these changes move: the run-to-run spread is larger than the
difference. Instruction counts are deterministic and reproduce anywhere.

They are a proxy, not a timing result. They ignore cache behaviour, branch
prediction and memory bandwidth, and they charge `rep stosb` one instruction
per byte, which makes a large `memset` look far more expensive than it is.
Read a ratio near 1.0 as "the same amount of work"; for how long that work
takes, use the throughput harness below.

**They can also be actively misleading, and for the encoders they are.** Both
encoders spend most of a high-entropy encode deciding what mode to use, and the
decisions that cost are the ones a branch predictor gets wrong -- which an
instruction count charges nothing for, while charging full price for the
arithmetic that avoids them. One step of the Rust encoder's 2026-08 pass added
4 % to its instruction count on random input and made that encode 2.1 times
faster; `rust/README.md` has the table. Use this harness to compare
*specification* versions, where the difference is in what has to be computed;
use the one below to compare implementations.

## Throughput, C against Rust

```sh
throughput/run.sh [bytes] [reps] [rounds]   # needs a C compiler and cargo
```

Times both implementations encoding and decoding the same inputs and prints the
ratio. The two harnesses -- `throughput/time.c` and
`rust/examples/throughput.rs` -- generate their input the same way, run the same
loop and report the fastest round, so their numbers divide; each pair is run
interleaved over several rounds and the best of each is taken, which is what
makes the comparison survive a noisy host. The driver checks that both
implementations encoded to the same length before it reports anything.

Its header carries the environment variables for measuring the Rust crate's
optional `simd` feature, which needs a nightly toolchain and a rebuilt standard
library; `rust/README.md` says why.
