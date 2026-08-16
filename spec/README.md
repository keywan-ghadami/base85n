# Base85N specification versions

The specification is versioned. Each version is a separate, immutable file:
once a version is published, its normative text is not edited in place —
corrections and changes go into a new version.

| Version | Status | Date | Document |
|---|---|---|---|
| 0.4.0 | Draft (current) | 2026-08-15 | [`base85n-v0.4.0.md`](base85n-v0.4.0.md) |
| 0.3.1 | Superseded | 2026-08-13 | [`base85n-v0.3.1.md`](base85n-v0.3.1.md) |
| 0.3.0 | Superseded | 2026-08-12 | [`base85n-v0.3.0.md`](base85n-v0.3.0.md) |
| 0.2.0 | Superseded | 2026-08-10 | [`base85n-v0.2.0.md`](base85n-v0.2.0.md) |
| 0.1.0 | Superseded | 2026-08-10 | [`base85n-v0.1.0.md`](base85n-v0.1.0.md) |

Proposals that have been measured but not adopted live in
[`proposals/`](proposals/): what was tried, what it cost, and why the answer
was what it was.

Version 0.4.0 **changes the wire format**. Output produced under 0.3.x does not
decode correctly under 0.4.0; the implementations and the shared test vectors
move together, and no implementation in this repository reads the older format.

Two things changed, and a third followed from them.

**Dynamic Passthrough builds its substitution per segment.** 0.3.x picked one of
eight fixed *replacement alphabets*, each of which gave up a few Alphabet-N
characters whether or not the segment needed them. 0.4.0 splits that into a
13-bit **mask** naming the R-Set characters that actually occur and a 3-bit
**donor profile** giving the order in which stand-ins are spent, so a segment
with two R-Set characters in it gives up two Alphabet-N characters and not
eight. The signal payload grows from 13 bits to 27 — 3 profile + 13 mask + 11
length — which also raises the segment cap from 1024 to 2048 characters.
Measured on the derivation corpus, text overhead falls from 2.62 % to 0.54 %.

**Solid Fill is new.** A second signal range carries a run of up to 2048
identical bytes in five characters and reads no data characters at all: the
zero padding in an object file, the indentation in pretty-printed JSON, a rule
of dashes in Markdown. It is also the only construct whose output is not
bounded by its input, which is why Section 13 now states a decompression bound
(410:1) and Section 7.4 caps a single signal.

**The final block must now be canonical.** 0.3.x accepted any trailing group
that `#`-padded below 2^32, which let several character sequences decode to the
same bytes. 0.4.0 requires the trailing group to be exactly what encoding those
bytes produces, so the encoding of a byte string is unique.

Two error conditions were renamed with the ranges they now describe:
`RESERVED_SIGNAL` became `UNDEFINED_SIGNAL` (a value in `FUTURE_SIGNAL_SPACE`),
and `INVALID_PARTIAL_BLOCK` became `INVALID_FINAL_BLOCK`.

Measured over the repository's 6.52 MB benchmark corpus — the same files
through both versions — encoded characters per input byte fall from **1.11407
to 1.02435** overall. Per file: pretty-printed JSON 1.00513 to 0.89239, the
CommonMark specification 1.01961 to 0.85889, a Python module 1.00903 to
0.96166, an uncompressed tar 1.07605 to 0.76266, a zero-padded ELF 1.24640 to
1.12618. Section 14.3 of the document records the configurations behind those
figures, including what Solid Fill contributes on its own.

Version 0.3.1 is an editorial revision of 0.3.0 with no wire-format change.

Version 0.3.0 **changed the wire format**, and was the first version to do so.
Output produced under 0.2.0 does not decode correctly under 0.3.0; the five
reference implementations and the shared test vectors all move to 0.3.0
together, and no implementation in this repository reads the older format. The
draft notice at the top of each document reserves the right to do this while
the specification is at 0.x.

Dynamic Passthrough is the part that changed. 0.2.0 activated individual R-Set
characters through a 13-bit mask, and any literal occurrence of an activated
character's replacement had to be escaped with `~`. 0.3.0 replaces that with
eight fixed **replacement alphabets** (Section 4.2): each is Alphabet-N with a
few of its rarest characters given up so that space, newline, quotes and the
other R-Set characters can be carried directly. Each alphabet is injective, so
nothing has to be escaped — a character a segment's alphabet cannot represent
simply ends the segment. The consequences:

- the signal payload shrinks from 22 bits to 13 — a 3-bit alphabet identifier
  and a 10-bit length, stored biased by one so it reaches 1024;
- a new normative constant, `MAX_DP_ANALYSIS_BYTES` = 1024, bounds how much
  input the encoder examines per decision, which is what fixes a candidate
  prefix at one segment and retires the multi-segment splitting rule;
- `MAX_CONSECUTIVE_ESCAPES`, the escape character's special role, and the
  "Dangling Escape Character" error are all gone;
- prefix identification becomes a single forward scan per alphabet instead of
  0.2.0's Pass 1 / Pass 2 procedure, and a normative tie-break (smallest
  identifier wins) keeps the five implementations byte-identical.

Measured over a corpus of JSON, Markdown, CSS, tab-separated data and source
code, encoded characters per input byte fall from 1.033 to 1.005 for
`countries.json`, from 1.053 to 1.005 for its minified form, and from 1.123 to
1.020 for the CommonMark specification. Binary input is unchanged at the
block-mode ratio of about 1.25. Section 14 of the document records the full
list of changes.

Version 0.2.0 **does not change the wire format** relative to 0.1.0. Every input encodes to
exactly the same characters as under 0.1.0, and the shared test vectors are
unchanged. It exists because 0.1.0 described the encoder's two-pass prefix
search in a way that is quadratic if implemented literally — Pass 1 scans to
the end of a representable run while the main loop may consume as little as
4 bytes of it — and all five reference implementations did implement it
literally. Benchmarking found the CommonMark specification encoding at
0.22 MB/s, and a buffer of `~` characters showing a clean fourfold increase
in time per doubling of length. The additions:

- a new normative **Section 6.6 (Encoding Complexity)** requiring linear-time
  encoding, explaining why the naive reading is quadratic, and describing an
  O(1)-memory technique that meets the bound with byte-identical output;
- a pointer to it from Section 6.1 step 1.a and from the Section 6.5 summary;
- a new normative bullet in **Section 13** covering the encoder as a
  denial-of-service surface, which 0.1.0 addressed only for the decoder.

Version 0.1.0 is the first versioned release of a document that previously
circulated as an undated/unversioned draft ("Base85N - Draft - Okt 07, 2025").
It carries that draft's normative content unchanged; the differences are
editorial only:

- version, date, status and editor metadata added;
- inline LaTeX-style math (`$2^{32}$`, `\ge`, …) rewritten as plain notation so
  the document renders identically on GitHub, on the project website, and in a
  plain-text viewer;
- repository-relative links rewritten as absolute URLs so the document is
  self-contained wherever it is served;
- a new, non-breaking Section 13 (Security Considerations) added;
- an AI-assisted-authorship notice added.

While the specification is at 0.x, the wire format is **not** frozen — see the
draft notice at the top of the document.
