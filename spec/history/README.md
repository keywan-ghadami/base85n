# History

Everything behind the specification: the versions it went through, the
proposals that shaped it, the measurements the constants come from, and the
places where the measurements were wrong.

**None of this is needed to implement Base85N.** The specification is
[`../base85n-v0.5.0.md`](../base85n-v0.5.0.md), it is self-contained, and it
is final. This directory is for the question "why is it like that", not
"what is it".

## Superseded specifications

Each version is a separate, immutable file: once published, its normative
text is not edited in place. Two of the five changed the wire format, which
is why no implementation here reads anything but 0.5.0.

| Version | Status | Date | Wire format | Document |
|---|---|---|---|---|
| 0.4.0 | Superseded by 0.5.0 | 2026-08-15 | changed | [`base85n-v0.4.0.md`](base85n-v0.4.0.md) |
| 0.3.1 | Superseded by 0.4.0 | 2026-08-13 | unchanged | [`base85n-v0.3.1.md`](base85n-v0.3.1.md) |
| 0.3.0 | Superseded by 0.3.1 | 2026-08-12 | changed | [`base85n-v0.3.0.md`](base85n-v0.3.0.md) |
| 0.2.0 | Superseded by 0.3.0 | 2026-08-10 | unchanged | [`base85n-v0.2.0.md`](base85n-v0.2.0.md) |
| 0.1.0 | Superseded by 0.2.0 | 2026-08-10 | first | [`base85n-v0.1.0.md`](base85n-v0.1.0.md) |

## Design decisions

- **[`lessons.md`](lessons.md)** — what the measurements got wrong before
  they got it right, and the three trades the format is balanced on. Start
  here if you are reading this directory at all.
- **[`binary-flag-decision.md`](binary-flag-decision.md)** — the last
  feature proposal evaluated before the freeze, and why a measured
  +31 % to +71 % was not enough to accept it.
- **[`flavors-decision.md`](flavors-decision.md)** — alternative alphabets
  for containers Alphabet-N does not fit through, and why none was built:
  four of the six candidate containers are already safe, one candidate
  destroys Dynamic Passthrough, and the survivor is waiting for a consumer.
  Includes the header scheme, in case it is ever taken up.
- **[`proposals/`](proposals/)** — what was tried and what it cost,
  including the 0.5.0 scope review against what actually shipped, and the
  zero-run prototype patch.
- **[`../../bench/results/`](../../bench/results/)** — the measurements
  themselves: size, throughput, and the `--binary` attribution study.

---

## What changed in each version

### Version 0.5.0

Version 0.5.0 **changes the wire format**. Output produced under 0.4.0 does not
decode correctly under 0.5.0; the implementations and the shared test vectors
move together, and no implementation in this repository reads the older format.

**Fill has two variants.** The solid variant is 0.4.0's, bit for bit: a byte
value repeated up to 2048 times. The new *tail* variant spends the same five
characters on up to 32 zero bytes **and the two bytes beside it**, with one bit
saying which side they are on. Those two bytes are the point — a zero run that
ends one or two bytes short of a group boundary used to hand them back to block
mode at 1.25 characters each. The fields are what the signal space allows: 16
bits of literal, 5 of length and 1 of order come to 4,194,304 values, and 7,343,813
were free. A zero-padded ELF falls from 1.12618 to 0.96541, past Ascii85's 1.026
— the one row in the benchmark corpus where an established codec was ahead.

**`MIN_FILL_IN_SEGMENT_BYTES` moves from 11 to 16.** Eleven was the ratio
optimum and nothing else. The threshold also decides how much text stays inside
a readable passthrough segment, how many substitution tables a decoder rebuilds,
and how often the prefix scan rolls back. Ratio is flat from 13 to 16; at 16 the
corpus carries 370,000 more bytes in DP segments and `countries.json` — the
slowest decode line in the benchmark — decodes a third faster, for 1.0 % of ratio.

**Encoding is parallelisable, and the specification says how.** No format change
was needed: signals are self-describing and segment boundaries are data-decided,
so encoders started at different offsets converge. Section 11.3 states the
splice procedure and the measured convergence distances that bound it.

### Version 0.4.0

Version 0.4.0 **changed the wire format** relative to 0.3.x. Output produced under 0.3.x does not
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

### Version 0.3.1

Version 0.3.1 is an editorial revision of 0.3.0 with no wire-format change.

### Version 0.3.0

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

### Version 0.2.0

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

### Version 0.1.0

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

That draft notice is now historical: as of 0.5.0 the wire format is frozen
and the feature set is closed. See [`../README.md`](../README.md).
