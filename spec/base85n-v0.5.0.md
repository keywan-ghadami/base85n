# Base85N: A Protocol-Friendly Binary-to-Text Encoding

| Field | Value |
|---|---|
| Version | 0.5.0 |
| Status | Final |
| Date | 2026-08-16 |
| License | MPL-2.0 |

> **Final.** The wire format is frozen at this version and the feature set is closed. No subsequent 0.x version will change either. Version 1.0.0 is not claimed yet: that is a statement about time in the field, not about content, and nothing in this document is waiting on it.
>
> **Self-contained.** This document defines the format completely. Earlier versions are neither referenced nor compatible; they and the reasoning behind the format are in [`history/`](history/) if the evolution is of interest.
>
> **Fill with a tail.** Fill is one mode with two variants (Sections 6.1, 7.4 and 9). The *solid* variant is unchanged: one byte value repeated up to 2048 times. The *tail* variant spends the same five characters on a short run of zero bytes **and the two bytes beside it**, which block mode would otherwise charge 1.25 characters each for. Every threshold in this version is measured against the 6.52 MB benchmark corpus of Section 14.3, including `MIN_FILL_IN_SEGMENT_BYTES`, which moves from 11 to 16 for reasons that are not about ratio (Section 14.3).

---

## 1. Abstract

Base85N is a binary-to-text encoding using a single 85-character alphabet (Alphabet-N) chosen for broad protocol compatibility. Its core is a 4-byte-to-5-character conversion. It features two advanced compression strategies:

1. A **Dynamic Passthrough (DP)** mode representing runs of printable text at exactly 1:1, partially human-readable, using only Alphabet-N characters and no escape mechanism. It dynamically masks and swaps forbidden characters with unused rare characters on a per-segment basis.
2. A **Fill** mode that leverages the mathematical surplus of the 5-character signal space to encode runs of identical bytes (e.g., zero-padding or whitespace indents) with zero appended data characters. Its second variant also carries the two literal bytes that end a zero run, so that a run which stops short of a group boundary does not hand those bytes back to block mode.

Each segment is prefaced by a 5-character signal. To prevent decompression attacks, all segments strictly bound output generation to 2048 bytes per signal.

---

## 2. Introduction

### 2.1 Design summary

Values are carried in a single 5-character signal. The scheme distinguishes between pure data blocks, DP segments, and Fill segments solely through the numerical value of these 5 characters. 

For DP segments, the signal carries:
* **Mask.** A 13-bit field naming which of the 13 forbidden text characters (the **R-Set**) actually occur in the text.
* **Profile.** A 3-bit identifier selecting the priority ordering of substitute characters.
* **Length.** An 11-bit length (up to 2048 characters).

For Fill segments, the signal carries one of two payloads. The *solid* variant:
* **Byte Value.** An 8-bit value representing the repeating byte.
* **Length.** An 11-bit length (up to 2048 bytes).

The *tail* variant, whose repeated byte is always `0x00`:
* **Literals.** 16 bits: the two bytes adjoining the run.
* **Length.** A 5-bit length (1 to 32 zero bytes).
* **Order.** One bit: whether the literals follow the zeros or precede them.

The zero byte is not the feature here, it is the price: an arbitrary byte value *and* two literals would need 8 + 16 bits before any length field, and only 22 bits are available (Section 9).

Unless otherwise stated, *ratio* means encoded bytes divided by input bytes; percentage overhead is `(ratio - 1) × 100`.

### 2.2 Key properties

* **Density.** 5 output characters per 4 input bytes (1.25× size), against Base64's 4 characters per 3 bytes (≈1.333×).
* **Single alphabet.** Only Alphabet-N is ever emitted.
* **Escape-free DP.** One input byte always yields exactly one output character in DP mode.
* **High-Ratio Fill.** Up to 2048 identical bytes can be expressed in just 5 characters, yielding overheads as low as -99.7 %; a short zero run and its two neighbours cost the same five.
* **Padding-free.** Any input length, with a single canonical form for truncated trailing blocks.
* **Bounded Expansion.** No 5-character signal can yield more than 2048 bytes, preventing Zip-bomb denial-of-service.
* **Linear time**, guaranteed by construction (Section 6.6).
* **Parallelisable encoding.** Signals are self-describing and segment boundaries are decided by the data rather than by position, so encoders started at different offsets converge on the same decisions. An encoder may exploit this to use several cores and still produce the one canonical output (Section 11.3).

