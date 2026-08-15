# Base85N: A Protocol-Friendly Binary-to-Text Encoding

| Field | Value |
|---|---|
| Version | 0.4.0 |
| Status | Draft |
| Date | 2026-08-15 |
| License | MPL-2.0 |

> **Draft status.** This is a 0.x draft. The wire format is not frozen and may change in any subsequent 0.x version.
>
> **Self-contained.** This document defines the format completely. Earlier versions are neither referenced nor compatible; consult the repository history if the evolution is of interest.
>
> **Experimental "Solid Fill" Mode.** This version introduces a zero-data Fill Signal (Section 7.4) utilizing the upper signal space to encode runs of identical bytes. While the DP mode's field widths and donor profiles are derived from 17.9 MB of measured real-world text, the efficiency and threshold constants of the new Fill Mode remain theoretically derived and unbenchmarked on the corpus.

---

## 1. Abstract

Base85N is a binary-to-text encoding using a single 85-character alphabet (Alphabet-N) chosen for broad protocol compatibility. Its core is a 4-byte-to-5-character conversion. It features two advanced compression strategies:

1. A **Dynamic Passthrough (DP)** mode representing runs of printable text at exactly 1:1, partially human-readable, using only Alphabet-N characters and no escape mechanism. It dynamically masks and swaps forbidden characters with unused rare characters on a per-segment basis.
2. A **Solid Fill** mode that leverages the mathematical surplus of the 5-character signal space to encode runs of identical bytes (e.g., zero-padding or whitespace indents) with zero appended data characters.

Each segment is prefaced by a 5-character signal. To prevent decompression attacks, all segments strictly bound output generation to 2048 bytes per signal.

---

## 2. Introduction

### 2.1 Design summary

Values are carried in a single 5-character signal. The scheme distinguishes between pure data blocks, DP segments, and Fill segments solely through the numerical value of these 5 characters. 

For DP segments, the signal carries:
* **Mask.** A 13-bit field naming which of the 13 forbidden text characters (the **R-Set**) actually occur in the text.
* **Profile.** A 3-bit identifier selecting the priority ordering of substitute characters.
* **Length.** An 11-bit length (up to 2048 characters).

For Fill segments, the signal carries:
* **Byte Value.** An 8-bit value representing the repeating byte.
* **Length.** An 11-bit length (up to 2048 bytes).

Unless otherwise stated, *ratio* means encoded bytes divided by input bytes; percentage overhead is `(ratio - 1) × 100`.

### 2.2 Key properties

* **Density.** 5 output characters per 4 input bytes (1.25× size), against Base64's 4 characters per 3 bytes (≈1.333×).
* **Single alphabet.** Only Alphabet-N is ever emitted.
* **Escape-free DP.** One input byte always yields exactly one output character in DP mode.
* **High-Ratio Fill.** Up to 2048 identical bytes can be expressed in just 5 characters, yielding overheads as low as -99.7 %.
* **Padding-free.** Any input length, with a single canonical form for truncated trailing blocks.
* **Bounded Expansion.** No 5-character signal can yield more than 2048 bytes, preventing Zip-bomb denial-of-service.
* **Linear time**, guaranteed by construction (Section 6.6).

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

**Step 1 — Fill scan.** Determine the length `L_fill` of the contiguous run of identical bytes matching the very first byte. 
If `L_fill >= MIN_FILL_BYTES`:
* Emit the 5-character Fill signal for that byte and `min(L_fill, MAX_FILL_BYTES)` (Section 9).
* Consume the encoded bytes. Repeat the loop.

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

> **Normative.** On STOP, the values associated with the emitted segment MUST be those in effect **before** the byte that ended the scan was examined.

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
| DP_PAYLOAD_BITS | 27 | 3 profile + 13 mask + 11 length |
| FILL_PAYLOAD_BITS | 19 | 8 byte value + 11 length |
| NUM_PROFILES | 8 | All identifiers defined |

