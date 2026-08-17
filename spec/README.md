# Base85N specification

**[`base85n-v0.5.0.md`](base85n-v0.5.0.md) is the specification.** It is
self-contained: it defines the format completely, references no earlier
version, and is the only document an implementer needs.

| Field | Value |
|---|---|
| Version | 0.5.0 |
| Status | **Final** — stable wire format |
| Date | 2026-08-16 |
| License | MPL-2.0 |

## What "final" means here

It means this *document* is done, and that what it defines is stable: Fill —
both variants, including the zero-run tail — and Dynamic Passthrough behave
exactly as written, a byte string has one encoding, and anything encoded
under 0.5.0 keeps decoding under 0.5.0 forever. No 0.5.x revision will change
an output character.

It does **not** mean Base85N is finished. Section 9 holds 3 149 509 signal
values in reserve as `FUTURE_SIGNAL_SPACE`, and **0.6.0 is planned to spend
some of them on _flavors_**: a five-character signal that switches the stream
to another predefined alphabet, so a container whose reserved characters are
not the ones Alphabet-N avoids can get an alphabet that suits it.

That is growth by addition rather than by breakage, and the reserved range is
what makes it so. A stream encoded under 0.5.0 means the same thing to a
decoder that knows about flavors; a stream that uses one is *rejected* by a
0.5.0 decoder as an undefined signal (Section 10), loudly, rather than
misread. Both directions are already covered by the adversarial vectors.

Proposals still get measured before they get in — the last one to be
evaluated, a `--binary` encoder mode, was declined on its numbers, and
[the record of that](history/binary-flag-decision.md) is the shape any future
one has to take.

What is *not* claimed yet is 1.0.0. That is a statement about how long the
format has been in the field, not about what is in it, and it needs time
rather than work.

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