---

## 3. Conventions

BCP 14 [RFC2119] [RFC8174] keywords apply when, and only when, they appear in all capitals.

| Symbol | Meaning |
|---|---|
| profile | 3-bit donor profile identifier, 0–7 |
| mask | 13-bit field; bit j set ⟺ R-Set character j occurs in the DP segment |
| k | popcount(mask); number of active substitutions, 0 ≤ k ≤ 13 |
| rank(j) | popcount(mask & ((1 << j) - 1)); position of bit j among the set bits |
| L | Segment length, in bytes |

---

## 4. Alphabet, R-Set and Donor Profiles

### 4.0 Alphabet-N

85 characters with integer values 0–84.

| Values | Characters |
|---|---|
| 0–9 | 0 1 2 3 4 5 6 7 8 9 |
| 10–19 | a b c d e f g h i j |
| 20–29 | k l m n o p q r s t |
| 30–39 | u v w x y z A B C D |
| 40–49 | E F G H I J K L M N |
| 50–59 | O P Q R S T U V W X |
| 60–69 | Y Z . - : + = ^ ! / |
| 70–79 | * ? &#96; _ ~ ( ) [ ] { |
| 80–84 | } @ % $ # |

```
ALPHABET_N = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ.-:+=^!/*?`_~()[]{}@%$#"
```

### 4.1 R-Set

Thirteen characters that occur frequently in real text and are **not** members of Alphabet-N. The index *j* is normative and fixes bit positions in `mask`.

| j | Character | Byte | | j | Character | Byte |
|---|---|---|---|---|---|---|
| 0 | space | 0x20 | | 7 | < | 0x3C |
| 1 | " | 0x22 | | 8 | > | 0x3E |
| 2 | ' | 0x27 | | 9 | & | 0x26 |
| 3 | , | 0x2C | | 10 | TAB | 0x09 |
| 4 | ; | 0x3B | | 11 | LF | 0x0A |
| 5 | \ | 0x5C | | 12 | CR | 0x0D |
| 6 | &#124; | 0x7C | | | | |

The R-Set and Alphabet-N are disjoint.

### 4.2 Donor Profiles

A **donor profile** is an ordered sequence of exactly 13 distinct Alphabet-N characters. 

> **Note:** A profile is not an alphabet. It is an ordered donor ranking. Only its first `k` entries have semantic effect for a segment with `k` active R-Set characters.

| ID | Rank → 0 1 2 3 4 5 6 7 8 9 10 11 12 |
|---|---|
| 0 | ~ ^ ? % @ + &#96; $ # ! * . - |
| 1 | ~ ^ + [ ] &#96; ? @ ! % # * ( |
| 2 | ^ ~ $ # ? % ! &#96; @ [ ] + _ |
| 3 | ~ + ? % @ ! ^ [ ] : &#96; ( ) |
| 4 | ~ % ^ &#96; + ? ! $ @ ( ) { } |
| 5 | ^ ~ ? @ ! + % * $ ( ) _ # |
| 6 | ^ ~ @ % ? $ + ! # [ ] = * |
| 7 | ^ $ ~ @ ? ! % &#96; [ ] : } { |

### 4.3 Substitution Derivation

Given `profile` and `mask`:

```
rank = 0
for j in 0..12:
    if mask & (1 << j):
        donor(j) = PROFILE[profile][rank]
        rank += 1
```

The set bits of `mask` consume **the first `k` characters of the selected profile**.

Within a DP segment so described:
* An input byte equal to `R_CHARS[j]` for a set bit *j* SHALL be written as `donor(j)`.
* An input byte equal to any `donor(j)` is **not representable**; it cannot occur in the segment.
* Every other Alphabet-N character represents itself.
* Every other byte is not representable.

### 4.4 Escape Character

Base85N has no escape character and no escape sequences.

---

## 5. Endianness

All conversions between multi-byte integers and byte sequences MUST use big-endian byte order.

---

## 6. Encoding

### 6.1 Main Loop

A streaming encoder MUST buffer up to `MAX_DP_ANALYSIS_BYTES` bytes of lookahead, or reach end of input, before deciding.

While input remains:

**Step 1 — Fill scan.** Determine the length `L_fill` of the contiguous run of identical bytes matching the very first byte, and the coverage of each of the three Fill candidates. All three cost the same five characters, so they are compared by the number of input bytes they consume.

```
solid:    covers L_fill                       if L_fill >= MIN_FILL_BYTES
tail 0:   covers Z + 2, Z = min(L_fill, MAX_TAIL_ZEROS)
                                              if input[0] == 0x00
                                                 and Z >= MIN_TAIL_ZEROS
                                                 and Z + 2 bytes remain
