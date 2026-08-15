# Base85N: A Protocol-Friendly Binary-to-Text Encoding

| Field | Value |
|---|---|
| Version | 0.4.0 |
| Status | Draft |
| Date | 2026-08-15 |
| Supersedes | 0.3.1 |
| License | MPL-2.0 |

> **Draft status.** This is a 0.x draft. The wire format is **not** backward compatible with v0.3.1 and is deliberately constructed so that a v0.3.1 decoder rejects v0.4.0 data rather than misdecoding it (Section 11). Anything here MAY change in a subsequent 0.x version.
>
> **One open parameter.** `DONOR_ORDER` (Section 4.2) is provisional. It is reasoned and prototype-tested, not measured on a production corpus. It MUST be re-derived per Section 15.1 before 0.4.0 is declared final. Everything else in this document is settled.
>
> **Self-contained.** Implementers do not need v0.3.1. Sections 4, 4.1, 5, 6.3, 7.4 and 8 are carried over unchanged and are restated here in full.

---

## 1. Abstract

Base85N is a binary-to-text encoding using a single 85-character alphabet (Alphabet-N) chosen for broad protocol compatibility. Its core is a 4-byte-to-5-character conversion. A **Dynamic Passthrough (DP)** mode represents runs of printable text at exactly 1:1, partially human-readable, using only Alphabet-N characters and no escape mechanism.

DP works by *trading*. Alphabet-N has 85 characters and does not contain space, newline, quotes, or several other characters that ordinary text is full of. For each segment, the encoder determines which of 13 such characters (the **R-Set**) actually occur, and gives up exactly that many of Alphabet-N's rarest characters (**donors**) to carry them. Because the resulting map is injective, no escape character is needed. A segment pays only in that its donors cannot appear as literals; encountering one ends the segment.

Version 0.4.0 replaces v0.3.1's eight fixed replacement alphabets with this per-segment derivation. The practical effect is that a segment no longer surrenders donors for R-Set characters it does not contain.

---

## 2. Introduction

### 2.1 What changed from v0.3.1, and why

v0.3.1 defined eight static replacement alphabets. Each surrendered **all** of its donor characters unconditionally. A run of plain prose encoded under alphabet 1 (`text`) still could not contain `%` or `$`, because those were reserved for `CR` and `TAB` — even when no `CR` or `TAB` occurred anywhere in the segment.

v0.4.0 signals a 13-bit **mask** naming exactly the R-Set characters present, and derives the donors from it. Donors beyond the ones actually needed remain ordinary literals.

Two independent measurements motivated the change (synthetic corpora, reference prototype; see Section 15.2 for the caveat):

* **Size.** Overhead fell from 5.21 % to 3.36 %. Merely re-tuning v0.3.1's eight static alphabets recovered only 0.66 points of that, which establishes the problem as structural rather than a matter of parameter choice.
* **Speed.** The byte-weighted number of active substitutions per segment fell from 6.9 to 4.6. Since the decoder's inner loop costs one compare-and-blend per active substitution, this makes the DP decode path *cheaper* than v0.3.1, not more expensive — the opposite of the naive expectation for a more dynamic scheme.

An intermediate design carrying an additional profile identifier (several donor orderings, selected per segment) was evaluated and **rejected**: it improved size by a further 0.39 points only, while multiplying encoder scan state, adding a table that would itself need corpus derivation, and occasionally producing worse output because a locally longer prefix can leave a worse remainder.

### 2.2 Key properties

* **Density.** 4 bytes to 5 characters in block mode, against Base64's 3-to-4.
* **Single alphabet.** Only Alphabet-N is ever emitted, in either mode.
* **Escape-free.** One input byte always yields exactly one output character in DP mode. A DP segment's character length equals its byte length.
* **Partially readable.** `hello world\nthis is fine\n` encodes to `%r5Wxhello^world~this^is^fine~` — a five-character signal followed by recognisable text.
* **Padding-free.** Any input length, with a single canonical form for truncated trailing blocks.
* **Bounded lookahead.** At most `MAX_DP_ANALYSIS_BYTES` (1024) bytes are examined per decision.
* **Linear time.** Guaranteed by construction (Section 6.6), not merely recommended.

