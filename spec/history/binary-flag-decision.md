# Decision: no `--binary` encoder mode

**Status: declined, 2026-08-17.** The last feature proposal evaluated before
0.5.0 was declared final.

The measurement is
[`bench/results/binary-flag.md`](../../bench/results/binary-flag.md); this is
the decision it produced, kept here because the reasoning applies to any
future proposal of the same shape.

## The proposal

An encoder flag telling Base85N that its input is binary, so it could skip
the Dynamic Passthrough machinery that binary input mostly does not use. The
decoder would be unchanged — it already accepts every construct wherever it
appears.

## The rule it was given

> `--binary` stays an official encoder option only if it delivers a
> significant encode speedup on representative binary data. Significant
> means 4 %.

The rule was set deliberately low and deliberately narrow: the concern behind
it was that the flag would turn out to be a second API for the same
functionality, and a small measured gain would have settled that.

## What was measured

Six encoders over the benchmark corpus, interleaved and paired; see
[`lessons.md`](lessons.md) §3 and §4 for why six and why paired.

**The rule is met.** `--binary` delivers **+31 % to +71 %** on every binary
sample, in every round — 27 to 67 points more than the threshold asks for.

## Why it was declined anyway

**It is not a second API for the same functionality. It is a second
dialect.** Section 6.5 rule 2 requires the longest Dynamic Passthrough prefix
at every decision point the loop reaches; Section 11.3 states there is "no
second canonical form and nothing to configure". An encoder that skips DP
produces a stream that decodes correctly and that no conforming encoder would
emit. The byte-identity of the four implementations, the shared test vectors
that enforce it, and the parallel encoder of 11.3 all rest on there being one
encoding per input.

**It costs size on the files it names.** Nil on JPEG, PNG and random bytes,
0.2–0.4 % on TrueType, ELF and WebAssembly — but **+17.7 % on an uncompressed
tar**, whose ASCII path headers are binary by file type and text by content,
which is the normal shape of a binary container. Pointed at text, which is
what a flag named `--binary` invites when someone guesses wrong, it costs
5–25 %.

**It is not the right bundle.** On ELF and WebAssembly, removing Fill is
worth more than removing Dynamic Passthrough. The proposal removes DP and
keeps Fill, so on two of the file types it targets, most of the available
saving is in the step it keeps.

**Most of its case was a defect in the baseline.** Against the encoder as it
stood when the flag was proposed it scored up to +314 %. The attribution rows
put that on an unpredictable branch in the block-mode lookahead, not on
Dynamic Passthrough. Fixing it flaglessly — same output, character for
character — took back two thirds to four fifths. Accepting the flag would
have shipped the defect behind it.

## What this means for future proposals

The freeze is not "no more measuring". It is that a proposal now has to clear
all of this, not just a throughput threshold:

1. **Does it change the output?** If yes, it is a wire-format change, and
   0.5.0 is frozen. That ends it.
2. **Is it an encoder option?** If yes, it contradicts 6.5 and 11.3
   regardless of what it buys. That ends it too.
3. **Can the conforming encoder have the same benefit?** If yes, do that
   instead — as happened here, at more than twice the size of what the flag
   was offering.
4. **Only then** is the number worth discussing.

Implementations getting faster inside those constraints is not a feature
proposal and needs no rule. That path is wide open, and
[`bench/results/binary-flag.md`](../../bench/results/binary-flag.md) ends by
saying how much is still on it.