tail 1:   covers Z + 2, Z = min(zero run at input[2], MAX_TAIL_ZEROS)
                                              if input[2] == 0x00
                                                 and Z >= MIN_TAIL_ZEROS
```

If any candidate applies:
* Emit the signal of the candidate with the **largest** coverage; on a tie prefer solid, then tail with order 0 (Section 6.5).
* Consume the bytes it covers. Repeat the loop.

A tail candidate's two literal bytes are the two immediately after its zeros for order 0, and the two immediately before them for order 1. They are ordinary bytes and MAY themselves be `0x00`.

**Step 2 — DP Prefix scan.** If Fill is not applicable, determine `(L, mask, profile)` per Section 6.2.

**Step 3 — Suitability.** Use DP mode if and only if **both** hold:
* `L >= MIN_PASSTHROUGH_BYTES`
* `5 + L <= ceil(L / 4) * 5`

If true, emit the DP signal (Section 9), immediately followed by `L` transformed characters. Consume `L` bytes.

**Step 4 — Block mode.** Otherwise encode exactly `min(4, remaining)` bytes per Section 6.3 and consume them.

### 6.2 DP Prefix Scan

The scan finds the longest valid prefix subject to the 2048-byte bound ($L = \min(L_{\text{first-invalid}}, 2048)$).

For each byte `b`, compute the *tentative* state that including `b` would produce, test it, and commit only on success:

```
if b is R_CHARS[j] for some j:
    if mask already has bit j:        accept, nothing changes
    new_k   = k + 1
    new_min = min_donor
else if b is an Alphabet-N character:
    r = per-profile rank vector of b   (13 where b is absent from that profile)
    new_min = elementwise min(min_donor, r)
    if new_min == min_donor:          accept, nothing changes
    new_k   = k
else:
    STOP                               # not representable under any mask

new_profile = smallest p with new_min[p] >= new_k
if no such p exists:
    STOP