---

## 3. Conventions

The key words MUST, MUST NOT, REQUIRED, SHALL, SHALL NOT, SHOULD, SHOULD NOT, RECOMMENDED, NOT RECOMMENDED, MAY and OPTIONAL are to be interpreted as described in BCP 14 [RFC2119] [RFC8174] when, and only when, they appear in all capitals.

Notation used throughout:

| Symbol | Meaning |
|---|---|
| `mask` | 13-bit field; bit *j* set ⟺ R-Set character *j* occurs in the segment |
| `k` | `popcount(mask)`; the number of active substitutions, 0 ≤ `k` ≤ 13 |
| `rank(j)` | `popcount(mask & ((1 << j) - 1))`; position of bit *j* among the set bits |
| `L` | Segment length, in bytes (input) and equally in characters (output) |

---

## 4. Alphabet (Alphabet-N)

Alphabet-N has 85 characters with integer values 0–84.

| Values | Characters |
|---|---|
| 0–9 | `0 1 2 3 4 5 6 7 8 9` |
| 10–19 | `a b c d e f g h i j` |
| 20–29 | `k l m n o p q r s t` |
| 30–39 | `u v w x y z A B C D` |
| 40–49 | `E F G H I J K L M N` |
| 50–59 | `O P Q R S T U V W X` |
| 60–69 | `Y Z . - : + = ^ ! /` |
| 70–79 | `* ? `` ` `` _ ~ ( ) [ ] {` |
| 80–84 | `} @ % $ #` |

```
ALPHABET_N = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ.-:+=^!/*?`_~()[]{}@%$#"
```

Implementations MUST assert at startup or build time that this string has length 85 with no repeated character.

### 4.1 R-Set

The R-Set is 13 characters that occur frequently in real text and are **not** members of Alphabet-N. The index *j* is normative and fixes bit positions in `mask`.

| j | Character | Byte | | j | Character | Byte |
|---|---|---|---|---|---|---|
| 0 | space | 0x20 | | 7 | `<` | 0x3C |
| 1 | `"` | 0x22 | | 8 | `>` | 0x3E |
| 2 | `'` | 0x27 | | 9 | `&` | 0x26 |
| 3 | `,` | 0x2C | | 10 | TAB | 0x09 |
| 4 | `;` | 0x3B | | 11 | LF | 0x0A |
| 5 | `\` | 0x5C | | 12 | CR | 0x0D |
| 6 | `\|` | 0x7C | | | | |

The R-Set and Alphabet-N are disjoint. This is what makes the scheme injective without an escape character, and implementations MUST assert it.

### 4.2 Donor Order and Mask Semantics

A single global donor priority order is defined:

```
DONOR_ORDER = "^~!?%$#*@+=`_"
```

| Rank | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Char | `^` | `~` | `!` | `?` | `%` | `$` | `#` | `*` | `@` | `+` | `=` | `` ` `` | `_` |

All 13 are members of Alphabet-N and are pairwise distinct; implementations MUST assert both.

> **Provisional.** This ordering is reasoned from expected character rarity, not measured. Ranks 0–4 are the hot path (almost every text segment sets space and LF, so ranks 0–1 are nearly always consumed); ranks 5–8 (`$ # * @`) are the ones most likely to change after measurement. See Section 15.1.

Given a segment's `mask`, the substitution set is derived deterministically:

```
rank = 0
for j in 0..12:
    if mask & (1 << j):
        donor(j) = DONOR_ORDER[rank]
        rank += 1
```

That is: the set bits of `mask` are walked in ascending R-Set index order and consume **the first `k` characters of `DONOR_ORDER`, in order**.

Within a segment so described:

* An input byte equal to `R_CHARS[j]` for a set bit *j* SHALL be written as `donor(j)`.
* An input byte equal to any `donor(j)` is **not representable**; it cannot occur in the segment.
* Every other Alphabet-N character represents itself — **including `DONOR_ORDER` characters at ranks ≥ k**.
* Every other byte is not representable.

Because no donor is assigned twice and no R-Set index is used twice, the map from representable input byte to output character is injective, and a donor character in DP data has exactly one meaning, fixed by `mask`.

**Worked example.** `mask = 0b0100000000001` (bits 0 and 11 → space and LF), `k = 2`. `rank(0) = 0` and `rank(11) = 1`, so space → `^` and LF → `~`. All eleven other `DONOR_ORDER` characters, `!` through `_`, are ordinary literals in this segment.

### 4.3 Escape Character

Base85N has no escape character and no escape sequences. `~` (value 74) is an ordinary Alphabet-N character; it acts as a donor only when `k ≥ 2` places it within the consumed prefix of `DONOR_ORDER`.

---

## 5. Endianness

All conversions between multi-byte integers and byte sequences MUST use big-endian byte order.

---

## 6. Encoding

### 6.1 Main Loop

The encoder consumes an input byte stream, conceptually via `intermediate_buffer`. A streaming encoder MUST buffer up to `MAX_DP_ANALYSIS_BYTES` bytes of lookahead, or reach end of input, before making a decision.

While `intermediate_buffer` is non-empty:

**Step 1 — Prefix scan.** Determine `(L, mask)` per Section 6.2.

**Step 2 — Suitability.** Use DP mode if and only if **both** hold:

* `L >= MIN_PASSTHROUGH_BYTES`
* `5 + L <= ceil(L / 4) * 5`

**Step 3a — DP mode.** Emit the five-character signal (Section 9) carrying `mask` and `L`, immediately followed by `L` characters: for each byte, its donor character if it is a substituted R-Set character, otherwise its own Alphabet-N character. Remove `L` bytes from the buffer.

**Step 3b — Block mode.** Otherwise encode exactly `min(4, remaining)` bytes with Section 6.3 and remove them.

Block mode consumes **one group and no more**, regardless of how long the rejected candidate was. The next iteration re-runs the scan from four bytes further on. Every branch removes at least one byte, so the loop terminates.

### 6.2 Prefix Scan

Maintain three variables across a single forward pass:

| Variable | Init | Meaning |
|---|---|---|
| `mask` | 0 | R-Set characters seen so far |
| `k` | 0 | `popcount(mask)`, maintained incrementally |
| `min_donor` | 13 | Lowest `DONOR_ORDER` rank seen **as a literal** |

The prefix is valid exactly while `k <= min_donor`. For each byte `b`, bounded by `MAX_DP_ANALYSIS_BYTES`:

```
if b is R_CHARS[j] for some j:
    if mask already has bit j:  accept, nothing changes
    else if k + 1 > min_donor:  STOP
    else:                       mask |= 1<<j ;  k += 1 ;  accept
else if b is an Alphabet-N character:
    p = rank of b in DONOR_ORDER, or 13 if absent
    if p >= min_donor:          accept, nothing changes
    else if p < k:              STOP
    else:                       min_donor = p ; accept
else:
    STOP
```

`L` is the number of accepted bytes; `mask` is its value at that point.

**Rationale for the condition.** If `k` R-Set characters occur, the mask necessarily claims `DONOR_ORDER[0..k-1]`. The segment is valid only while none of those appear as a literal, i.e. while `min_donor >= k`.

**Monotonicity.** Extending a prefix can only increase `k` and only decrease `min_donor`. Validity is therefore monotonically non-increasing: once broken it can never be restored by reading further. Three consequences follow, and implementations may rely on all three:

1. A single forward pass finds the exact maximal prefix. No backtracking, no lookahead beyond the current byte.
2. The validity check is only *ever* tightest at the end of any span of bytes. An implementation MAY therefore evaluate it once per SIMD block rather than once per byte, and rescan scalar only when a block fails.
3. `popcount` need never be computed per byte; `k` is maintained incrementally.

