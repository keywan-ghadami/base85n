# Base85N

[![CI](https://github.com/keywan-ghadami/base85n/actions/workflows/ci.yml/badge.svg)](https://github.com/keywan-ghadami/base85n/actions/workflows/ci.yml)
[![Pages](https://github.com/keywan-ghadami/base85n/actions/workflows/pages.yml/badge.svg)](https://keywan-ghadami.github.io/base85n/)
[![Scorecard](https://api.securityscorecards.dev/projects/github.com/keywan-ghadami/base85n/badge)](https://scorecard.dev/viewer/?uri=github.com/keywan-ghadami/base85n)
[![Spec](https://img.shields.io/badge/spec-v0.5.0%20final-brightgreen)](spec/base85n-v0.5.0.md)
[![License](https://img.shields.io/badge/license-MPL--2.0-green)](LICENSE)

**An encoding for data that has to live inside a text format — JSON, XML, HTML,
configuration files, APIs. Denser than Base64, and for text-like input it stays
readable.**

Base64's everyday job is rarely "turn bytes into text" for its own sake — it is
*getting data through a text format*: a payload in a JSON field, a blob in an
HTML attribute, a value in a YAML file, a token in a config. **Base85N is built
for that job.** It is meant to replace Base64 where Base64 is used to embed data
in a text-based format and the size of the result, or how clean and readable it
looks, matters.

Base64 is a great general-purpose encoding — established, interoperable, in
every standard library, and the right choice in plenty of places. Base85N is the
specialised one: the same job in fewer characters, with output that drops into a
container format without a second escaping layer.

Typical contexts:

- JSON inside JSON, and JSON inside HTML
- HTML attributes and XML text nodes
- configuration files, log lines, CSV fields
- request and response bodies in APIs
- generally: nested or embedded data inside a text format

In those containers the output needs no escaping at all — with four exceptions
worth knowing before you start, chief among them a JavaScript template literal:
[**Embedding: where the output can be pasted verbatim**](#embedding-where-the-output-can-be-pasted-verbatim).

**The input does not have to be binary.** Developers reach for Base64 on binary
data most often, but the problem being solved there is the container, not the
bytes. Text carries its own hazards inside a text format: quotes and
backslashes, `<` `>` `&`, newlines and control characters, escape sequences that
get processed twice on the way through two nesting levels, Unicode and umlauts
that each layer normalises or re-encodes differently, characters that are syntax
at one level and data at the next. Base85N encodes whatever you hand it — text,
binary, or a mix of both — into characters that carry no syntactic meaning in the
usual containers.

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

**Fill** carries a run of up to 2048 identical bytes in the five characters of
its signal alone — the zero padding in an object file, the indentation in
pretty-printed JSON, a rule of dashes in Markdown. A second variant of the same
signal carries a short zero run *together with the two bytes beside it*, so a
run that stops one or two bytes short of a group boundary does not hand them
back to the 5:4 core.

The difference shows up where the encoded text actually goes. A 37-byte JSON
payload carried inside another JSON document:

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
contains none of `"` `'` `\` `<` `>` `&`, so its 40 characters stay 40
characters in an HTML attribute, an XML node and a JSON string alike.

- 📖 **[Specification v0.5.0](spec/base85n-v0.5.0.md)** — the normative
  document, final and self-contained
- 🌐 **[Project website](https://keywan-ghadami.github.io/base85n/)**
- 📊 **[Benchmarks](bench/results/RESULTS.md)** — size and throughput against
  Base64, Ascii85, Z85 and RFC 1924 Base85, including where those alternatives
  beat Base85N
- 🔐 **[Security policy](SECURITY.md)** — read this before decoding untrusted input
- 🤝 **[How this was built — and where review is welcome](#how-this-was-built--and-where-review-is-welcome)**
  — AI assistance, the evidence that is in place, and the reviews this project
  is asking for

---

## How this was built — and where review is welcome

The specification, all four implementations and their test suites were written
with substantial AI assistance. That is stated because you should know how the
code in front of you came to be; it is not offered as a verdict on it, in either
direction.

What the project has instead of a pedigree is evidence you can check:

- four independent implementations of one specification, cross-checked against a
  shared set of golden and adversarial vectors, so a rule that only one of them
  believes shows up as a test failure;
- three libFuzzer targets under AddressSanitizer and UndefinedBehaviorSanitizer
  — encoder round trip, decoder against arbitrary input, and C against Rust in
  one process — plus 6,146 generated differential cases;
- complexity regression tests asserting sub-quadratic encoding in every
  language, and a benchmark suite that round-trip verifies every measurement it
  reports;
- a specification that is self-contained and small enough to read in a sitting,
  with [what has been measured and what is knowingly left
  undone](SECURITY.md#measures-still-outstanding) written down rather than
  implied.

### Reviews wanted — this is the most useful thing you can contribute

Every kind of review is welcome, from anyone, on any part of this:

- **Security.** The decoder is the interesting surface: it parses
  attacker-controlled lengths, and one Fill signal turns five characters into up
  to 2048 bytes. Sections 7, 10 and 13 of the
  [specification](spec/base85n-v0.5.0.md) are where to start, or point a fuzzer
  at any implementation and report what falls out.
- **Documentation.** Is the specification implementable from the document alone?
  Does this README tell you what Base85N is for in the first thirty seconds? Say
  where you got lost — that is a bug report.
- **Usability.** The API is two functions per language. Are the error types the
  ones you would want to catch, is the build what you expect, is the naming
  right, does it feel at home in your language?
- **Anything that excites you.** A benchmark number you do not believe, a
  cleaner way to select donor profiles, a language you want bindings for, a use
  case nobody here has thought of, an argument that a design decision is wrong.

Open an issue or a pull request, or write to **keywan.ghadami@gmail.com**.

And read the code before you ship it — it is deliberately small, and that is a
design property rather than an accident.

---

## Why Base85N

Two questions tend to come up in this order. *Base64 costs a third more
characters than the data it carries — can that be cheaper?* And then, once the
answer is yes: *why not simply use one of the Base85 variants that already
exist?* Everything below answers those two, in that order.

Every figure below is measured over 6.52 MB of real files — binaries, an
uncompressed source tarball, JSON, JavaScript, CSS and Python source,
specification text, a changelog, images — plus a set of short protocol fields.
Expansion is encoded characters per input byte; lower is better. Full method and
raw numbers: **[benchmark results](bench/results/RESULTS.md)**.

The same benchmark also runs the **Silesia corpus** — the twelve files, 202 MiB,
that compression work has reported against since 2003, and which nobody here
chose. Base85N encodes it at **1.051** characters per input byte, against
Base64's 1.333, Ascii85's 1.203 and 1.250 for both Z85 and RFC 1924, and it is
the smallest of the five on 11 of the 12 files — on all twelve once the output
is placed in JSON, HTML or XML.

| | Base64 | Ascii85 | Z85 | Base85 (RFC 1924) | **Base85N** |
|---|---|---|---|---|---|
| Whole corpus | 1.3333 | 1.1881 | 1.2500 | 1.2500 | **1.0070** |
| Pretty-printed JSON | 1.333 | 1.250 | 1.250 | 1.250 | **0.935** |
| Minified JSON | 1.333 | 1.250 | 1.250 | 1.250 | **1.003** |
| CommonMark spec text | 1.333 | 1.250 | 1.250 | 1.250 | **0.859** |
| JavaScript source | 1.333 | 1.250 | 1.250 | 1.250 | **1.004** |
| Python source | 1.333 | 1.250 | 1.250 | 1.250 | **0.973** |
| Uncompressed tar | 1.333 | 1.015 | 1.250 | 1.250 | **0.767** |
| Zero-padded ELF | 1.333 | 1.026 | 1.250 | 1.250 | **0.965** |
| WebAssembly module | 1.333 | 1.247 | 1.250 | 1.250 | **1.239** |
| TrueType font | 1.333 | 1.240 | 1.250 | 1.250 | **1.232** |
| JPEG photograph | 1.333 | 1.250 | 1.250 | 1.250 | **1.249** |
| …whole corpus inside XML | 1.3333 | 1.3986 | 1.3571 | 1.3514 | **1.0070** |
| …what the escaping costs | +32.4 % | +38.9 % | +34.8 % | +34.2 % | **none** |
| …whole corpus in a URL query | **1.3543** | 2.0198 | 1.7148 | 1.6956 | 1.4632 |
| Padding | `=` required | **none** | **none** | **none** | **none** |
| Arbitrary input length | **yes** | **yes** | no (multiples of 4) | **yes** | **yes** |
| Readable output for text-like input | no | no | no | no | **yes, partially** |

A ratio below 1.0 means the encoded text is *shorter than the input bytes*, and
only Fill can do it: Dynamic Passthrough spends one character per byte plus a
signal, so a row under 1.000 is a file with runs of at least sixteen identical
bytes in it — indentation, block padding, a flat region of an image. Which
construct carried how much of each file is measured per file in
[bench/results/mode-mix.md](bench/results/mode-mix.md).

**Bold** — a green cell on the website — marks the best value in each row, and
every codec that reaches it when there is a tie. Every column has at least one:
the alphabet wins the embedded rows, and Base64 wins the URL row.

### If Base64 is too big, why not just use another Base85?

Because a share of that theoretical density is handed straight back at the moment
the output is embedded, and how much depends entirely on the alphabet. Ascii85,
Z85 and RFC 1924 Base85 all start out cheaper than Base64 — and all three
alphabets contain characters that a container format reserves. Inside XML `<`,
`>` and `&` have to be escaped, one character becomes four to six, and all three
end up *larger than Base64*: that is the XML row above.

Base85N's alphabet deliberately excludes the characters that force escaping in
common container formats, so encoded output can be dropped into JSON strings,
XML text nodes and attributes, and quoted HTML attributes without a second
escaping layer. (That is about *not needing an extra encoding step* — it is
**not** a substitute for context-appropriate output escaping; see
[SECURITY.md](SECURITY.md).)

Which containers that covers, and which four need care, is a table:
[**Embedding: where the output can be pasted verbatim**](#embedding-where-the-output-can-be-pasted-verbatim).

So Base85N's ratio does not move between the raw, JSON, HTML and XML tables,
while every other codec's does — and its lead over the other Base85 variants
grows from 4–19 % raw to **26–28 % in XML**. That is the case for a fifth
Base85: not that base 85 is denser than base 64, which was known, but that the
alphabet decides how much of that density survives being embedded.

The URL row is the same argument running the other way, and it is the one
embedding Base85N loses. `#`, `%`, `+`, `?` and `&` are in Alphabet-N *because*
they are free in JSON and XML; a URL encoder charges three characters for each
of them. In a query string use Base64url.

### Where the alternatives are the better choice

The benchmark is equally explicit about this, and so is this README:

- **Base64url in a URL.** Percent-encoding to RFC 3986's unreserved set charges
  three characters for five of Alphabet-N's punctuation characters, so over the
  corpus Base85N costs 1.463 in a query string against Base64's 1.354. This is
  the one embedding measured where Base85N is not the smallest, and it follows
  directly from the alphabet choice that wins the other three.
- **Raw encode speed against Base64.** In a like-for-like scalar C harness
  Base64 encodes fastest of the four on every sample, Base85N included: a
  6→8-bit repack is simply less work than a base conversion. Its margin is
  1.3× on high-entropy binary and 2–4× elsewhere. Against the other Base85s
  the picture is no longer one-sided — Base85N decodes fastest of the four on
  13 of 16 samples and encodes faster than Ascii85 and Z85 on 12 of them, both
  the structured text it is designed for and high-entropy binary, while
  trailing them on a zero-padded ELF. Encoding also parallelises —
  `encode_parallel` in the Rust crate and `encode(data, threads=...)` in
  Python reach 2.7× on four cores, with output identical to the
  single-threaded result.
- **Z85 for addressable data.** Its fixed 4→5 mapping means a byte offset
  converts to a character offset by arithmetic, so random access and seeking
  are trivial. Base85N's output length is data-dependent, so none of that is
  possible.
- **Maturity.** Ascii85 is in PDF and PostScript, Z85 is a ZeroMQ standard,
  RFC 1924 Base85 ships in Python's standard library. Base85N is a 0.x draft
  whose wire format has changed in every version so far, including this one,
  and whose defects are still being found — this version's parallel encoder
  surfaced one in the sequential encoder's lookahead.

So this is not a proposal to replace Base64 everywhere. Base85N can replace it
where Base64 is used to embed data in a text-based format and either the size or
the cleanliness of the result matters: structured payloads, identifiers, keys,
tokens, documents nested inside documents — and especially mixed content, where
part of the data is text-like and part is not. Where the container is a URL, or
where interoperability with everything that already exists outweighs a third of
the characters, Base64 remains the better answer. Base85N is also not a
compression format, and not a security mechanism.

## Embedding: where the output can be pasted verbatim

Alphabet-N was chosen to be free of the characters that JSON, XML and HTML
reserve — but it is not free of *every* special character, and the ones it does
contain are exactly the ones those three formats leave alone: `` ` ``, `$`, `{`,
`=`, `%`, `#`, `[`, `*`. Four containers care about those, so this table is the
honest version of "drops in anywhere".

The JSON, XML, HTML-attribute, YAML, TOML, CSV, JavaScript and shell rows were
checked by embedding encoded output in the container and parsing it back with a
real parser (`json`, `ElementTree`, PyYAML, `tomllib`, `csv`, `node`, `bash`);
the rest follow from the alphabet, and the alphabet claims underneath all of them
are pinned by
[`typescript/test/containers.test.ts`](typescript/test/containers.test.ts).

| Container | Verbatim? | Why |
|---|---|---|
| JSON string value | ✅ | no `"`, no `\`, no control characters |
| XML text node, XML/HTML attribute **quoted** | ✅ | no `<`, `>`, `&`, `"`, `'` |
| YAML/TOML **quoted** scalar | ✅ | same, and no line break to fold |
| CSV field | ✅ | no comma, no quote, no newline |
| SQL string literal | ✅ | no `'` and no `\` to terminate or escape it |
| Shell **single**-quoted word | ✅ | nothing expands inside `'…'` |
| JavaScript `'…'` / `"…"` string | ✅ | no quote, no backslash |
| HTTP header value, log line, filename | ✅ | no space, no control characters, ASCII only |
| **JS template literal** `` `…` `` | ❌ | `` ` `` ends it, `${` starts an interpolation |
| **Unquoted** HTML attribute | ❌ | `` ` `` and `=` are forbidden there by HTML5 |
| **Plain (unquoted) YAML** scalar | ❌ | output can start with `%`, `{`, `[`, `:`, `-`, `?`, `!`, `*`, `@` |
| Shell **double**-quoted or unquoted word | ❌ | `` ` `` and `$` substitute; `*`, `?`, `~`, `{` glob |
| URL query string | ⚠️ | 19 of 85 characters percent-encode — use Base64url |
| CSV opened by a spreadsheet | ⚠️ | a field starting `=`, `+`, `-`, `@` is a formula (this is [CSV injection](https://owasp.org/www-community/attacks/CSV_Injection), not a Base85N quirk) |
| Markdown prose | ⚠️ | `` ` ``, `*`, `_`, `[`, `]`, `~`, `#` are syntax — use a code span |

**The template-literal one is the trap, especially for a hardcoded value.** On
high-entropy input a backtick lands roughly one output character in 85 and `${`
about once every 8,000 characters, so the first few values you paste will work
and a later one will not. This is a real 24-byte payload:

```js
const p = `y?W`a*@M~_23-${^M{~tJ3$hlr8Jf!`;  // ← SyntaxError, and ${^M{~tJ3$hlr8Jf!} would interpolate
const p = "y?W`a*@M~_23-${^M{~tJ3$hlr8Jf!";  // ← fine, and needs no escaping
```

If you generate code, emit an ordinary string literal. The same shape of bug is
what makes an *unquoted* HTML attribute and a double-quoted shell word unsafe:
quote the value and all three go away.

None of this is a substitute for context-appropriate output escaping. Encoding
removes the need for a *second* encoding layer in the ✅ rows; it does not make
the value safe to interpolate into a context whose syntax it can still reach. See
[SECURITY.md](SECURITY.md).

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

Python is **bindings, not a fifth implementation**: the hand-written Python
encoder was replaced in 0.4.0 with a thin PyO3 layer over `rust/`, so what
Python runs is the same code the Rust and C-ABI callers get. One implementation fewer
is one implementation fewer to keep in step — and the cross-checking that
matters still has four independent ones behind it.

This repository is the canonical source; before depending on a copy of it from
anywhere else, see
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
# Python (pip install base85n)
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

The normative document is **[`spec/base85n-v0.5.0.md`](spec/base85n-v0.5.0.md)**
(version 0.5.0, final, 2026-08-16). It is self-contained — it is the only
document needed to implement Base85N — and it is also published on the
[project website](https://keywan-ghadami.github.io/base85n/spec/).

It covers the alphabet, the R-Set and the eight donor profiles (§4), the
encoding algorithm (§6) and the linear-time bound every encoder must meet
(§6.6), decoding (§7), the four signal ranges (§9), the error conditions every
decoder must detect (§10), and security considerations (§13). §11.3 explains
why encoding parallelises without a second canonical form, and §14 records what
has been measured, what it cost, and what is knowingly left undone.

**The wire format is frozen and the feature set is closed at 0.5.0.** No
further mode, signal or encoder option is going in, and no 0.x version will
change the format again. 1.0.0 is not claimed yet — that is a statement about
time in the field rather than about content — so the number stays at 0.5.0 and
the answer to "will this change?" is no.

That is a decision rather than an absence of ideas, and the two most recent
proposals are on record with their reasoning: a
[`--binary` encoder mode](spec/history/binary-flag-decision.md), declined
despite clearing its own speed threshold, and
[container flavors](spec/history/flavors-decision.md) — alternative alphabets
for the containers Alphabet-N does not fit through — not pursued, because four
of the six candidate containers are already safe without one and the only
survivor costs every donor profile.

Getting here did break the format twice: output produced under 0.4.0 does not
decode under 0.5.0. That is over. Earlier versions, the proposals behind the
format — including the ones that were measured and declined — and
[what the measurements got wrong](spec/history/lessons.md) are in
[`spec/history/`](spec/history/). None of it is needed to implement Base85N.

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
measures already in place, the ones still outstanding (no external review yet,
fuzzing only as a short CI regression run, no signed releases), and what you
should do as a user — including how to check that the code you got actually came
from here and is unmodified.

Report vulnerabilities privately to **keywan.ghadami@gmail.com**.

## Contributing

Issues and pull requests are welcome. The
[review this project is asking for](#reviews-wanted--this-is-the-most-useful-thing-you-can-contribute)
is the most valuable thing you can bring; concretely, that includes:

- a review of the specification or of any implementation — security,
  documentation, API ergonomics, or simply "this part is confusing";
- fuzzing beyond the three C targets: `cargo-fuzz`, Atheris, `go test -fuzz`, a
  campaign that runs for longer than CI's two minutes, or OSS-Fuzz;
- differential testing that covers Go and TypeScript, not only C against Rust;
- a fifth implementation, verified against `testvectors/`;
- bindings for a language that is not here yet.

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
