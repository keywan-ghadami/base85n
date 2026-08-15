# Base85N

[![CI](https://github.com/keywan-ghadami/base85n/actions/workflows/ci.yml/badge.svg)](https://github.com/keywan-ghadami/base85n/actions/workflows/ci.yml)
[![Pages](https://github.com/keywan-ghadami/base85n/actions/workflows/pages.yml/badge.svg)](https://keywan-ghadami.github.io/base85n/)
[![Spec](https://img.shields.io/badge/spec-v0.4.0%20draft-blue)](spec/base85n-v0.4.0.md)
[![License](https://img.shields.io/badge/license-MPL--2.0-green)](LICENSE)

**A binary-to-text encoding that is denser than Base64 — and, for text-like
input, stays readable.**

Base85N packs 4 bytes into 5 characters from a single 85-character,
protocol-friendly alphabet (Alphabet-N). On top of that core it adds two
adaptive modes, and the output needs no padding.

**Dynamic Passthrough (DP)** carries a run of text-like input at exactly one
character per byte instead of expanding it. The 13 "R-Set" characters that
real text is full of but the alphabet excludes — space, quotes, comma,
newline, `<`, `>`, `&`, … — are written as stand-ins borrowed from the
alphabet's rarest characters. A segment's 5-character signal names which of
the 13 it contains and which of eight **donor profiles** lends the stand-ins,
so the substitution is built per segment rather than chosen from a fixed set.

**Solid Fill** carries a run of up to 2048 identical bytes in the five
characters of its signal alone — the zero padding in an object file, the
indentation in pretty-printed JSON, a rule of dashes in Markdown.

Encoded data almost never travels alone — it sits in a JSON field, an XML
node, an HTML attribute. That is where the difference shows up. A 37-byte
JSON payload carried inside another JSON document:

```
Base64   {"body":"eyJ1c2VyIjoiYWRhIiwiaWQiOjQyLCJyb2xlIjoiYWRtaW4ifQ=="}    52 chars
Ascii85  {"body":"HQmTRATAtU,%5\"j+tOpPA0O&k1+XViDeru/3[/!CD/!l3I/"}        48 chars
Base85N  {"body":"%nU$w{~user~:~ada~^~id~:42^~role~:~admin~}"}              42 chars
```

Base85N's line is not just the shortest, it is still *readable*: `user`,
`ada`, `id`, `42`, `role`, `admin` survive the round trip. `~` stands in for
`"`, `^` for `,`, and the leading `%nU$w` is the 5-character signal naming
which R-Set characters the segment contains, which profile lends their
stand-ins, and how long the segment is. Ascii85 needs a backslash in the
middle of its payload, because its alphabet contains `"`.

On binary there is no text structure to exploit, so every Base85 lands on the
same 5:4 ratio — until you put the result somewhere. 32 bytes in an HTML
attribute:

```
Base64   <img data-thumb="AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=">              44 chars
Ascii85  <img data-thumb="!!*-'&quot;9eu7#RLhG$k3[W&amp;.oNg'GVB&quot;(`=52*$$(B">    54 chars
Base85N  <img data-thumb="009c61o!#m2NH?C3~iWS5d]J*6CRx17-skh9337x">                  40 chars
```

Ascii85 starts out 4 characters ahead of Base64 and ends up 10 behind it,
because `"` becomes `&quot;` and `&` becomes `&amp;`. Base85N's alphabet
contains none of `"` `'` `\` `` ` `` `<` `>` `&`, so its 40 characters are
40 characters wherever you put them.

- 📖 **[Specification v0.4.0](spec/base85n-v0.4.0.md)** — the normative document
- 🌐 **[Project website](https://keywan-ghadami.github.io/base85n/)**
- 📊 **[Benchmarks](bench/results/RESULTS.md)** — size and throughput against
  Base64, Ascii85, Z85 and RFC 1924 Base85, including where those alternatives
  beat Base85N
- 🔐 **[Security policy](SECURITY.md)** — read this before decoding untrusted input
- ⚠️ **[AI-generated code notice](#ai-generated-code--notice)** — read this before using any of it

---

## AI-generated code — notice

⚠️ **Everything in this repository — the specification text, every
implementation, and their test suites — was written with substantial AI
assistance. It has not been independently reviewed or audited by a human
security expert.**

What that means in practice:

- The code is *plausible* and it *passes its tests*, and its tests were largely
  written by the same process that wrote the code. Shared blind spots between
  implementation and test are the expected failure mode here, and no amount of
  green CI rules them out.
- Four independent implementations cross-checked against shared vectors is a
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

Every figure below is measured over 6.52 MB of real files — binaries, an
uncompressed source tarball, JSON, JavaScript, CSS and Python source,
specification text, a changelog, images — plus a set of short protocol fields.
Expansion is encoded characters per input byte; lower is better. Full method and
raw numbers: **[benchmark results](bench/results/RESULTS.md)**.

| | Base64 | Ascii85 | Z85 | Base85 (RFC 1924) | **Base85N** |
|---|---|---|---|---|---|
| Whole corpus | 1.3333 | 1.1881 | 1.2500 | 1.2500 | **1.0243** |
| Pretty-printed JSON | 1.333 | 1.250 | 1.250 | 1.250 | **0.892** |
| Minified JSON | 1.333 | 1.250 | 1.250 | 1.250 | **1.003** |
| CommonMark spec text | 1.333 | 1.250 | 1.250 | 1.250 | **0.859** |
| JavaScript source | 1.333 | 1.250 | 1.250 | 1.250 | **1.003** |
| Python source | 1.333 | 1.250 | 1.250 | 1.250 | **0.962** |
| Uncompressed tar | 1.333 | 1.015 | 1.250 | 1.250 | **0.763** |
| Zero-padded ELF | 1.333 | **1.026** | 1.250 | 1.250 | 1.126 |
| WebAssembly module | 1.333 | 1.247 | 1.250 | 1.250 | **1.242** |
| TrueType font | 1.333 | 1.240 | 1.250 | 1.250 | **1.237** |
| JPEG photograph | 1.333 | 1.250 | 1.250 | 1.250 | **1.249** |
| **…whole corpus inside XML** | 1.3333 | 1.3990 | 1.3570 | 1.3510 | **1.0243** |
| **…what that costs vs Base85N** | +30.2 % | +36.6 % | +32.5 % | +31.9 % | — |
| Padding | `=` required | none | none | none | none |
| Arbitrary input length | yes | yes | **no** (multiples of 4) | yes | yes |
| Readable output for text-like input | no | no | no | no | **yes, partially** |

A ratio below 1.0 means the encoded text is *shorter than the input bytes*:
Dynamic Passthrough spends one character per byte, and Solid Fill spends five
characters on runs that would otherwise cost hundreds.

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

- **Ascii85 on sparse binaries.** Its zero-run shorthand still encodes the
  zero-padded ELF sample at 1.026 against Base85N's 1.126. Solid Fill closed
  most of that gap (1.246 in 0.3.x) but not all of it: Ascii85 spends one
  character per four zero bytes with no threshold and no signal, where a Fill
  signal costs five characters and only fires from five bytes up.
- **Speed.** In a like-for-like scalar C harness Base85N is the slowest encoder
  of the four on every sample. Base64 is 4–7× faster, Ascii85 and Z85 roughly
  1.5–2×. Decoding is competitive on binary and behind on text. If you are
  bound by CPU rather than by bytes, this is the wrong codec.
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

Four conformant implementations of the same specification live in this
repository, plus Python bindings to one of them. All of them are dependency-free
at runtime and verified against one shared set of golden vectors.

| Language | Directory | Test command | Notes |
|---|---|---|---|
| Rust | [`rust/`](rust/) | `cargo test` | `encode` / `decode`, typed `DecodeError`, plus a C ABI |
| Go | [`go/`](go/) | `go test ./...` | `Encode` / `Decode`, sentinel errors |
| TypeScript | [`typescript/`](typescript/) | `npm test` | ESM, strict mode, `Uint8Array` |
| C | [`c/`](c/) | `make test` | C11, no deps, ASan/UBSan in CI |
| Python | [`python/`](python/) | `pytest` | PyO3 bindings to the Rust crate, built by maturin |

Python is **bindings, not a fifth implementation**: version 0.4.0 replaced the
hand-written Python encoder with a thin PyO3 layer over `rust/`, so what Python
runs is the same code the Rust and C-ABI callers get. One implementation fewer
is one implementation fewer to keep in step — and the cross-checking that
matters still has four independent ones behind it.

Nothing here is published to crates.io, npm, PyPI, or any other registry — see
[SECURITY.md § What you, as a user, should do](SECURITY.md#what-you-as-a-user-should-do).

**Binding from another language?** The Rust crate also builds as a C library —
`libbase85n.so` / `.a` behind [`rust/include/base85n.h`](rust/include/base85n.h),
ABI-identical to the C implementation's header. Use that rather than the C
implementation: it is the same calling convention with a bounds-checked parser
behind it. See [`rust/README.md` § Using it from C](rust/README.md#using-it-from-c-and-other-languages).

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
# Python (pip install ./python, needs a Rust toolchain)
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

The normative document is **[`spec/base85n-v0.4.0.md`](spec/base85n-v0.4.0.md)**
(version 0.4.0, draft, 2026-08-15). It is also published on the
[project website](https://keywan-ghadami.github.io/base85n/spec/).

It covers the alphabet, the R-Set and the eight donor profiles (§4), the
encoding algorithm (§6) and the linear-time bound every encoder must meet
(§6.6), decoding (§7), the three signal ranges (§9), the error conditions every
decoder must detect (§10), and security considerations (§13). §14 records what
has been measured, and on what.

Specification versions are immutable: a published version is never edited in
place, and changes go into a new version. See [`spec/README.md`](spec/README.md)
for the version index. **While the spec is at 0.x the wire format is not
frozen** — 0.4.0 changed it again, and output produced under 0.3.x does not
decode under 0.4.0. Do not persist data you must still be able to decode after
an upgrade.

## Test vectors

[`testvectors/`](testvectors/) holds the shared, language-independent vectors,
in both JSON and TSV form:

- [`vectors.json`](testvectors/vectors.json) — golden encode/decode pairs. Every
  implementation's test suite verifies all of them in both directions.
- [`adversarial_vectors.json`](testvectors/adversarial_vectors.json) — decoder
  robustness against hostile input: undefined signals, truncated segments,
  every profile identifier over the same segment data, the boundaries of all
  three signal ranges, Fill expansion, non-canonical final blocks, and
  multi-byte Unicode placed where character-position bugs live. Each entry is either "must be rejected with this error code, and
  must not crash" or "spec-legal but unreachable from any conforming encoder".

If you write a sixth implementation, these are the vectors to run against.

## Building and testing everything

```sh
(cd rust       && cargo test)
(cd go         && go vet ./... && go test ./...)
(cd typescript && npm ci && npm test)
(cd c          && make test)          # ASan/UBSan when the toolchain supports it
(cd python     && pip install ".[test]" && python -m pytest)   # needs cargo
python3 tools/check_vectors.py                                # shared vectors
```

CI runs all of them on every push and pull request, across multiple compilers and
language versions — see [`.github/workflows/ci.yml`](.github/workflows/ci.yml).

## Security

Base85N is an **encoding**, not encryption. It provides no confidentiality, no
integrity, and no authenticity, and encoded output is trivially reversible.

> ⚠️ **Decoding strings from untrusted sources is the risky operation here.**
> A decoder parses attacker-controlled lengths — including a Fill signal's,
> which produces up to 2048 bytes from five characters. Bound the input size,
> handle the error path explicitly, and treat decoded output as untrusted
> binary — not as text.

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
- differential testing between the four implementations;
- a fifth implementation, verified against `testvectors/`.

A change that alters encoder output or decoder acceptance must update the
specification and the shared vectors together, and must keep every
implementation passing.

## License

Everything in this repository — the specification, every implementation,
the test vectors, the tooling and the website — is licensed under the
**[Mozilla Public License 2.0](LICENSE)** (`MPL-2.0`).

MPL-2.0 is a file-level copyleft licence: you can use these files in a larger
work under whatever licence you like, including a proprietary one, but if you
modify a file that is covered by the MPL, that file must stay under the MPL and
its source must be made available. Every source file carries the Exhibit A
notice, and all package manifests declare `MPL-2.0`.