### 6.3 Block Mode

Treat each 4-byte group as a 32-bit big-endian unsigned integer and convert to five Alphabet-N characters (Section 8).

For a final partial group of 1, 2 or 3 bytes, right-pad with zero bytes to four, convert to five characters, and emit only the first **2, 3 or 4** characters respectively.

This truncation is meaningful only at true end of input, because a decoder recognises a partial group only by reaching end of stream. Every other call site MUST pass a length that is an exact multiple of four.

### 6.4 Constants

| Constant | Value | Notes |
|---|---|---|
| `MAX_DP_ANALYSIS_BYTES` | 1024 | Lookahead bound; also bounds segment length |
| `MAX_DP_SEGMENT_CHARS` | 1024 | Equals the above, since DP is 1:1 |
| `MIN_PASSTHROUGH_BYTES` | 20 | Smallest `L` at which DP is never larger than block mode |
| `DP_SIGNAL_OFFSET` | 8192 | Separates v0.4.0 signals from v0.3.1's range |
| `DP_PAYLOAD_BITS` | 23 | 13 mask + 10 length |

> **`MIN_PASSTHROUGH_BYTES` is the one performance knob.** 20 is the *size* break-even. The *throughput* break-even is higher, because per-segment setup is amortised over segment length (Section 12.3). Raising it is fully decode-compatible — any decoder handles any segment length — but it changes encoder output and therefore the golden vectors. Implementations that deviate MUST document the value used and MUST NOT claim conformance with the reference vectors. Fix one value after benchmarking (Section 15.2).

### 6.5 Canonicity

Four rules make encoder output a deterministic function of its input:

1. **Maximal prefix.** The encoder SHALL take the longest valid prefix. DP is exactly 1:1, so a longer prefix is never worse locally.
2. **Exact mask.** `mask` SHALL contain a set bit for every R-Set character occurring in the segment and for no other. Setting a spurious bit is forbidden even though it would decode identically.
3. **Both suitability conditions** SHALL be applied as written; DP MUST NOT be used for a prefix that fails either.
4. **Block mode consumes one group.** A rejected DP candidate SHALL NOT be block-encoded as a unit.

**The decoder does not enforce these.** Every well-formed signal decodes unambiguously whether or not the encoder was canonical, so enforcement would cost work without removing ambiguity. This is a deliberate asymmetry with the final-partial-block rule of Section 7.4, where non-canonical input *does* create genuine ambiguity and MUST be rejected.

**Consequence.** Base85N output is not a canonical form suitable for hashing, deduplication or signature comparison unless the producing encoder is known to be conformant. Implementations MUST NOT assume otherwise.

**Known non-optimality (informative).** Maximal-prefix selection is locally, not globally, optimal: a shorter first segment can occasionally permit a longer second one. This is accepted deliberately, since global optimisation requires unbounded lookahead and would break Section 6.6.

### 6.6 Complexity

**Normative.** An encoder SHALL perform `O(N)` total input-byte inspections for an input of `N` bytes, with per-byte cost independent of `MAX_DP_ANALYSIS_BYTES`. An implementation SHALL NOT inspect any input byte an unbounded number of times.

Unlike v0.3.1, where linearity had to be recommended because the natural implementation performed eight independent scans, v0.4.0 obtains it structurally: Section 6.2's monotonicity means one forward pass is both necessary and sufficient. An implementation that does anything else is doing extra work for no benefit.

**Verification.** Conformance testing SHOULD measure encoding time across increasing `N`, including pseudorandom binary input that forces frequent block-mode fallback, and confirm linear growth.

---

## 7. Decoding

### 7.1 Stream Structure and Whitespace

Input is a sequence of constructs. A construct is one of:

* a complete 5-character block group;
* a 5-character DP signal followed immediately by its declared data segment;
* a final partial block group of 2, 3 or 4 characters, permitted only at end of input.

