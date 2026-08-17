# Base85N specification

**[`base85n-v0.5.0.md`](base85n-v0.5.0.md) is the specification.** It is
self-contained: it defines the format completely, references no earlier
version, and is the only document an implementer needs.

| Field | Value |
|---|---|
| Version | 0.5.0 |
| Status | **Final** — feature-frozen |
| Date | 2026-08-16 |
| License | MPL-2.0 |

## What "final" means here

The feature set is closed. Fill — both variants, including the zero-run tail
— and Dynamic Passthrough are what Base85N has, and no further mode,
signal or encoder option is going in. Proposals for new features are not
open; the last one to be evaluated, a `--binary` encoder mode, was measured
and declined, and [the record of that](history/binary-flag-decision.md) is
the shape any future one would have to take.

The wire format is stable at 0.5.0 and no 0.x version will change it.

What is *not* claimed yet is 1.0.0. That is a statement about how long the
format has been in the field, not about what is in it, and it needs time
rather than work. Until then the version number stays at 0.5.0 and the
answer to "will this change?" is no.

What may still change: implementations may get faster, benchmarks may be
re-measured, and this repository's documents may be corrected — all without
touching a single output character. The encoder is held to producing exactly
one encoding of any input, and the shared test vectors in
[`testvectors/`](../testvectors/) enforce that across all four
implementations.

## Reading the specification

Sections 1 to 10 define the format. Section 11 is implementation guidance,
non-normative except where it says otherwise — including 11.3, which
describes how to encode on several cores and still produce the one canonical
output. Section 13 is security considerations. Section 14 records what the
thresholds were measured against.

## History

Every earlier version, and the reasoning and measurements behind the format
as it stands, are in **[`history/`](history/)** — five superseded
specifications, the design proposals including the ones that were rejected,
and [what the measurements got wrong before they got it
right](history/lessons.md).

None of it is needed to implement Base85N. It is there because the format's
constants are not arbitrary, and someone will eventually want to know why
`MIN_FILL_IN_SEGMENT_BYTES` is 16.
