# Base85N specification versions

The specification is versioned. Each version is a separate, immutable file:
once a version is published, its normative text is not edited in place —
corrections and changes go into a new version.

| Version | Status | Date | Document |
|---|---|---|---|
| 0.1.0 | Draft (current) | 2026-08-10 | [`base85n-v0.1.0.md`](base85n-v0.1.0.md) |

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
