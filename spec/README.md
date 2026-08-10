# Base85N specification versions

The specification is versioned. Each version is a separate, immutable file:
once a version is published, its normative text is not edited in place —
corrections and changes go into a new version.

| Version | Status | Date | Document |
|---|---|---|---|
| 0.2.0 | Draft (current) | 2026-08-10 | [`base85n-v0.2.0.md`](base85n-v0.2.0.md) |
| 0.1.0 | Superseded | 2026-08-10 | [`base85n-v0.1.0.md`](base85n-v0.1.0.md) |

Version 0.2.0 **does not change the wire format**. Every input encodes to
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
