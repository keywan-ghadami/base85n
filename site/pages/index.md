# Base85N

Base85N packs 4 bytes into 5 characters from a single 85-character,
protocol-friendly alphabet. On top of that core it adds an adaptive **Dynamic
Passthrough (DP)** mode: when a run of input already consists of characters the
alphabet can carry, the encoder passes those bytes through nearly one-to-one
instead of expanding them, substituting a small fixed set of characters (space,
quotes, comma, newline, `<`, `>`, `&`, …) with safe stand-ins. The encoder picks
whichever mode is shorter, per segment. The output needs no padding.

<div class="hero-compare">
<pre><code>Base64   {"body":"eyJ1c2VyIjoiYWRhIiwiaWQiOjQyLCJyb2xlIjoiYWRtaW4ifQ=="}    52 chars
Ascii85  {"body":"HQmTRATAtU,%5\"j+tOpPA0O&amp;k1+XViDeru/3[/!CD/!l3I/"}        48 chars
Base85N  {"body":"%nSAJ{$user$:$ada$%$id$:42%$role$:$admin$}"}              42 chars</code></pre>
</div>

A 37-byte JSON payload carried inside another JSON document. Base85N's line is
not just the shortest, it is still *readable* — `user`, `ada`, `id`, `42`,
`role`, `admin` survive the round trip. Ascii85 needs a backslash in the middle
of its payload, because its alphabet contains `"`.

On binary every Base85 lands on the same 5:4 ratio — until you put the result
somewhere. The same 32 bytes in an HTML attribute cost **44** characters as
Base64, **54** as Ascii85 (`"` becomes `&quot;`, `&` becomes `&amp;`) and
**40** as Base85N, whose alphabet has nothing that needs escaping.