commit:  mask, k, min_donor, profile  ←  tentative values
```

**Run break.** The scan SHALL also STOP at the first position `f` such that the `MIN_FILL_IN_SEGMENT_BYTES` bytes at `f` are identical and `f + MIN_FILL_IN_SEGMENT_BYTES <= L_max`, where `L_max` is the scan's own bound (Section 6.2's first paragraph). The prefix then ends at `f`, and the next iteration of the main loop emits that run as a Fill segment.

This is what lets Fill reach runs *inside* passthrough text and not only runs at a segment boundary. The threshold is higher than `MIN_FILL_BYTES` because the two situations differ in cost: at a segment boundary a run costs a Fill signal and nothing else, while inside a segment it also costs the 5-character signal that resumes passthrough afterwards. On ratio alone the break-even is 11 — but the threshold decides three other things as well, and all three want it higher: a run left inside a segment stays readable as itself, a decoder rebuilds one fewer substitution table, and the scan rolls back one fewer time. Ratio is flat from 13 to 16, and 16 is the top of that plateau (Section 14.3 measures all four columns).

> **Normative.** On STOP, the values associated with the emitted segment MUST be those in effect **before** the byte that ended the scan was examined. For a run break this is the state before the byte at `f`: the bytes of a run after its first cannot have changed the state, so exactly one byte's effect is rolled back.

### 6.3 Block Mode

Treat each 4-byte group as a 32-bit big-endian unsigned integer and convert to five Alphabet-N characters (Section 8).

For a final partial group of 1, 2 or 3 bytes, right-pad with zero bytes to four, convert, and emit only the first **2, 3 or 4** characters respectively. 

### 6.4 Constants

| Constant | Value | Notes |
|---|---|---|
| MAX_DP_ANALYSIS_BYTES | 2048 | Lookahead bound; bounds DP and Fill length |
| MAX_DP_SEGMENT_CHARS | 2048 | Equal, since DP is 1:1 |
| MIN_PASSTHROUGH_BYTES | 20 | Smallest L at which DP is never larger than block mode |
| MAX_FILL_BYTES | 2048 | Strict cap on identical bytes per Fill Signal |
| MIN_FILL_BYTES | 5 | At 4 bytes, Block mode is equal (5 chars). At 5, Fill is superior. |
| MIN_FILL_IN_SEGMENT_BYTES | 16 | Shortest run that ends a DP segment (Section 6.2). Below it, breaking out to Fill and back costs more than passthrough. |
| MIN_TAIL_ZEROS | 3 | Shortest zero run the tail variant is spent on. At 2, its 4 bytes cost the same 5 characters as block mode. |
| MAX_TAIL_ZEROS | 32 | Longest zero run the tail variant carries, matching its 5-bit length field. |
| DP_PAYLOAD_BITS | 27 | 3 profile + 13 mask + 11 length |
| FILL_PAYLOAD_BITS | 19 | 8 byte value + 11 length (solid variant) |
| TAIL_PAYLOAD_BITS | 22 | 16 literal + 5 length + 1 order (tail variant) |
| NUM_PROFILES | 8 | All identifiers defined |

### 6.5 Canonicity

Encoder output is deterministic:
1. **Maximal Fill.** At a decision point, if any Fill candidate of Section 6.1 applies, Fill mode MUST be used, and the candidate with the largest coverage MUST be chosen. Two candidates can only tie when the length fields of both saturate; the order **solid > tail 0 > tail 1** settles it. Inside a DP prefix, a run of `MIN_FILL_IN_SEGMENT_BYTES` identical bytes ends the prefix (Section 6.2) and is then processed by Fill at the next decision point. A shorter run inside a DP prefix is carried as passthrough data, and a tail candidate never ends a prefix — only the run break of Section 6.2 does.
2. **Maximal DP prefix.** Take the longest prefix the scan of Section 6.2 accepts — that is, the longest one subject to the 2048-byte bound, to profile viability, and to the run break of rule 1. 
3. **Smallest viable profile.** Choose the numerically smallest identifier.
4. **Exact mask.** `mask` SHALL have a set bit for every R-Set character occurring in the segment and for no other. 
5. **Empty mask implies profile 0.** If `mask = 0`, `profile` MUST be 0.
6. **Decision points are the encoder's own.** Steps 1 to 4 are evaluated at position 0 and at every position the loop reaches by consuming what a step accepted; block mode consumes exactly `min(4, remaining)` bytes. A construct that would apply at a position the loop never reaches MUST NOT be emitted. This is what makes the skip of Section 11.1 an optimisation rather than a second dialect: it may only pass over positions at which no step applies.

### 6.6 Complexity

**Normative.** An encoder SHALL perform `O(N)` total input-byte inspections. Block mode is entered only when both scans fail, which bounds each block-mode iteration at `MIN_PASSTHROUGH_BYTES` inspections for the DP scan plus `MIN_FILL_BYTES` for the Fill scan, per 4 bytes consumed. Both scans consume what they accept, so neither carries state between iterations. 

---

## 7. Decoding

### 7.1 Stream Structure and Whitespace

The decoder operates on a **logical stream** of significant characters obtained by removing ASCII space (U+0020), TAB (U+0009), LF (U+000A), and CR (U+000D) from the physical input stream. All subsequent decoding steps operate exclusively on this logical stream.

A signal's length field counts **significant** characters from this logical stream.

> **Normative implementation requirement.** Locating the end of `L` significant characters SHALL be done by counting **incrementally** over characters not yet examined to prevent quadratic time complexity on padded streams.

### 7.2 Block Groups

Convert five significant characters to `V` per Section 8. If `V < 2^32`, emit `V` as four big-endian bytes.

### 7.3 DP Segments

If `V >= 2^32` and `V < 4 429 185 024` (DP range):

```
payload = V - 2^32
L       =  (payload        & 0x7FF) + 1         # 1..2048
mask    =  (payload >> 11) & 0x1FFF             # MUST mask to 13 bits
profile =  (payload >> 24) & 0x7                # MUST mask to 3 bits

if fewer than L significant characters remain:  error UNEXPECTED_EOS
                                                # checked BEFORE reading