### 6.5 Canonicity

Encoder output is deterministic:
1. **Maximal Fill.** Identical byte runs MUST be processed by Fill mode if `L_fill >= MIN_FILL_BYTES`.
2. **Maximal DP prefix.** Take the longest valid prefix subject to the 2048-byte bound. 
3. **Smallest viable profile.** Choose the numerically smallest identifier.
4. **Exact mask.** `mask` SHALL have a set bit for every R-Set character occurring in the segment and for no other. 
5. **Empty mask implies profile 0.** If `mask = 0`, `profile` MUST be 0. 

### 6.6 Complexity

**Normative.** An encoder SHALL perform `O(N)` total input-byte inspections. Block mode is entered only when scans fail; block-mode iterations cost at most ~20 byte inspections per 4 bytes consumed. 

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

If `V >= 4 429 185 024` and `V < 4 429 709 312` (Fill range):

```
fill_payload = V - 4429185024
L            = (fill_payload & 0x7FF) + 1       # 1..2048
byte_value   = (fill_payload >> 11) & 0xFF      # 0..255

emit byte_value exactly L times
```
No significant characters are read from the stream to construct the data. The next character read will belong to the subsequent block or signal.

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
| 4 429 185 024 … 4 429 709 311 | Solid Fill signal (524,288 values) |
| 4 429 709 312 … 4 437 053 124 | FUTURE_SIGNAL_SPACE. MUST be rejected. |

Five characters span `85^5 = 4 437 053 125` values. Block mode occupies `2^32`, leaving **142 085 829** for signals. DP utilizes `2^27` states. Solid Fill requires `2^19` states (11 bits for length, 8 bits for byte value). **7 343 813** values remain as `FUTURE_SIGNAL_SPACE`.

**Payload constructions:**

*DP Payload:*
```
payload = (profile << 24) | (mask << 11) | L_enc      # L_enc = L - 1
V       = 2^32 + payload
```

*Fill Payload:*
```
fill_payload = (byte_value << 11) | L_enc             # L_enc = L - 1
V            = 4429185024 + fill_payload
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

Binary input is almost entirely block mode (Section 14.4). Where no run of `MIN_PASSTHROUGH_BYTES` representable bytes exists, DP is impossible and the encoder may skip in whole 4-byte groups. A regular expression over the representable byte class finds such runs in C.

> The quantifier MUST be `{20}`, not `{20,}`. Both find the same leftmost start, but a greedy `{20,}` consumes the entire run on every call, making repeated searches quadratic. Measured 0.2 MB/s versus 6.0 MB/s on real JSON — a **30×** difference.

The skip MUST be applied *after* the block group has been consumed. 

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

### 12.4 Adversarial decode
* Payload boundaries and future reserved spaces.
* Solid Fill signals generating up to 2048 bytes; ensure bounded memory consumption.

---

## 13. Security Considerations

Base85N is an encoding, not a cryptographic transform. The decoder is the security-relevant surface.

* **Length is attacker-controlled.** It MUST be validated before reading.
* **Bounded Decompression.** A 5-character Solid Fill signal can expand to a maximum of 2048 bytes. This establishes a strict upper limit on the decompression ratio (~410x), neutralizing "Zip bomb" denial-of-service threats via memory exhaustion.
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

| Scheme | text corpus overhead |
|---|---|
| eight fixed replacement alphabets | 2.62 % |
| **8 profiles, cap 2048** | **0.54 %** |

Full-corpus round-trip (DP + Block Mode only): text 1.00541, binary 1.24991.

### 14.4 What remains unmeasured

* **Solid Fill Efficiency.** The theoretical capability of the Solid Fill mode (Section 7.4) and its impact on the `0.54 %` overhead figure has not yet been benchmarked against the real-world text and binary corpora.
* **Compiled throughput.** All timings are CPython. 
* **Corpus composition is not a population.** The sources are English-language open-source repositories.
