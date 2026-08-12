# Benchmarks

Base85N measured against four established encodings, on encoded size and
on throughput.

- **[results/RESULTS.md](results/RESULTS.md)** — the report: what was
  measured, the numbers, and what they mean, including a section on where
  the other Base85 variants beat Base85N. Start there.
- `results/size.md`, `results/size.json` — generated size tables and the
  raw measurements behind them.

## What is compared

| codec | implementation used |
|---|---|
| Base64 (RFC 4648) | Python `base64.b64encode`; scalar C in `speed/bench_speed.c` |
| Ascii85 (Adobe/btoa) | Python `base64.a85encode`; scalar C in `speed/bench_speed.c` |
| Z85 (ZeroMQ RFC 32) | written for this benchmark, in both languages |
| Base85 (RFC 1924) | Python `base64.b85encode` |
| Base85N | this repository (`python/`, `c/`) |

Alongside the raw expansion ratio, the size benchmark reports what each
codec costs once its output is placed inside a JSON string literal or in
XML character data. Comparing raw ratios alone flatters the codecs whose
alphabets contain `"`, `\`, `<`, `>` or `&`, because that escaping is a
real cost paid in the contexts encoded payloads actually travel in.

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
| `countries.json` | JSON, pretty | npm `world-countries` 5.1.0 |
| `countries.min.json` | JSON, minified | the same dataset |
| `commonmark-spec.txt` | prose | PyPI `commonmark` 0.9.2 |
| `grace_hopper.jpg` | JPEG | PyPI `matplotlib` 3.9.2 |
| `minduka_present.png` | PNG | PyPI `matplotlib` 3.9.2 |

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
```

`python3 size_bench.py --no-corpus` runs only the short wire samples and
downloads nothing.

The size numbers are implementation-independent: all five Base85N
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
takes, use the throughput harness on a quiet machine.