derive donor(j) from (profile, mask) per Section 4.3

for each of the next L significant characters c:
    if c is not in Alphabet-N:                  error INVALID_CHARACTER
    if c == donor(j) for some active j:         emit R_CHARS[j]
    else:                                       emit the byte value of c
```

`L_enc = 0` denotes a one-character segment; a decoder MUST NOT read it as empty.

### 7.4 Fill Segments

**Solid variant.** If `V >= 4 429 185 024` and `V < 4 429 709 312`:

```
fill_payload = V - 4429185024
L            = (fill_payload & 0x7FF) + 1       # 1..2048
byte_value   = (fill_payload >> 11) & 0xFF      # 0..255

emit byte_value exactly L times
```

**Tail variant.** If `V >= 4 429 709 312` and `V < 4 433 903 616`:

```
tail_payload = V - 4429709312
Z            =  (tail_payload >> 16) & 0x1F  + 1   # 1..32 zero bytes
order        =  (tail_payload >> 21) & 0x1
lit0         =  (tail_payload >>  8) & 0xFF
lit1         =   tail_payload        & 0xFF

if order == 0:  emit Z zero bytes, then lit0, then lit1
else:           emit lit0, then lit1, then Z zero bytes
```

Neither variant reads any significant character from the stream to construct its data. The next character read will belong to the subsequent block or signal.

### 7.5 Final Partial Block

A trailing group of 2, 3 or 4 significant characters is padded conceptually with `#` (value 84) to five, converted to `V`, and yields the first 1, 2 or 3 bytes respectively.

* A trailing group of exactly **1** character is an error.
* A padded trailing group whose `V >= 2^32` is an error.
* **Canonical enforcement.** The decoder MUST verify the characters are exactly the canonical prefix produced by encoding the resulting bytes zero-padded to four.

---

## 8. Value and Digit Conversion

```
digits_to_value(d[5]) :  V = ((((d0*85 + d1)*85 + d2)*85 + d3)*85 + d4)
value_to_digits(V)    :  for i from 4 down to 0:  d[i] = V % 85 ;  V = V / 85
```

---

## 9. Signal Interpretation

| Range of V | Interpretation |
|---|---|
| 0 … 4 294 967 295 | Standard 4-byte block |
| 4 294 967 296 … 4 429 185 023 | DP signal (134,217,728 values) |
| 4 429 185 024 … 4 429 709 311 | Fill signal, solid variant (524,288 values) |
| 4 429 709 312 … 4 433 903 615 | Fill signal, tail variant (4,194,304 values) |
| 4 433 903 616 … 4 437 053 124 | FUTURE_SIGNAL_SPACE. MUST be rejected. |

Five characters span `85^5 = 4 437 053 125` values. Block mode occupies `2^32`, leaving **142 085 829** for signals. DP utilizes `2^27` states, the solid Fill variant `2^19` (11 bits of length, 8 of byte value), the tail variant `2^22` (16 bits of literal, 5 of length, 1 of order). **3 149 509** values remain as `FUTURE_SIGNAL_SPACE`.

The tail variant's 22 bits are the widest field the format can still hold: above the solid variant only 7 343 813 values were left, and a 23-bit payload would need 8 388 608. That is why its length field is 5 bits rather than 8, and why the repeated byte is fixed at `0x00` rather than carried.

**Payload constructions:**

*DP Payload:*
```
payload = (profile << 24) | (mask << 11) | L_enc      # L_enc = L - 1
V       = 2^32 + payload
```

*Fill Payload, solid variant:*
```
fill_payload = (byte_value << 11) | L_enc             # L_enc = L - 1
V            = 4429185024 + fill_payload
```

*Fill Payload, tail variant:*
```
tail_payload = (order << 21) | (Z_enc << 16) | (lit0 << 8) | lit1
                                                      # Z_enc = Z - 1
V            = 4429709312 + tail_payload
```

---

## 10. Error Handling

| Code | Condition |
|---|---|
| INVALID_CHARACTER | A significant character outside Alphabet-N |
| UNEXPECTED_EOS | Input ends while a block, signal or segment is still required |
| UNDEFINED_SIGNAL | V in FUTURE_SIGNAL_SPACE |
| INVALID_FINAL_BLOCK | Trailing group of one character; padded value ≥ 2³²; or non-canonical |

