# Benchmarks

Base85N measured against four established encodings, on encoded size and
on throughput.

- **[results/RESULTS.md](results/RESULTS.md)** — the report: what was
  measured, the numbers, and what they mean, including a section on where
  the other Base85 variants beat Base85N. Start there.
- `results/size.md`, `results/size.json` — generated size tables and the
  raw measurements behind them.
- `results/mode-mix.md` — which construct carried how much of each file:
  what makes a ratio below 1.0 checkable rather than surprising.
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

The corpus lives in
[binary2textbench](https://github.com/keywan-ghadami/binary2textbench), which
measures this codec against Base64, classic basE91, Ascii85, Base91z and
Base94Max on the same bytes. It used to live here, in `corpus.py` and
`wire_samples.py`; Base91z carried a second copy of the same generator, and two
corpus generators that are supposed to agree are a bug waiting to happen.
`fetch.sh` clones it and fills `corpus/`; the scripts here import the modules
exactly as before, by way of `central.py`.

Nothing is vendored into this repository. The generator downloads each
sample from a pinned upstream archive and verifies it against a recorded
SHA-256, so a rerun either reproduces the same bytes or fails loudly.
Downloads land in `corpus/` (git-ignored).

There are two groups. The **core** group is thirteen files, 6.52 MB, one
per input class, each from a package on PyPI or the npm registry. The
**silesia** group is the Silesia compression corpus: twelve files,
202 MiB, and the point of it is that this project did not choose it.

### Core

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

### Silesia

| sample | class | what it is |
|---|---|---|
| `dickens` | prose | the collected works of Charles Dickens |
| `mozilla` | archive | tarred executables of Mozilla 1.0, Tru64 UNIX |
| `mr` | image | a medical magnetic-resonance image |
| `nci` | data | a chemical database of structures |
| `ooffice` | binary | a shared library from OpenOffice.org 1.01 |
| `osdb` | data | a MySQL sample database (Open Source Database Benchmark) |
| `reymont` | document | the book *Chłopi* by Władysław Reymont, as a PDF |
| `samba` | archive | tarred source code of Samba 2-2.3 |
| `sao` | binary | the SAO star catalogue, fixed-width binary records |
| `webster` | prose | the 1913 Webster Unabridged Dictionary, HTML |
| `x-ray` | image | an X-ray medical image |
| `xml` | code | collected XML files |

Silesia is here as a control. Thirteen files picked by the author of a
codec are a weak basis for a claim about real data, however carefully
they are picked: the classes are chosen, and so is the mix. Silesia was
assembled in 2003 by somebody with no interest in this encoding, has not
changed since, and most published compression work reports against it. It
also contains classes the core group has none of — a star catalogue, two
medical images, a chemical database, a dictionary — and it is 32 times the
size.

It is not on PyPI or npm. The pinned archive is the Go module proxy's
snapshot of the `SilesiaCorpus` repository, which is an immutable,
content-addressed artefact named by the commit it was built from, and its
SHA-256 is recorded like every other. Each of the twelve members is a
single-file zip; the extracted lengths are the corpus's published ones.

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
bench/fetch.sh all                     # fetch and verify every group (~87 MB download)
bench/fetch.sh core                    # the 6.52 MB group only
python3 size_bench.py --markdown results/size.md --json results/size.json
python3 size_bench.py --no-silesia     # the same, core corpus only
python3 mode_mix.py                    # where each file's characters go
make -C speed run                      # throughput, needs the corpus
make -C speed run-silesia              # the same over the Silesia group
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
downloads nothing. The full run measures 208 MiB through five codecs, two
of which are pure Python, and takes a few minutes; `--no-silesia` is the
fast path and reproduces every core table.

## Where the characters go

```sh
python3 mode_mix.py [FILE...]          # a table; --markdown OUT writes it
```

A ratio below 1.0 looks impossible for an encoding whose passthrough mode
is exactly 1:1, and the reaction to `countries.json` at 0.935 is normally
to assume the number is stale. `mode_mix.py` settles it by walking the
encoded stream and attributing every input byte and every output
character to the construct that carried it, using only the signal ranges
of specification section 7. It decides nothing itself, so it cannot agree
with the encoder by accident, and it fails loudly unless its attribution
adds up to the file and to its encoding exactly.

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
