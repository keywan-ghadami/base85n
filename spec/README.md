# Base85N specification versions

The specification is versioned. Each version is a separate, immutable file:
once a version is published, its normative text is not edited in place —
corrections and changes go into a new version.

| Version | Status | Date | Document |
|---|---|---|---|
| 0.3.0 | Draft (current) | 2026-08-12 | [`base85n-v0.3.0.md`](base85n-v0.3.0.md) |
| 0.2.0 | Superseded | 2026-08-10 | [`base85n-v0.2.0.md`](base85n-v0.2.0.md) |
| 0.1.0 | Superseded | 2026-08-10 | [`base85n-v0.1.0.md`](base85n-v0.1.0.md) |

Version 0.3.0 **changes the wire format**, and is the first version to do so.
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