A decoder MUST ignore space (U+0020), TAB (U+0009), LF (U+000A) and CR (U+000D) occurring **between** Base85N characters and constructs.

> **Normative interaction with DP data — read carefully.** Whitespace skipping does **not** apply inside `transformed_DP_data`. A DP segment's characters are consumed raw. It follows that the availability check of Section 7.3 SHALL count **raw** remaining characters, not whitespace-stripped ones.
>
> This is not a detail. Counting stripped characters lets whitespace injected inside a segment be reported as truncation instead of as an invalid character, and lets an attacker shift the segment boundary relative to what a length-checking peer computed. Whitespace can never legitimately occur inside DP data: space, TAB, LF and CR are all R-Set characters and are always emitted as donors, never literally.

A decoder MUST treat its input as untrusted (Section 14).

### 7.2 Block Groups

Convert the five characters to `V` per Section 8. If `V < 2^32`, emit `V` as four big-endian bytes and continue.

### 7.3 DP Segments

If `V >= 2^32`:

```
payload = V - 2^32

if payload <  DP_SIGNAL_OFFSET:                    error LEGACY_SIGNAL
if payload >= DP_SIGNAL_OFFSET + 2^23:             error UNDEFINED_SIGNAL

packed = payload - DP_SIGNAL_OFFSET
L      =  (packed        & 0x3FF) + 1              # 1..1024
mask   =  (packed >> 10) & 0x1FFF                  # MUST mask to 13 bits

if fewer than L raw characters remain:             error UNEXPECTED_EOS
                                                   # checked BEFORE reading

derive donor(j) from mask per Section 4.2

for each of the next L characters c, read raw:
    if c is not in Alphabet-N:                     error INVALID_CHARACTER
    if c == donor(j) for some active j:            emit R_CHARS[j]
    else:                                          emit the byte value of c
```

`L_enc = 0` denotes a one-character segment. A decoder MUST NOT interpret it as an empty segment.

The masking of `mask` to 13 bits is REQUIRED even though `packed` is bounded by construction, because `packed` is derived from attacker-controlled input.

### 7.4 Final Partial Block

A trailing group of 2, 3 or 4 characters is padded conceptually with `#` (value 84) to five characters, converted to `V`, and yields the first 1, 2 or 3 bytes respectively.

* A trailing group of exactly **1** character is an error.
* A padded trailing group whose `V >= 2^32` is an error.
* **Canonical enforcement.** The decoder MUST verify that the supplied characters are exactly the canonical prefix produced by encoding the resulting bytes zero-padded to four (Section 6.3). Any mismatch MUST be rejected as `INVALID_FINAL_BLOCK`.

---

## 8. Value and Digit Conversion

```
digits_to_value(d[5]) :  V = ((((d0*85 + d1)*85 + d2)*85 + d3)*85 + d4)
value_to_digits(V)    :  for i from 4 down to 0:  d[i] = V % 85 ;  V = V / 85
```

Integer division. `85^5 = 4 437 053 125`, which exceeds 32 bits; implementations MUST use at least 64-bit arithmetic here.

---

## 9. Signal Interpretation

Let `V` be the value of a 5-character group.

| Range of `V` | Interpretation |
|---|---|
| `0` … `4 294 967 295` | Standard 4-byte block |
| `4 294 967 296` … `4 294 975 487` | **Reserved.** v0.3.1 DP signal range. MUST be rejected. |
| `4 294 975 488` … `4 303 364 095` | v0.4.0 DP signal |
| `4 303 364 096` … `4 437 053 124` | Reserved for future use. MUST be rejected. |

Signal space available above block mode is `85^5 − 2^32 = 142 085 829`. v0.4.0 occupies `8192 + 2^23 = 8 396 800`, leaving `133 689 029` for future extension — enough that a later revision can define, for example, a profiled variant in the reserved range without disturbing this one.

**Payload construction:**

```
packed  = (mask << 10) | L_enc                 # 23 bits;  L_enc = L - 1
payload = DP_SIGNAL_OFFSET + packed            # ADDITION
V       = 2^32 + payload
```

