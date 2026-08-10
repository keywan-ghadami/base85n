# Base85N

[![CI](https://github.com/keywan-ghadami/base85n/actions/workflows/ci.yml/badge.svg)](https://github.com/keywan-ghadami/base85n/actions/workflows/ci.yml)
[![Pages](https://github.com/keywan-ghadami/base85n/actions/workflows/pages.yml/badge.svg)](https://keywan-ghadami.github.io/base85n/)
[![Spec](https://img.shields.io/badge/spec-v0.2.0%20draft-blue)](spec/base85n-v0.2.0.md)
[![License](https://img.shields.io/badge/license-MPL--2.0-green)](LICENSE)

**A binary-to-text encoding that is denser than Base64 — and, for text-like
input, stays readable.**

Base85N packs 4 bytes into 5 characters from a single 85-character,
protocol-friendly alphabet (Alphabet-N). On top of that core it adds an
adaptive **Dynamic Passthrough (DP)** mode: when a run of input already
consists of characters the alphabet can carry, the encoder passes those bytes
through nearly one-to-one instead of expanding them, substituting a small
fixed set of "R-Set" characters (space, quotes, comma, newline, `<`, `>`, `&`, …)
with safe stand-ins. The encoder picks whichever of the two modes is shorter,
per segment, and the output needs no padding.

```
input     {"user":"ada","id":42,"role":"admin"}                   37 bytes

Base64    eyJ1c2VyIjoiYWRhIiwiaWQiOjQyLCJyb2xlIjoiYWRtaW4ifQ==    52 chars
Base85N   %nS`W{+user+:+ada+^+id+:42^+role+:+admin+}              42 chars
```

The JSON above stays legible after encoding: `+` stands in for `"`, `^` for
`,`, and the leading ``%nS`W`` is the 5-character DP signal announcing the
segment and which substitutions are active. Binary input falls back to dense
block mode:

```
input     00 01 02 ... 1f                                         32 bytes

Base64    AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=            44 chars
Base85N   009c61o!#m2NH?C3~iWS5d]J*6CRx17-skh9337x                40 chars
```

- 📖 **[Specification v0.2.0](spec/base85n-v0.2.0.md)** — the normative document
- 🌐 **[Project website](https://keywan-ghadami.github.io/base85n/)**
- 📊 **[Benchmarks](bench/results/RESULTS.md)** — size and throughput against
  Base64, Ascii85, Z85 and RFC 1924 Base85, including where those alternatives
  beat Base85N
- 🔐 **[Security policy](SECURITY.md)** — read this before decoding untrusted input
- ⚠️ **[AI-generated code notice](#ai-generated-code--notice)** — read this before using any of it

---

## AI-generated code — notice

⚠️ **Everything in this repository — the specification text, all five
implementations, and their test suites — was written with substantial AI
assistance. It has not been independently reviewed or audited by a human
security expert.**

What that means in practice:

- The code is *plausible* and it *passes its tests*, and its tests were largely
  written by the same process that wrote the code. Shared blind spots between
  implementation and test are the expected failure mode here, and no amount of
  green CI rules them out.
- Five independent implementations cross-checked against shared vectors is a
  real mitigation — a hallucinated rule tends to show up as a divergence — but
  it is not an audit, and it does not catch a mistake made consistently in the
  specification itself.
- The C implementation manages memory by hand. It is compiled warning-free and
  tested under ASan/UBSan, but it has never been fuzzed. Treat it accordingly.
- There is no fuzzing, no formal verification, and no external review. See
  [SECURITY.md § Measures still outstanding](SECURITY.md#measures-still-outstanding)
  for the full list of gaps.

**Recommendation:** read the code before you ship it. It is deliberately small.
If you cannot afford to review a dependency you rely on, do not adopt this one
yet — use a mature, audited encoding instead.

---

## Why Base85N

Every figure below is measured over 4.94 MB of real files — binaries, JSON,
specification text, images — plus a set of short protocol fields. Expansion is
encoded characters per input byte; lower is better. Full method and raw numbers:
**[benchmark results](bench/results/RESULTS.md)**.

| | Base64 | Ascii85 | Z85 | Base85 (RFC 1924) | **Base85N** |
|---|---|---|---|---|---|
| Whole corpus | 1.3333 | 1.1996 | 1.2500 | 1.2500 | **1.1503** |
| Pretty-printed JSON | 1.333 | 1.250 | 1.250 | 1.250 | **1.033** |
| Minified JSON | 1.333 | 1.250 | 1.250 | 1.250 | **1.053** |
| CommonMark spec text | 1.333 | 1.250 | 1.250 | 1.250 | **1.123** |
| WebAssembly module | 1.333 | 1.247 | 1.250 | 1.250 | **1.246** |
| TrueType font | 1.333 | **1.240** | 1.250 | 1.250 | 1.248 |
| JPEG photograph | 1.333 | 1.250 | 1.250 | 1.250 | **1.250** |
| Zero-padded ELF | 1.333 | **1.026** | 1.250 | 1.250 | 1.246 |
| **…carried inside XML** | 1.3333 | 1.4171 | 1.3662 | 1.3530 | **1.1503** |
| **…what that costs vs Base85N** | +15.9 % | +23.2 % | +18.8 % | +17.6 % | — |
| Padding | `=` required | none | none | none | none |
| Arbitrary input length | yes | yes | **no** (multiples of 4) | yes | yes |
| Readable output for text-like input | no | no | no | no | **yes, partially** |

**Bold** marks the smallest output in each row.

Base85N's alphabet deliberately excludes the characters that force escaping in
common container formats, so encoded output can be dropped into JSON strings,
XML text nodes, and HTML bodies without a second escaping layer. (That is about
*not needing an extra encoding step* — it is **not** a substitute for
context-appropriate output escaping; see [SECURITY.md](SECURITY.md).)

That is where the XML rows come from. Ascii85, Z85 and RFC 1924 Base85 all look
cheaper than Base64 until their alphabets meet a container format: `<`, `>` and
`&` must be escaped, and all three end up *larger than Base64*. Base85N's ratio
does not move — so its lead over the other Base85 variants grows from 4–9 % raw
to **18–23 % in XML**.

### Where the alternatives are the better choice

The benchmark is equally explicit about this, and so is this README:

- **Ascii85 on sparse binaries.** Its zero-run shorthand encodes the zero-padded
  ELF sample at 1.026 against Base85N's 1.246 — 18 % smaller. Base85N has no
  equivalent and cannot close that gap without a format change.
- **Speed.** In a like-for-like scalar C harness, Ascii85 and Z85 encode at
  ~400 MB/s and Base85N at 97–150 MB/s: roughly **3–4× slower**, because it
  decides between two modes where they simply expand. Z85 decodes ~3× faster.
- **Z85 for addressable data.** Its fixed 4→5 mapping means a byte offset
  converts to a character offset by arithmetic, so random access, seeking and
  parallel chunked processing are trivial. Base85N's output length is
  data-dependent, so none of that is possible.
- **Maturity.** Ascii85 is in PDF and PostScript, Z85 is a ZeroMQ standard,
  RFC 1924 Base85 ships in Python's standard library. Base85N is a 0.x draft
  whose defects are still being found — the benchmark itself surfaced one.

Base85N is a good fit for identifiers, keys, tokens, and structured payloads
that are embedded in text formats — especially mixed payloads where part of the
data is text-like and part is binary. It is not a compression format, and it is
not a security mechanism.

## Implementations

Five conformant implementations of the same specification live in this
repository. All of them are dependency-free at runtime and verified against one
shared set of golden vectors.

| Language | Directory | Test command | Notes |
|---|---|---|---|
| Rust | [`rust/`](rust/) | `cargo test` | `encode` / `decode`, typed `DecodeError` |
| Go | [`go/`](go/) | `go test ./...` | `Encode` / `Decode`, sentinel errors |
| TypeScript | [`typescript/`](typescript/) | `npm test` | ESM, strict mode, `Uint8Array` |
| C | [`c/`](c/) | `make test` | C11, no deps, ASan/UBSan in CI |
| Python | [`python/`](python/) | `pytest` | original reference implementation |

Nothing here is published to crates.io, npm, PyPI, or any other registry — see
[SECURITY.md § What you, as a user, should do](SECURITY.md#what-you-as-a-user-should-do).

### Quick start

```rust
// Rust
let encoded = base85n::encode(b"hello, world!");
let decoded = base85n::decode(&encoded)?;
```

```go
// Go
encoded := base85n.Encode([]byte("hello, world!"))
decoded, err := base85n.Decode(encoded)
```

```ts
// TypeScript
import { encode, decode } from "base85n";
const encoded = encode(new TextEncoder().encode("hello, world!"));
const decoded = decode(encoded); // throws Base85NDecodeError on bad input
```

```python
# Python
from base85n import encode, decode
encoded = encode(b"hello, world!")
decoded = decode(encoded)  # raises Base85NDecodeError on bad input
```

```c
/* C */
char *out; size_t out_len;
if (base85n_encode(data, data_len, &out, &out_len) == BASE85N_OK) {
    /* ... */ free(out);
}
```

Each directory has its own README with the full API, build instructions, and a
description of what its test suite covers.

## Specification

The normative document is **[`spec/base85n-v0.2.0.md`](spec/base85n-v0.2.0.md)**
(version 0.2.0, draft, 2026-08-10). It is also published on the
[project website](https://keywan-ghadami.github.io/base85n/spec/).

It covers the alphabet and R-Set (§4), the encoding algorithm including the
two-pass Dynamic Passthrough procedure (§6) and the linear-time bound every
encoder must meet (§6.6), decoding (§7), the signal format (§9), the error
conditions every decoder must detect (§10), and security considerations (§13).

Specification versions are immutable: a published version is never edited in
place, and changes go into a new version. See [`spec/README.md`](spec/README.md)
for the version index. **While the spec is at 0.x the wire format is not
frozen** — do not persist data you must still be able to decode after an
upgrade.

## Test vectors

[`testvectors/`](testvectors/) holds the shared, language-independent vectors,
in both JSON and TSV form:

- [`vectors.json`](testvectors/vectors.json) — golden encode/decode pairs. Every
  implementation's test suite verifies all of them in both directions.
- [`adversarial_vectors.json`](testvectors/adversarial_vectors.json) — decoder
  robustness against hostile input: malformed signals, dangling escapes,
  truncated segments, and multi-byte Unicode placed where character-position
  bugs live. Each entry is either "must be rejected with this error code, and
  must not crash" or "spec-legal but unreachable from any conforming encoder".

If you write a sixth implementation, these are the vectors to run against.

## Building and testing everything

```sh
(cd rust       && cargo test)
(cd go         && go vet ./... && go test ./...)
(cd typescript && npm ci && npm test)
(cd c          && make test)          # ASan/UBSan when the toolchain supports it
(cd python     && pip install -e ".[test]" && python -m pytest)
```

CI runs all five on every push and pull request, across multiple compilers and
language versions — see [`.github/workflows/ci.yml`](.github/workflows/ci.yml).

## Security

Base85N is an **encoding**, not encryption. It provides no confidentiality, no
integrity, and no authenticity, and encoded output is trivially reversible.

> ⚠️ **Decoding strings from untrusted sources is the risky operation here.**
> A decoder parses attacker-controlled lengths and escape sequences. Bound the
> input size, handle the error path explicitly, and treat decoded output as
> untrusted binary — not as text.

[**SECURITY.md**](SECURITY.md) documents the threat model, the assurance
measures already in place, the ones still outstanding (no fuzzing, no external
audit, no signed releases), and what you should do as a user — including how to
check that the code you got actually came from here and is unmodified.

Report vulnerabilities privately to **keywan.ghadami@gmail.com**.

## Contributing

Issues and pull requests are welcome, particularly:

- an independent review of the specification or of any implementation;
- fuzzing harnesses (`cargo-fuzz`, libFuzzer, Atheris, `go test -fuzz`) — the
  largest known gap;
- differential testing between the five implementations;
- a sixth implementation, verified against `testvectors/`.

A change that alters encoder output or decoder acceptance must update the
specification and the shared vectors together, and must keep all five
implementations passing.

## License

Everything in this repository — the specification, all five implementations,
the test vectors, the tooling and the website — is licensed under the
**[Mozilla Public License 2.0](LICENSE)** (`MPL-2.0`).

MPL-2.0 is a file-level copyleft licence: you can use these files in a larger
work under whatever licence you like, including a proprietary one, but if you
modify a file that is covered by the MPL, that file must stay under the MPL and
its source must be made available. Every source file carries the Exhibit A
notice, and all package manifests declare `MPL-2.0`.