[Read the specification](spec/base85n-v0.3.0.md){: .cta }
[Benchmark results](bench/results/RESULTS.md){: .cta .secondary }
[Security policy](SECURITY.md){: .cta .secondary }
[Source on GitHub](https://github.com/keywan-ghadami/base85n){: .cta .secondary }

> ### ⚠️ Read this first {: #ai-generated-code--notice }
>
> **AI-generated.** The specification text, all five implementations, and their
> test suites were produced with substantial AI assistance and have **not** been
> independently reviewed or audited. They pass their tests — and those tests
> were written by the same process that wrote the code, so shared blind spots
> are the expected failure mode. Read the code before you ship it.
>
> **Decoding untrusted input is the risky operation.** A Base85N decoder parses
> attacker-controlled lengths and escape sequences. Bound your input size,
> handle the error path, and treat decoded output as untrusted binary. There is
> no fuzzing and no external audit yet — see the
> [security policy](SECURITY.md) for the full list of gaps.
>
> **Draft format.** At 0.x the wire format is not frozen. Do not persist data
> you must still be able to decode after an upgrade.

## What it is good for

- **Identifiers, keys and tokens embedded in text formats.** The alphabet
  deliberately excludes the characters that force escaping in JSON, XML and
  HTML, so encoded output can be dropped in without a second escaping layer.
- **Mixed payloads.** Data that is part text-like and part binary gets the
  passthrough treatment where it helps and dense block encoding where it does
  not — decided per segment, by the encoder.
- **Debuggability.** Text-like payloads stay partly readable in logs and on the
  wire.

Base85N is not compression, and it is not a security mechanism. Being
"JSON-safe" is about not needing an extra encoding step; it is not a substitute
for context-appropriate output escaping.
{: .note }

## How it measures up

Measured over 4.94 MB of real files — binaries, JSON, specification text,
images — plus a set of short protocol fields. Expansion is encoded characters
per input byte; lower is better.

| | Base64 | Ascii85 | Z85 | Base85 (RFC 1924) | **Base85N** |
|---|---|---|---|---|---|
| Whole corpus | 1.3333 | 1.1996 | 1.2500 | 1.2500 | **1.1503** |
| Pretty-printed JSON | 1.333 | 1.250 | 1.250 | 1.250 | **1.033** |
| CommonMark spec text | 1.333 | 1.250 | 1.250 | 1.250 | **1.123** |
| TrueType font | 1.333 | **1.240** | 1.250 | 1.250 | 1.248 |
| JPEG photograph | 1.333 | 1.250 | 1.250 | 1.250 | **1.250** |
| Zero-padded ELF | 1.333 | **1.026** | 1.250 | 1.250 | 1.246 |
| …carried inside XML | 1.3333 | 1.4171 | 1.3662 | 1.3530 | **1.1503** |
| **…what that costs vs Base85N** | +15.9 % | +23.2 % | +18.8 % | +17.6 % | — |

**Bold** marks the smallest output in each row.

The XML rows decide most real deployments. Ascii85, Z85 and RFC 1924 Base85 all
look cheaper than Base64 until their alphabets meet a container format: `<`, `>`
and `&` have to be escaped, and all three end up *larger than Base64*. Base85N's
ratio does not move, because there is nothing in its output to escape.

Two things this project does not hide: **Ascii85 wins on zero-padded binaries**
via its zero-run shorthand, and **Base85N encodes text 3–4× slower** than
Ascii85 and Z85 — text is where the mode decision has work to do, and where the
size win is largest. On binary it is level with them, and on the image samples
it is now the fastest encoder in the comparison.

[Full results, including where the alternatives win](bench/results/RESULTS.md){: .cta .secondary }

## Five implementations, one set of vectors

Every implementation is dependency-free at runtime and verified against the same
shared golden and adversarial vectors. A divergence in any one of them shows up
as a test failure rather than as silently different output.

<ul class="cards">
<li><strong><a href="rust/README.md">Rust</a></strong>
<code>cargo test</code> — typed <code>DecodeError</code>, clippy clean.</li>
<li><strong><a href="go/README.md">Go</a></strong>
<code>go test ./...</code> — sentinel errors, <code>go vet</code> clean.</li>
<li><strong><a href="typescript/README.md">TypeScript</a></strong>
<code>npm test</code> — ESM, strict mode, <code>Uint8Array</code>.</li>
<li><strong><a href="c/README.md">C</a></strong>
<code>make test</code> — C11, no dependencies, <code>-Werror</code>, ASan/UBSan.</li>
<li><strong><a href="python/README.md">Python</a></strong>
<code>pytest</code> — the original reference implementation, which generated the
shared vectors.</li>
</ul>

Nothing here is published to crates.io, npm, PyPI or any other registry. If you
found a "base85n" package on a registry, it did not come from this project —
see [what you should do as a user](SECURITY.md#what-you-as-a-user-should-do).

## How it works, in one paragraph

The encoder walks the input and, at each position, identifies the longest prefix
it could carry in Dynamic Passthrough mode. It does that in two passes: the
first pass scans forward collecting which substitutable characters occur,
bounded only by whether each byte is representable at all; the second pass
re-scans the same window with that substitution mask *held fixed*, applying the
escaping rules to find the actual boundary. Fixing the mask before any escaping
decision is what makes the encoding well-defined — otherwise a character
encountered late in the scan could retroactively change how an earlier byte had
been encoded. The resulting prefix is then measured against what plain block
encoding would cost, and used only if it is at least as compact. Everything else
goes through the 4-byte-to-5-character block path, with partial-block padding
deferred to the genuinely final block so a decoder can never mistake it for the
start of the next group.

The full normative treatment is in
[Section 6 of the specification](spec/base85n-v0.2.0.md#6-encoding-algorithm).

## Test vectors

Two shared, language-independent vector sets live in
[`testvectors/`](testvectors), in JSON and TSV form:

- **`vectors.json`** — golden encode/decode pairs, verified in both directions
  by all five test suites.
- **`adversarial_vectors.json`** — decoder robustness against hostile input:
  malformed signals, dangling escapes, truncated segments, and multi-byte
  Unicode placed exactly where "character position" can diverge from a
  language's actual storage unit. Each entry is either "must be rejected with
  this error code, and must not crash", or "spec-legal but unreachable from any
  conforming encoder".

If you write a sixth implementation, these are the vectors to run it against.

## Reporting problems

Security issues: **<keywan.ghadami@gmail.com>** (see the
[security policy](SECURITY.md)). Everything else:
[GitHub issues](https://github.com/keywan-ghadami/base85n/issues).
