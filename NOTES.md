# Implementation Notes for Base85N

## History

While building a reference implementation for this repository's four
library implementations (`rust/`, `go/`, `typescript/`, `c/`), three real
ambiguities/bugs were found in the draft algorithm as it was worded at the
time, all in the area of Dynamic Passthrough (DP) mode's interaction with
the R-Set mask, segment splitting, and Block Mode fallback:

1. Which R-Set mask an encoder must use when building DP output for a
   candidate prefix (a mask that is still growing while escaping
   decisions are made can make one byte's encoding depend on a
   character encountered *later* in the scan -- ambiguous to a decoder,
   which only ever sees one fixed mask per segment).
2. That a DP segment longer than `MAX_DP_OUTPUT_CHARS_PER_SIGNAL` (511
   characters) must never be split at a fixed character offset, since
   that can land inside a 2-character escape pair (`~x`) and strand the
   `~` in one segment with `x` in the next -- undecodable.
3. That plain Block Mode may only emit a partial (non-multiple-of-5-
   character) trailing group when it is truly the last thing in the
   whole encoded stream, never mid-stream, since a decoder can only
   recognize a partial final block by reaching actual end-of-input.

**These have since all been fixed upstream, in the spec itself.**
`README.md` Section 6.1 ("Main Encoding Loop") now explicitly documents:

- A two-pass "Pass 1 / Pass 2" procedure (Section 6.1, step 1.a-c) that
  fixes the R-Set mask (via a representability-only Pass 1 "window" scan)
  *before* any escaping decision is made (Pass 2), resolving (1).
- An explicit "DP Output Segmentation" procedure (Section 6.1, step 1.d)
  that packs whole per-byte contributions into <=511-character segments
  and never splits a 2-character escape pair across a segment boundary,
  resolving (2).
- An explicit rule (Section 6.1, step 2.b) that a DP-unsuitable candidate
  prefix is only ever block-encoded up to its largest exact multiple of 4
  bytes immediately; any 1-3 trailing bytes are deferred, unpadded, to the
  next loop iteration rather than encoded as a premature partial block,
  resolving (3).

**All four library implementations in this repository MUST follow
`README.md` as it now stands (Sections 6.1, 6.1.d, and 6.2) literally --
this file no longer needs to override or clarify the spec's wording.**
The golden cross-language test vectors in `testvectors/vectors.json` /
`testvectors/vectors.tsv` were (re)generated from a reference
implementation of the current, official Section 6.1 algorithm, including
a dedicated regression vector,
`dp_segment_boundary_no_split_escape_pair`, that specifically exercises
the escape-pair-safe segment-splitting rule (2) above -- an encoder that
naively slices a DP segment's transformed text at a fixed 511-character
offset instead of packing whole pieces will fail to round-trip that
vector. Every implementation's test suite must load and verify against
that vector file.