An implementation MUST NOT read outside its input buffer or terminate the process on malformed input.

---

## 11. Implementation Guidance (Informative)

### 11.1 Encoder: skipping binary stretches

Binary input is almost entirely block mode (Section 14.4), and step 4 consumes only 4 bytes per iteration, so the two scans are re-entered every 4 bytes for nothing. An encoder may skip ahead instead, and the skip is exact rather than heuristic once one observation is made: **inside a block-mode run the loop only ever visits positions `off`, `off + 4`, `off + 8`, …**, so only those positions have to be tested.

At each of them, three tests decide it, all bailing out on their first counterexample:

* a solid Fill segment starts there if and only if `MIN_FILL_BYTES` equal bytes do;
* a tail Fill segment starts there if and only if `MIN_TAIL_ZEROS` zero bytes start at the position or two bytes past it. Both cases put a zero at `off + 2`, so one load gates the pair and settles it for input with no zeros in it;
* a DP segment can start there only if `MIN_PASSTHROUGH_BYTES` representable bytes do — 98 of the 256 byte values are representable, so on high-entropy input this fails on the first or second byte.

The encoder may then jump to the last 4-byte boundary at or before the position found. Every position passed over would have taken step 4 and consumed exactly 4 bytes, and block mode over a whole number of groups is the concatenation of the per-group results, so the output is unchanged.

> **The skip must be exact, not approximate.** Section 6.5's rule 6 makes the visited positions part of the canonical form, so a skip that passes over a position where any step applies produces a different, non-conforming stream. Stopping *too early* is always safe: the loop simply re-decides.

An implementation may also hoist the three tests' bound checks by splitting the loop where the whole `MIN_PASSTHROUGH_BYTES` window is known to be in the input. Measured over 400 kB of pseudorandom bytes, the skip and that split together leave the C encoder marginally faster than the same encoder is with neither (0.97× its instruction count), and on text it is neutral.

### 11.2 Encoder: tracking eight profiles at once

The prefix scan has to keep, per profile, the lowest rank any literal Alphabet-N character has held in it; a profile is viable exactly while that number is at least `k`. Eight such numbers fit in one 64-bit word, one per byte lane, and both operations the scan needs are then branch-free:

```
lane_ge(x, y) = ((x | 0x8080...80) - y) & 0x8080...80    # lane bit set iff x >= y
lane_min(x, y): m = (lane_ge(x, y) >> 7) * 0xFF ; (x & ~m) | (y & m)
```

Both rely on every lane holding a value below 128, which ranks (0–13) and `k` (0–13) satisfy. The smallest viable profile is then the lowest set lane of `lane_ge(min_donor, k)`. Only 22 of the 85 alphabet characters appear in any profile, so a single lookup settles the common case without touching the word at all. 

### 11.3 Encoder: several cores, one canonical output

Two properties of the format make an encoder parallelisable without a chunk-size parameter, and therefore without giving up canonicity:

* **Signals are self-describing.** Mask, profile, byte value, length and order all live in the signal. No state crosses a signal boundary, so a segment can be produced without knowing what preceded it.
* **Segment boundaries are decided by the data.** A DP prefix ends at the first byte it cannot carry or at a run break; a Fill candidate begins where a run does. These are properties of absolute positions in the input, not of where an encoder happened to start.

Two encoders started at different offsets therefore tend to make the same decisions once past a common construct — their chains of decision points **converge**. A parallel encoder exploits that:

```
1. Split the input into chunks. Each worker encodes its chunk speculatively
   from the chunk's first byte, recording the positions it decided at.
2. In order, worker N's real chain ends at some position E (usually past the
   chunk boundary, since the last construct may cross it). If E appears in
   worker N+1's recorded positions, everything worker N+1 emitted from E
   onwards is byte-identical to what a sequential encoder would have emitted;
   discard the speculative part before E and splice.
3. If E does not appear, encode forward from E until a position in worker
   N+1's list is reached -- the convergence distance -- and splice there.
```

The result is the sequential output, byte for byte; there is no second canonical form and nothing to configure.