| Bits of `packed` | Field | Range |
|---|---|---|
| 10–22 | `mask` | 0 … 8191 |
| 0–9 | `L_enc` | 0 … 1023, meaning `L` = 1 … 1024 |

> **Implementation warning — this bug is not caught by round-trip tests.** `DP_SIGNAL_OFFSET` MUST be **added**, not bitwise-OR'd. `packed` routinely exceeds 8192, so `OFFSET | packed` silently discards the offset for most real segments and produces a value in the v0.3.1 range. An encoder and decoder that share the mistake round-trip perfectly against each other while emitting data that a conformant peer will misdecode. Section 13 makes this an explicit test requirement.

---

## 10. Error Handling

Implementations MUST detect and report at least the following, distinctly:

| Code | Condition |
|---|---|
| `INVALID_CHARACTER` | A character outside Alphabet-N, after permitted whitespace stripping |
| `UNEXPECTED_EOS` | End of input while a block, signal or segment is still required |
| `LEGACY_SIGNAL` | `payload < DP_SIGNAL_OFFSET` — v0.3.1 data or corruption |
| `UNDEFINED_SIGNAL` | `payload >= DP_SIGNAL_OFFSET + 2^23` |
| `INVALID_FINAL_BLOCK` | Trailing group of one character; padded value ≥ 2³²; or non-canonical |

`LEGACY_SIGNAL` MUST be distinguishable from the other signal errors so that version mismatch is diagnosable in the field rather than presenting as generic corruption.

An implementation MUST NOT read outside its input buffer, index past a declared segment end, or terminate the process on malformed input.

---

## 11. Migration from v0.3.1

The formats are mutually exclusive by construction:

* A **v0.3.1 decoder** rejects all v0.4.0 signals, because its own valid payload range ends at 8191 and every v0.4.0 payload is ≥ 8192.
* A **v0.4.0 decoder** rejects all v0.3.1 signals as `LEGACY_SIGNAL`, because every v0.3.1 payload is < 8192.

The 8192-value hole exists solely to buy this. Without it, a v0.3.1 payload would be a structurally valid v0.4.0 payload with different meaning, and a version mismatch would silently yield wrong plaintext instead of an error. The cost is 8192 values out of 142 million.

Block-mode output is **byte-identical** between the versions. Data containing no DP segments is valid and identical under both.

Deployments SHOULD migrate by re-encoding at rest rather than by attempting dual-format decode. Where a transitional decoder must accept both, it SHOULD dispatch on the payload range and MUST NOT infer the version from anything else.

---

## 12. Implementation Guidance (Informative)

None of this section is required for conformance. All of it affects whether v0.4.0 is faster or slower than v0.3.1 in practice.

### 12.1 Decoder: do not build a mutable table

The naive approach — a 256-entry translation table, mutated per segment and restored afterwards — is the slow path and the dangerous one. It costs stores proportional to `k` twice per segment, and a table left dirty on an error path silently corrupts subsequent segments. That class of bug does not appear in round-trip tests on valid input.

Prefer a compare-and-blend loop. Because `DONOR_ORDER` and `R_CHARS` are compile-time constants, all 26 broadcast vectors are constants too; per-segment work is only the walk over the set bits of `mask`:

```
rank = 0
for j in set_bits(mask):                 # k iterations, k <= 13
    pairs[rank] = (DONOR_VEC[rank], RCHAR_VEC[j])
    rank += 1

for each 32-byte vector v of segment data:
    for (d, r) in pairs:                 # k iterations
        v = blend(v, r, cmpeq(v, d))
    store(v)
```

Inner-loop cost is `2k + 2` operations per 32 bytes. Byte-weighted mean `k` measured 4.6 for v0.4.0 against 6.9 for v0.3.1's static alphabets, so this loop is roughly 30 % cheaper than the equivalent v0.3.1 path.

### 12.2 Encoder: check once per block, not per byte

