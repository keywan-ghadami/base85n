# Implementation Notes for Base85N

This file documents two clarifications applied on top of the literal wording
in `README.md` (the draft spec). Both were found by writing a reference
implementation and fuzz-testing round-trips (`encode` then `decode` must
always reproduce the original bytes) with tens of thousands of random
inputs. All library implementations in this repository (`rust/`, `go/`,
`typescript/`, `c/`) MUST follow these clarifications so that they are
mutually interoperable and always round-trip correctly.

The golden cross-language test vectors in `testvectors/vectors.json` were
generated from a reference implementation that follows these rules, and
every implementation's test suite must load and verify against that file.

## Clarification 1: the R-Set mask used to build DP output must be the
*final* mask, not the growing "tentative" mask

Section 6.1.b of the spec describes a byte-by-byte scan that grows a
`tentative_mask` as R-Set characters are discovered, and section 6.1.b.ii
uses that *in-progress* mask to decide whether a given
`allowedPassthroughSafeReplacementCharacters[j]` byte needs escaping at the
point it is scanned. Taken completely literally, this means a
`allowedPassthroughSafeReplacementCharacters[j]` byte occurring *before* the
first occurrence of `R_Char[j]` in the same candidate prefix would be
emitted as an unescaped literal (bit `j` not set yet) -- but the decoder
(section 7.1.e) only ever sees the *final* mask for the whole DP segment,
so it would incorrectly interpret that earlier literal as a substituted
`R_Char[j]`. That is a real ambiguity that can break round-tripping.

**Rule actually implemented here (two-pass approach):**

1. **Pass 1 (byte inclusion / boundary scan):** Run the scan exactly as
   written in section 6.1.b, byte by byte, using the growing
   `tentative_mask`, honoring `MAX_CONSECUTIVE_ESCAPES` and terminating on
   an unrepresentable byte (case iv). This determines exactly which bytes
   belong to `candidate_prefix` and the resulting `final_mask` (the mask
   value at the end of the scan -- bits only ever get set, never cleared).
2. **Pass 2 (transform):** Once `candidate_prefix` and `final_mask` are
   fixed, build the actual transformed output string by iterating over
   `candidate_prefix` again from the start, this time testing every byte
   against the constant `final_mask` (not a growing one):
   - If the byte is an R-Set character with index `j` (`final_mask` is
     guaranteed to have bit `j` set, since inclusion in pass 1 always sets
     that bit and bits never clear): emit
     `allowedPassthroughSafeReplacementCharacters[j]`.
   - Else if the byte is `~`: emit `~~`.
   - Else if the byte equals `allowedPassthroughSafeReplacementCharacters[j]`
     for some `j`, and bit `j` of `final_mask` is set: emit `~` followed by
     the character.
   - Else: emit the literal character.

   The **actual length** of this pass-2 string (not the scan's running
   `transformed_length` from pass 1) is what must be used for the DP vs.
   block-mode efficiency comparison (section 6.1.2.a) and for the
   `Length_9bit_encoded_value` field(s) of the DP signal(s) -- because that
   length is exactly what the decoder will read back. This is consistent
   with decoding (section 7.1.e), which always applies one fixed mask to
   an entire DP segment.

   In practice pass 2's length is always `>=` pass 1's `L_transformed`
   (mask bits only grow), so on rare inputs a prefix that pass 1 judged
   DP-suitable may turn out (after pass 2) to be equal in length to, but
   never shorter than, what pass 1 estimated; the `<=` comparison in
   section 6.1.2.a still applies to the pass-2 (actual) lengths.

## Clarification 2: plain Block Mode may only emit a "partial" (non
multiple-of-5-characters) trailing group when it is truly the last thing
in the whole encoded stream

Section 7.1's decoder only recognizes a partial final block (2, 3, or 4
Alphabet-N characters, decoded to 1, 2, or 3 bytes respectively) when the
**entire input stream** ends with those characters. It has no way to
resynchronize if a partial (non-multiple-of-5) group appears in the
*middle* of the stream followed by more Base85N constructs.

However, section 6.1.2.b's "otherwise" branch says: if the DP scan
produced a non-empty but DP-unsuitable `dp_candidate_prefix` (for example
because it was too short, or ended at an unrepresentable byte), that
*entire* prefix is run through `ProcessWithBlockMode` -- even though there
may be more bytes left in `intermediate_buffer` afterward. If that
prefix's length is not a multiple of 4, `ProcessWithBlockMode` (section
6.2) emits a partial trailing group, and if this happens while more data
still follows, the result cannot be decoded unambiguously.

**Rule actually implemented here:** when block-encoding a non-empty
`candidate_prefix` that is *not* suitable for DP mode:

- If, after removing `candidate_prefix` from the buffer, the buffer is now
  **empty** (this prefix really is the tail of the whole stream): encode
  it with the ordinary `ProcessWithBlockMode` as written in section 6.2
  (a partial trailing group is fine here -- it is genuinely at the end of
  the stream).
- Otherwise (more bytes remain after this prefix): only encode
  `floor(len(candidate_prefix) / 4) * 4` bytes of it as full 4-byte blocks
  (this can never produce a partial group). Any leftover 1-3 bytes of the
  prefix are **not** consumed here; they are left at the front of the
  buffer to be reconsidered by the next iteration of the main loop (where
  they will typically fall through to the "no suitable DP prefix found"
  fallback below).
- If that floor-division yields zero full blocks (the unsuitable prefix is
  itself shorter than 4 bytes and more data follows), fall back to
  encoding `min(4, len(buffer))` raw bytes starting at the buffer's
  current front (i.e. the same fallback already used in section
  6.1.2.b's "else" branch for an empty `dp_candidate_prefix`). This is
  always safe: if `len(buffer) <= 4` the buffer becomes empty afterward
  (a legitimate final partial block), otherwise exactly 4 bytes (a full
  block) are consumed.

DP-signaled segments are never affected by this issue: a DP signal is
always a full, untruncated 5-character group (since its numeric value is
always `>= 2^32`), so the decoder can always find and skip exactly
`Length_9bit_encoded_value` characters afterward regardless of where in
the stream it occurs.

## Everything else

Every other part of the algorithm (alphabet, R-Set table, escape rules,
signal bit layout, big-endian 4-byte <-> 5-digit Base85 conversion, error
conditions) should be implemented exactly as written in `README.md`.