Convergence distance is what bounds the gain, and it is not small. Measured over the benchmark corpus at 3,900 random boundaries: median 1,078 bytes, mean 9,892, 95th percentile 50,604, maximum 226,279. Block mode does *not* resynchronise after four bytes — two encoders inside a block-mode run stay four-byte-out-of-phase until some construct realigns them, and a file that is one long DP stream (`bootstrap.css`, median 21,497) or high-entropy binary (`_cffi_backend.so`, mean 32,377) can run a long way before that happens. Chunks should therefore be **at least a megabyte**; at that size the discarded speculation and the sequential repair are both about 1 % of the work, and the repair is what remains strictly sequential.

---

## 12. Conformance Testing

### 12.1 Structural
* `ALPHABET_N` has 85 distinct characters.
* `R_CHARS` has 13 distinct entries, none in Alphabet-N.
* Each of the 8 profiles has 13 distinct characters, all in Alphabet-N.

### 12.2 Round-trip
* Random binary at every length 0–64, plus 255, 256, 1023, 1024, 1025, 2047, 2048, 2049, 4096.
* Sequences of identical bytes traversing `MIN_FILL_BYTES` and `MAX_FILL_BYTES` boundaries.
* Mixed text, solid runs, and binary, exercising Fill↔DP↔block transitions.

### 12.3 Canonicity

* **No active donor occurs as a literal inside any emitted segment.** 
* The emitted `profile` is the smallest viable one for the accepted prefix.
* `mask = 0` is emitted only with `profile = 0`.
* `mask` has a set bit for every R-Set character in the segment and no other, including where a run break rolled the state back one byte.
* Every trailing group is the canonical encoding of the bytes it decodes to, and a decoder rejects every other group that pads to the same bytes (Section 7.5).
* Where a solid and a tail candidate both apply, the emitted signal is the one covering more bytes, and the tie order of Section 6.5 rule 1 is respected.
* An encoder that skips ahead (Section 11.1) emits exactly what one that does not emits, on inputs whose zero runs sit at every offset modulo four.

### 12.4 Adversarial decode
* All three signal ranges of Section 9 from both sides, and values in `FUTURE_SIGNAL_SPACE`.
* Both length fields' bias of one, at both ends of their range.
* Every profile identifier over the same segment data, and partial masks, so that a decoder which ignores the profile or derives the donors in the wrong order is caught.
* Fill signals of the solid variant generating up to 2048 bytes, and back to back; ensure bounded memory consumption.
* Both ends of the tail variant's range, both values of its order bit, its length field's bias of one, and literals that are themselves `0x00`.
* Trailing groups that pad to the right bytes without being their encoding.

---

## 13. Security Considerations

Base85N is an encoding, not a cryptographic transform. The decoder is the security-relevant surface.

* **Length is attacker-controlled.** It MUST be validated before reading.
* **Bounded Decompression.** A 5-character Fill signal can expand to a maximum of 2048 bytes in the solid variant and 34 in the tail variant. This establishes a strict upper limit on the decompression ratio (~410x), neutralizing "Zip bomb" denial-of-service threats via memory exhaustion.
* **Whitespace counting is a DoS vector.** See the normative requirement in Section 7.1.
* **Output is arbitrary binary.** Callers MUST NOT assume printable, NUL-terminated or text.

---

## 14. Measurements

### 14.1 Corpora

**Text: 17.91 MB, 2 317 real files** — source in C, Go, Rust, JavaScript/TypeScript and Python; Markdown and HTML; YAML, TOML and shell; open-data CSV; npm and PyPI JSON.
**Binary: 8.26 MB** — media files, gzip-compressed tarballs, SHA-256 digest streams.

### 14.2 Profile derivation

Derived on 0.94 MB of training data and evaluated on a disjoint 3.20 MB hold-out set. Eight profiles capture 72.1% of the theoretical oracle headroom. 

### 14.3 Overall

Two corpora are in play. The first is the derivation corpus of Section 14.1, on which the field widths and the profile table were chosen:

| Scheme | text corpus overhead |
|---|---|
| eight fixed replacement alphabets | 2.62 % |
| 8 profiles, cap 2048 (this version) | **0.54 %** |

The second is the repository's benchmark corpus — 6.52 MB across 13 real files, none of them used to derive anything: three binary container formats, an uncompressed tar of a source release, a JSON dataset pretty-printed and minified, JavaScript, CSS and Python source, the CommonMark specification, a Markdown changelog, a JPEG and a PNG. Encoded characters per input byte:

| Configuration | text | binary | whole corpus |
|---|---|---|---|
| DP + Block Mode only | 1.00377 | 1.21072 | 1.10581 |
| + Fill at segment boundaries (`MIN_FILL_BYTES` = 5) | 1.00342 | 1.10798 | 1.05498 |
| + Fill inside segments, threshold 11 | **0.94603** | 1.10487 | 1.02435 |
| + Fill inside segments, threshold 16 | 0.96474 | 1.10596 | 1.03437 |
| + the tail variant (this version) | 0.96474 | **1.05041** | **1.00698** |

The best value in each column is marked. This version holds two of the three and gives up the text column on purpose: read down the last two rows, the threshold change costs text 1.9 % and the tail variant gives binary 5.0 % back.

**The tail variant.** It is worth 2.7 % of the whole corpus, and nearly all of it comes from one file — which is the file it was designed for. A zero-padded ELF goes from 1.12618 to 0.96541, and Ascii85, the only established codec that beat Base85N anywhere in this corpus, sits at 1.026 on it. Eight of the thirteen files encode to exactly the same stream with the variant as without it. Encoding the length directly rather than in steps of four is what makes it worth having: a length field that only lands on multiples of four leaves 0 to 3 zeros behind for block mode, and measured that way the same construct was worth 0.9 % of the corpus instead of 2.7 %.

**`MIN_FILL_IN_SEGMENT_BYTES` = 16, and why not 11.** Eleven is where ratio alone puts the break-even. Ratio is not the only thing the threshold moves:

| threshold | corpus ratio | bytes carried in DP segments | DP segments | `countries.json` decode | `countries.json` encode |
|---|---|---|---|---|---|
| 11 | **0.99696** | 3 160 919 | 22 354 | 482 MB/s | 158 MB/s |
| 13 | 1.00661 | 3 522 974 | 18 789 | — | — |
| 16 (this version) | 1.00698 | 3 531 218 | 18 248 | **643 MB/s** | **198 MB/s** |
| 18 | 1.02058 | **3 770 831** | **6 224** | — | — |

The best value in each column is marked; ratio and segment count are better lower, the other three better higher. No threshold holds more than two columns, which is what makes this a choice rather than an optimum.

Ratio is flat from 13 to 16 and then steps; 16 is the top of that plateau. It leaves 370 000 more bytes of the corpus inside readable DP segments, gives the decoder 4 100 fewer substitution tables to build, and takes the corpus's slowest decode line — `countries.json`, last of four codecs at 937 MB/s in the repository's benchmark — up by a third. The 1.0 % of ratio that costs is spent on an axis where every alternative is at 1.25 or worse.

For comparison on the same corpus: Base64 1.3333, Ascii85 1.1881, Z85 1.2500, Base85 (RFC 1924) 1.2500. Full method and per-file numbers: `bench/results/RESULTS.md` in the repository.

### 14.4 What remains unmeasured, and what is knowingly left on the table

* **Corpus composition is not a population.** The sources are English-language open-source repositories, and the benchmark corpus is 13 files. A file whose bytes are neither text-like nor repetitive lands on block mode's 1.25, and no measurement here changes that. Thresholds tuned to three decimal places on 13 files are fitted to those 13 files; the ones in Section 6.4 are chosen at plateau edges rather than at optima for that reason.
* **Short space runs are not compressed.** With the in-segment break at 16, a run of 4 to 15 spaces is carried through Dynamic Passthrough one character per byte. That is 53 673 runs and 448 321 bytes across the benchmark corpus — 6.9 % of it — at 1.0 rather than at some fraction. Python at indentation depth one or two, and flat JSON and XML, are the cases. It is a known gap and not a defect: 1.0 already beats Base64's 1.333 and every other Base85's 1.25 on the same bytes. Closing it needs a construct that resumes passthrough without a fresh signal, which would put state across a signal boundary and cost the property Section 11.3 is built on.
* **Fill's expansion bound is now fuzzed, its interaction with allocators is not.** 60 000 randomised round trips over inputs built from zero runs, other runs, text and random bytes pass under AddressSanitizer and UndefinedBehaviorSanitizer, as do 60 000 arbitrary Alphabet-N strings put through the decoder. That exercises the bound of Section 13; it does not model an allocator under memory pressure.