By Section 6.2's monotonicity, validity is tightest at the end of any span. Compute `mask |= OR(block)` and `min_donor = MIN(min_donor, MIN(block))` over a whole SIMD block, then evaluate `k <= min_donor` once. Only when a block fails does the implementation rescan those bytes scalar to locate the exact break — and breaks occur once per segment, not once per byte.

Both classifications (is this byte an R-Set character; what is its `DONOR_ORDER` rank) are over small fixed sets of low-ASCII bytes and are well suited to nibble-indexed `PSHUFB` lookups. Neither requires a gather.

### 12.3 Where v0.4.0 can lose

Per-segment setup is amortised over segment length. On highly fragmented data — measured worst case was shell script at roughly 43 bytes per segment — setup can outweigh the cheaper inner loop and make DP decode slower than v0.3.1 despite the smaller `k`. Two mitigations, in order of preference:

1. Section 12.1's constant-vector approach, which reduces setup to a set-bit walk with no memory traffic.
2. Raising `MIN_PASSTHROUGH_BYTES` (Section 6.4), which trades a little size to eliminate the short-segment case entirely.

Benchmarks SHOULD report decode throughput **as a function of segment length** (20, 50, 100, 300, 1024 bytes), not only as an aggregate. The aggregate hides exactly the case that matters.

### 12.4 Block mode is unchanged

Encode and decode of block-mode groups are identical to v0.3.1. Existing optimised implementations of that path carry over without modification, and performance on binary input is unaffected by this revision.

---

## 13. Conformance Testing

### 13.1 Structural

* `ALPHABET_N` has 85 characters, all distinct.
* `R_CHARS` has 13 entries, all distinct, none a member of Alphabet-N.
* `DONOR_ORDER` has 13 characters, all distinct, all members of Alphabet-N.
* For all 8192 values of `mask`: the derived donors are distinct, number exactly `popcount(mask)`, and no donor collides with an R-Set character.

### 13.2 Round-trip

* Exhaustive over all inputs of 1–3 bytes.
* Random binary at every length 0–40, plus 255, 256, 1023, 1024, 1025, 4096.
* Text drawn from `ALPHABET_N ∪ R_CHARS`, so that every R-Set character and every donor appears both as a substituted character and as a literal.
* Mixed text and binary, exercising DP↔block transitions.
* Each R-Set character and each donor placed individually at offsets 0, 1, 19, 20, 21, 1023 and 1024, to hit every boundary.

### 13.3 Canonicity

* The emitted `mask` contains exactly the R-Set characters present in the segment.
* No active donor occurs as a literal inside any emitted segment.
* The scan is maximal: re-scanning the prefix extended by one byte yields the same `L`.
* An encoder emits no character outside Alphabet-N, in either mode.

### 13.4 Cross-version — required

* Every v0.3.1 DP signal (payload 0, 1, 8191) is rejected as `LEGACY_SIGNAL`.
* Every v0.4.0 DP signal is rejected by a v0.3.1 decoder.
* **An encoder that OR-s `DP_SIGNAL_OFFSET` instead of adding it MUST fail this suite** (Section 9).

### 13.5 Adversarial decode

* Payload boundaries: 8191, 8192, `8192 + 2^23 − 1`, `8192 + 2^23`, `85^5 − 1`.
* Declared length exceeding the remaining stream — rejected *before* any read.
* Segment truncated by exactly one character.
* Signal truncated mid-group.
* Non-Alphabet-N character in a block group and inside DP data.
* **Whitespace injected inside DP data** — MUST be `INVALID_CHARACTER`, not `UNEXPECTED_EOS` (Section 7.1).
* Whitespace between constructs — MUST be accepted.
* Trailing group of one character; non-canonical trailing group; trailing group overflowing 2³².
* A rejected segment leaves no residual state in any shared decode structure.

### 13.6 Performance

* Encode time grows linearly in `N`, including on pseudorandom binary.
* Decode throughput reported per segment length band (Section 12.3).

Reference golden vectors and adversarial vectors accompany this document as `vectors.json` and `adversarial_vectors.json`.

---

## 14. Security Considerations

Base85N is an encoding, not a cryptographic transform. It provides no confidentiality, no integrity protection and no authentication. Encoded text is trivially reversible by anyone.

The decoder is the security-relevant surface, because it is by nature fed data the receiving system did not produce.

* **Length is attacker-controlled.** A DP segment's length field MUST be validated against the raw characters actually remaining *before* those characters are read, and MUST NOT size any read, copy or allocation without that check.
* **Mask is attacker-controlled.** It MUST be masked to 13 bits before use, even though `packed` is bounded by construction.
* **Whitespace handling is attack surface.** See the normative note in Section 7.1. Applying whitespace skipping inside DP data, or counting availability on stripped characters, creates a boundary disagreement between peers.
* **State must not survive an error.** Any implementation holding per-segment decode state MUST reset it on every exit path including error paths. Section 12.1's constant-vector approach avoids the problem structurally and is RECOMMENDED partly for that reason.
* **Output is arbitrary binary.** Decoded output may contain NUL bytes, control characters and invalid UTF-8. Callers MUST NOT assume it is printable, NUL-terminated or text.
* **Structure leaks content.** `mask` names exactly which of the 13 R-Set characters occur in a segment, and segment lengths reveal where text runs break. This is strictly more informative than v0.3.1's alphabet identifier. Base85N MUST NOT be relied on to hide such properties and MUST NOT appear in any constant-time path.
* **Bound total input.** There is no decompression amplification — decoded output is never larger than the input — but implementations SHOULD still bound the total input accepted from untrusted peers.

---

## 15. Open Items

### 15.1 `DONOR_ORDER` must be measured before 0.4.0 final

This is the only parameter in the document derived from intuition. The mechanism is sound regardless of the ordering; the *benefit* depends on it. Procedure:

1. Over 1 KB windows of each corpus type, record the literal frequency of all 23 Alphabet-N symbol characters.
2. Record the distribution of `k` — how many distinct R-Set characters occur per window. This identifies which ranks are actually hot.
3. Order the 13 donor candidates by ascending literal frequency across the union of corpora, weighted by the observed `k` distribution so that hot ranks dominate.

Ranks 0–4 are near-certain (`^ ~ ! ? %`). The measurement will mainly settle ranks 5–8, currently `$ # * @`, where usage is genuinely corpus-dependent: `#` is heavy in shell, Python and YAML comments and in CSS colours; `$` in shell, PHP and template literals; `@` in e-mail, decorators and CSS at-rules.

Changing `DONOR_ORDER` changes all encoder output and therefore all golden vectors. Fix it once, before publication.

### 15.2 Benchmark plan

Corpora per the v0.3.1 plan: GitHub Archive JSON, Reddit and Common Crawl for markup and prose, `bigcode/the-stack-smol` for code and shell, open-data CSVs.

Metrics:

* Overhead ratio, v0.3.1 vs v0.4.0.
* Block-mode fallback rate — share of bytes forced into block mode.
* Mean DP segment length — the single number that drives both signal overhead and setup amortisation.
* **Encode and decode throughput, separately, banded by segment length** (Section 12.3).

Two notes on experimental design:

* Chunk sizes above about 1 KB are not an interesting axis. Segments are capped at 1024 characters, so 10 KB and 100 KB chunks differ from 1 KB chunks only in boundary effects.
* Include a `compress + Base85N block mode` column. For size alone, compression beats every DP variant on text by a wide margin. DP's value is that it is compression-free and leaves data partially readable in transit. Reviewers should be able to see what DP is and is not competing on.

### 15.3 Status of the figures in Section 2.1

The size and `k` figures quoted there were produced by the reference prototype on small synthetic corpora with repeated content and near-zero block-mode fallback — unrealistically clean. The *ordering* of the results is expected to hold; the *magnitudes* are not yet established. Section 15.2 replaces them.
