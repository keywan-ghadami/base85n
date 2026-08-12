# Base85N Specification

| Field | Value |
|---|---|
| **Version** | 0.3.0 |
| **Status** | Draft |
| **Date** | 2026-08-12 |
| **Editor** | Keywan Ghadami |
| **License** | [MPL-2.0](https://github.com/keywan-ghadami/base85n/blob/main/LICENSE) |
| **Canonical URI** | <https://github.com/keywan-ghadami/base85n/blob/main/spec/base85n-v0.3.0.md> |
| **Predecessor** | [0.2.0](https://github.com/keywan-ghadami/base85n/blob/main/spec/base85n-v0.2.0.md) (**wire format changed**; see Section 14) |

> **Draft status.** This is a 0.x draft specification. It is implemented and
> tested (see Section 12), but it has not been through independent review and
> the wire format is not yet frozen. Anything in this document MAY change in a
> subsequent 0.x version. Do not use Base85N for data that must remain
> decodable by future versions without a migration path.
>
> **This version is not compatible with 0.2.0.** The Dynamic Passthrough signal
> layout and the meaning of Dynamic Passthrough data both changed. A 0.3.0
> decoder MUST NOT be fed 0.2.0 output and vice versa; Section 14 records what
> changed and why.
>
> **AI-assisted authorship.** Large parts of this document, and all five
> reference implementations, were produced with AI assistance. See
> [the repository README](https://github.com/keywan-ghadami/base85n#ai-generated-code--notice)
> and [SECURITY.md](https://github.com/keywan-ghadami/base85n/blob/main/SECURITY.md)
> before relying on it.

## 1. Abstract

This document defines Base85N, a binary-to-text encoding scheme using a single
85-character alphabet (Alphabet-N) selected for broad compatibility and protocol
friendliness. It features a 4-byte-to-5-character core conversion mechanism. An
enhanced Dynamic Passthrough (DP) mode enables efficient, partially
human-readable representation of compatible byte sequences using only
Alphabet-N characters. DP operates by selecting, per segment, one of eight
fixed **replacement alphabets**: each is Alphabet-N with a small number of its
rarest characters given up so that frequently occurring characters outside
Alphabet-N — space, newline, quotes, and the other R-Set characters of Section
4.1 — can be carried directly. Because each replacement alphabet is injective,
DP needs no escape mechanism and achieves exactly 1:1 efficiency on its data,
plus a 5-character signal per segment. A fallback to standard Base85N block
encoding is used when DP would be less efficient, or if the original data
contains bytes that no replacement alphabet can represent. The scheme supports
padding-free encoding and decoding.

## 2. Introduction

Binary data, such as cryptographic keys, identifiers, or media files, often
needs representation as text. Common formats like JSON, XML, HTML, etc., often
impose character set restrictions. Base64 is common but inefficient (approx. 33%
overhead). Base85 variants are denser. This specification defines Base85N, a
Base85 variant aiming for high efficiency combined with a broadly compatible,
single alphabet (Alphabet-N).

It includes a Dynamic Passthrough (DP) mechanism intended to represent byte
sequences that consist largely of printable text. The difficulty such a
mechanism has to solve is that the useful characters do not fit: Alphabet-N has
85 characters, and the R-Set characters that ordinary text is full of — space
above all — are not among them. Adding all 13 R-Set characters would require 98
distinct output characters, which 85 cannot carry.

Base85N resolves this by *trading*. A replacement alphabet gives up k of
Alphabet-N's rarest characters and uses the freed positions to carry k R-Set
characters instead. The result is still a set of 85 output characters, and the
mapping from representable input byte to output character is still injective —
so no escape character is needed to tell a substituted character from a literal
one. What a segment pays instead is that its k given-up characters are simply
not representable in that segment; encountering one ends the segment. Eight such
alphabets are defined, and the encoder picks one per segment, so the trade can
follow the shape of the data. DP commitment requires a minimum input data length
(MIN_PASSTHROUGH_BYTES).

### 2.1. Key Features and Rationale

 * High Data Density: Core 4-byte to 5-character Base85N conversion offers
   better density than Base64.
 * Protocol-Friendly Alphabet (Alphabet-N): Base85N exclusively uses Alphabet-N,
   based on (but distinct from) z85 with minor modifications, which aims for
   broad compatibility (including HTML, XML, JSON contexts).
 * Dynamic Passthrough (DP) using Alphabet-N: Attempts to directly represent
   sequences of bytes. DP mode exclusively emits Alphabet-N characters. Each DP
   segment names one of the eight replacement alphabets of Section 4.2, and its
   data is that alphabet applied byte by byte.
 * Escape-Free: Because a replacement alphabet is injective, DP data contains no
   escape sequences. One input byte always produces exactly one output
   character, so a DP segment's character length equals its byte length.
 * Adaptive Block Mode Fallback Strategy: The encoding algorithm (detailed in
   Section 6) processes data in segments, scanning for a prefix that some
   replacement alphabet can represent.
   * If such a prefix is found, its DP-encoded length is compared to its
     standard block-encoded length. DP mode is chosen for this prefix only if it
     is shorter or equal.
   * If DP mode is not chosen for this prefix (either because it's longer, or no
     suitable prefix meeting minimum length and representability was found), the
     encoder falls back to standard Base85N block processing. If a DP-suitable
     prefix was identified but found inefficient for DP, that entire prefix is
     block-encoded. Otherwise (no suitable DP prefix found), a smaller,
     standard-sized block (typically 4 bytes, or fewer at stream end) is
     block-encoded.
 * Bounded Lookahead: The encoder analyses at most MAX_DP_ANALYSIS_BYTES (1024)
   bytes when identifying a DP prefix, so a DP segment carries at most 1024
   bytes and its signal's length field never has to describe more.
 * Padding-Free: Handles input of any length using standard Base85N partial
   block encoding in Block mode.

## 3. Conformance Requirements

The keywords "MUST", "MUST NOT", "REQUIRED", "SHALL", "SHALL NOT", "SHOULD",
"SHOULD NOT", "RECOMMENDED", "MAY", and "OPTIONAL" are to be interpreted as
described in RFC 2119.

## 4. Alphabet (Alphabet-N)

Base85N uses a single 85-character alphabet, referred to as Alphabet-N. Each
character is assigned a unique integer value from 0 to 84.
The character assignments for Alphabet-N, corresponding to their integer values
(indices), are presented below:

| Values (Indices) | Alphabet-N Characters |
|---|---|
| 0-9 | 0 1 2 3 4 5 6 7 8 9 |
| 10-19 | a b c d e f g h i j |
| 20-29 | k l m n o p q r s t |
| 30-39 | u v w x y z A B C D |
| 40-49 | E F G H I J K L M N |
| 50-59 | O P Q R S T U V W X |
| 60-69 | Y Z . - : + = ^ ! / |
| 70-79 | * ? `` ` `` _ ~ ( ) [ ] { |
| 80-84 | } @ % $ # |

String Representation for Implementations:
ALPHABET_N_CHARS_STR = '0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ.-:+=^!/*?`_~()[]{}@%$#'

### 4.1. R-Set Characters (Alphabet_R)

The R-Set defines a standardized set of 13 characters that are candidates for
substitution in Dynamic Passthrough (DP) mode. These are characters that occur
frequently in real text but are not part of Alphabet-N. The R-Set characters are
assigned fixed indices 0-12; a replacement alphabet (Section 4.2) refers to them
by these indices.

| Index (j) | R_Char (Conceptual) | ASCII Value | Notes |
|---|-----|-----|-----|
| 0 | (space) | 32 | Space |
| 1 | " | 34 | Double quote |
| 2 | ' | 39 | Single quote |
| 3 | , | 44 | Comma |
| 4 | ; | 59 | Semicolon |
| 5 | \ | 92 | Backslash |
| 6 | \| | 124 | Pipe symbol |
| 7 | < | 60 | Less-than symbol |
| 8 | > | 62 | Greater-than symbol |
| 9 | & | 38 | Ampersand |
| 10 | \t (tab) | 9 | Horizontal Tab |
| 11 | \n (newline) | 10 | Line Feed |
| 12 | \r (car. ret) | 13 | Carriage Return |

This table is unchanged from version 0.2.0. What changed is how a segment
selects among these characters: 0.2.0 carried a 13-bit mask, 0.3.0 carries a
3-bit replacement alphabet identifier.

### 4.2. Replacement Alphabets

Eight replacement alphabets are defined, identified by the values 0 through 7.
A DP segment's signal (Section 9) names exactly one of them, and that alphabet
governs the whole segment.

A replacement alphabet is a set of **substitutions**. Each substitution names an
R-Set index j (Section 4.1) and a **donor character**, which is an Alphabet-N
character. Within a segment encoded under that alphabet:

 * an input byte equal to R_Char[j] SHALL be written as the donor character;
 * an input byte equal to the donor character is **not representable** under that
   alphabet, and therefore cannot appear in that segment at all;
 * every other Alphabet-N character represents itself;
 * every other byte is not representable.

No donor character appears twice within one alphabet, and no R-Set index appears
twice, so the mapping from representable input byte to output character is
injective. This is the property that makes an escape character unnecessary: a
donor character in DP data has exactly one meaning, fixed by the alphabet
identifier in the signal.

The eight alphabets:

| ID | Name | Substitutions (R_Char -> donor character) |
|---|---|---|
| 0 | none | *(no substitutions; all 85 Alphabet-N characters represent themselves)* |
| 1 | text | space->`^` LF->`@` CR->`%` TAB->`$` |
| 2 | prose | space->`^` LF->`@` `,`->`%` `"`->`$` `'`->`?` `;`->`!` |
| 3 | markup | space->`^` LF->`@` `<`->`%` `>`->`$` `&`->`?` `"`->`!` `'`->`~` `,`->`{` |
| 4 | json | space->`^` LF->`@` `"`->`%` `,`->`$` `\`->`?` CR->`!` |
| 5 | code | space->`^` LF->`@` `,`->`%` `;`->`$` `"`->`?` `'`->`!` TAB->`~` `>`->`` ` `` |
| 6 | shell | space->`^` LF->`@` `\|`->`%` `\`->`$` `"`->`?` `'`->`!` `&`->`~` `;`->`#` |
| 7 | full | space->`^` LF->`@` CR->`%` TAB->`$` `,`->`?` `;`->`!` `"`->`~` `'`->`#` `<`->`*` `>`->`+` `&`->`=` `\|`->`_` `\`->`` ` `` |

The same table by R-Set index, which is the form an implementation wants — each
cell is the donor character for that R-Set index under that alphabet, and an
empty cell means that R-Set character is not representable under that alphabet:

| j | R_Char | A0 | A1 | A2 | A3 | A4 | A5 | A6 | A7 |
|---|---|---|---|---|---|---|---|---|---|
| 0 | space | | `^` | `^` | `^` | `^` | `^` | `^` | `^` |
| 1 | `"` | | | `$` | `!` | `%` | `?` | `?` | `~` |
| 2 | `'` | | | `?` | `~` | | `!` | `!` | `#` |
| 3 | `,` | | | `%` | `{` | `$` | `%` | | `?` |
| 4 | `;` | | | `!` | | | `$` | `#` | `!` |
| 5 | `\` | | | | | `?` | | `$` | `` ` `` |
| 6 | `\|` | | | | | | | `%` | `_` |
| 7 | `<` | | | | `%` | | | | `*` |
| 8 | `>` | | | | `$` | | `` ` `` | | `+` |
| 9 | `&` | | | | `?` | | | `~` | `=` |
| 10 | TAB | | `$` | | | | `~` | | `$` |
| 11 | LF | | `@` | `@` | `@` | `@` | `@` | `@` | `@` |
| 12 | CR | | `%` | | | `!` | | | `%` |

Note that the same donor character means different things under different
alphabets — `%` carries CR under A1, a comma under A2 and A5, a double quote
under A4, and a pipe under A6. This is unambiguous because a segment's alphabet
is fixed by its signal before any of its data is read.

#### 4.2.1. How these were chosen

This subsection is informative.

A donor character costs whatever its own frequency in the data is: every literal
occurrence of it ends the segment. The donors are therefore drawn, in order,
from the least frequent Alphabet-N characters as measured over a corpus of
JSON, Markdown, CSS, tab-separated data and source code in five languages:

`^` `@` `%` `$` `?` `!` `~` `#` each occur fewer than 0.3 times per 1000 bytes,
followed at some distance by `*` `+` `=` `_` `` ` ``. An alphabet needing k
substitutions takes the first k of that order, except where the alphabet's own
target shape makes one of them common: A3 (markup) spends `{` rather than `#`,
because `#` begins a Markdown heading, and A5 (code) spends `` ` `` , which is
rare in source code but is the single most common special character in Markdown
(213 per 1000 bytes).

The eight alphabets are not eight equally important choices. Measured over that
corpus, Dynamic Passthrough with no substitution at all is worth almost nothing
(124.5% of input size, against block mode's 125%); a single alphabet carrying
all 13 R-Set characters — A7 — reaches 104.0%; all eight together reach 102.5%.
Most of the benefit is in the first few, and the remainder is worth including
because the signal spends 3 bits on the identifier either way.

### 4.3. Escape Character

Version 0.3.0 has no escape character. The character `~` (tilde, at index 74 of
Alphabet-N) is an ordinary Alphabet-N character: it represents itself in DP data
under every alphabet that does not use it as a donor, and it is a donor under
alphabets 3, 5 and 6.

Implementers coming from version 0.2.0 should note that this removes the
`~`-prefixed two-character sequences from DP data entirely, along with the
MAX_CONSECUTIVE_ESCAPES limit and the "Dangling Escape Character" error
condition.

## 5. Endianness

All conversions between multi-byte integers and byte sequences MUST use
Big-Endian byte order.

## 6. Encoding Algorithm

The encoding algorithm processes an input byte stream, accumulating data into an
intermediate_buffer. It exclusively uses Alphabet-N. The algorithm adaptively
chooses between Dynamic Passthrough (DP) mode and standard Block mode for
segments of the input data.

### 6.1. Main Encoding Loop

Initialize output_string = "".
Input data is progressively accumulated into intermediate_buffer.
The loop continues as long as intermediate_buffer is not empty.
Inside the loop, the following sequence of steps SHALL be performed:

**1. Dynamic Prefix Identification**

A byte B is said to be *representable under alphabet a* when either

 * B equals R_Char[j] for some R-Set index j that alphabet a substitutes, or
 * chr(B) is in ALPHABET_N_CHARS_STR and chr(B) is not a donor character of
   alphabet a.

Every other byte is not representable under a. Note that representability
depends only on B and a — not on position, not on any other byte in the buffer,
and not on anything decided earlier in the scan. This is what lets version 0.3.0
identify a prefix in a single forward scan where version 0.2.0 needed two.

For each alphabet identifier a from 0 to 7, the encoder SHALL determine L(a):
the number of leading bytes of intermediate_buffer that are representable under
a, bounded above by MAX_DP_ANALYSIS_BYTES. That is, the scan for a stops at the
first byte not representable under a, or after MAX_DP_ANALYSIS_BYTES bytes,
whichever comes first.

dp_alphabet SHALL be the identifier a for which L(a) is greatest. If several
alphabets achieve the same greatest L(a), the **numerically smallest** such
identifier SHALL be chosen. This tie-break is normative: without it, two
conforming encoders could produce different output for the same input, and the
shared test vectors of Section 12 would not be shared.

dp_candidate_prefix SHALL be the first L(dp_alphabet) bytes of
intermediate_buffer, and L_transformed SHALL be L(dp_alphabet). The two are
equal because a replacement alphabet maps one input byte to exactly one output
character; version 0.3.0 has no construct that spends two characters on one
byte.

An implementation MAY compute the eight scans in any order or interleaved, and
MAY stop a scan early once it cannot beat the best L(a) found so far, provided
the chosen dp_alphabet and dp_candidate_prefix are those specified above.

**2. Decision and Processing**

a. DP Suitability Check: A boolean flag use_dp_mode is initialized to false. The
encoder SHALL check if both of the following conditions are met:

 * The length of dp_candidate_prefix is >= MIN_PASSTHROUGH_BYTES.
 * The calculated conceptual_dp_output_length is less than or equal to the
   block_mode_output_length for the dp_candidate_prefix, where:
   * conceptual_dp_output_length = 5 + L_transformed (one 5-character signal
     followed by the segment's characters);
   * block_mode_output_length = ceil(length(dp_candidate_prefix) / 4) * 5.

If both conditions are true, use_dp_mode SHALL be set to true.

With MIN_PASSTHROUGH_BYTES = 20 the second condition follows from the first —
at length 20 both sides are exactly 25 characters, and DP's advantage only grows
after that — so a conforming encoder may implement the check as the length test
alone. It is stated in full because both constants are subject to change while
the specification is at 0.x.

b. Processing Execution:

If use_dp_mode is true:

 * A 5-character signal carrying dp_alphabet and the exact character length of
   the segment (Section 9) SHALL be emitted, immediately followed by the
   L_transformed transformed characters: for each byte of dp_candidate_prefix in
   order, the donor character if the byte is a substituted R_Char under
   dp_alphabet, otherwise the byte's own Alphabet-N character.
 * The resulting string SHALL be appended to output_string.
 * The bytes corresponding to dp_candidate_prefix SHALL be removed from
   intermediate_buffer.

Because MAX_DP_ANALYSIS_BYTES bounds dp_candidate_prefix at 1024 bytes and the
transformation is 1:1, a candidate prefix is always carried by exactly one
signal. Version 0.2.0's DP Output Segmentation rule, which split a long prefix
across several signals and had to avoid splitting an escape pair, has no
counterpart here.

If use_dp_mode is false:

Let R = length(dp_candidate_prefix) mod 4.

 * If length(dp_candidate_prefix) >= 4 (i.e., dp_candidate_prefix contains at
   least one complete 4-byte group): the leading
   (length(dp_candidate_prefix) - R) bytes of dp_candidate_prefix — an exact
   multiple of 4 — SHALL be encoded using ProcessWithBlockMode (see Section
   6.2); since this length is already a multiple of 4, no padding or truncation
   occurs. The result is appended to output_string, and only these bytes SHALL
   be removed from intermediate_buffer.
 * The trailing R bytes (0 <= R <= 3) of dp_candidate_prefix, if any, SHALL NOT
   be removed from intermediate_buffer and SHALL NOT be padded now; they remain
   at the front of intermediate_buffer so that the next iteration's Dynamic
   Prefix Identification can combine them with whatever bytes follow. Applying
   the partial-block padding of Section 6.2 to them at this point would be
   premature: unless they also happen to be the last bytes of the entire input,
   a decoder cannot distinguish a padded partial block emitted here from the
   start of the following 5-character group, and everything decoded afterward
   would be misaligned.
 * Else (dp_candidate_prefix has fewer than 4 bytes — which, given
   MIN_PASSTHROUGH_BYTES, means the byte at the front of intermediate_buffer is
   representable under no alphabet, or under so few that no scan reached 4
   bytes): a block of size min(4, length(intermediate_buffer)) SHALL be encoded
   using ProcessWithBlockMode. The result is appended to output_string. The
   corresponding bytes SHALL be removed from intermediate_buffer.

Every branch of step 2.b removes at least 1 byte from intermediate_buffer
whenever intermediate_buffer is non-empty (at least MIN_PASSTHROUGH_BYTES in the
DP branch, at least 4 in the aligned block-mode branch, at least 1 in the final
branch); deferring the R trailing bytes therefore cannot stall the loop, since
each iteration strictly reduces the number of bytes remaining to be encoded.

The loop then repeats until intermediate_buffer is empty.

### 6.2. ProcessWithBlockMode(buffer_to_encode)

This function encodes buffer_to_encode using standard Base85N block encoding.
It processes the input in 4-byte full blocks. Each 4-byte block is treated as a
32-bit unsigned integer (Big-Endian) and converted into five Base85N characters
using Alphabet-N (see Section 8 for conversion).
If buffer_to_encode is not a multiple of 4 bytes (i.e., a partial final block of
1, 2, or 3 bytes remains), these trailing bytes are padded with zero bytes
(conceptually, to the right) to form a 4-byte block. This 4-byte block is then
converted to 5 Base85N characters. From this 5-character group, only the first
2, 3, or 4 characters are taken as the encoded output for original 1, 2, or 3
trailing bytes, respectively. The output of this function is the string of
Alphabet-N characters.

This padding/truncation behavior is only meaningful when buffer_to_encode
represents the final remaining bytes of the entire input (i.e., no further input
bytes will follow it in the stream): a decoder can only recognize a partial
final block by reaching the actual end of input (Section 7.1), not by any
in-band marker. Every other call site in this specification SHALL therefore only
pass a buffer_to_encode whose length is an exact multiple of 4 bytes, deferring
any true remainder to a later, genuinely final call; see Section 6.1, step 2.b
for how the main encoding loop upholds this.

### 6.3. Required State Information

 * Dynamic State Information: intermediate_buffer (stores bytes from the input
   stream awaiting processing).

### 6.4. Constants

 * MAX_DP_ANALYSIS_BYTES = 1024 (Maximum number of leading input bytes examined
   when identifying a Dynamic Passthrough prefix, and therefore the maximum
   number of bytes a single DP segment can carry).
 * MAX_DP_OUTPUT_CHARS_PER_SIGNAL = 1024 (Maximum character length of a DP
   segment's data. Equal to MAX_DP_ANALYSIS_BYTES because the transformation is
   1:1, and matching the 10-bit length field in the DP signal).
 * MIN_PASSTHROUGH_BYTES = 20 (Minimum length of a candidate prefix for DP
   processing to be attempted).

Version 0.2.0's MAX_CONSECUTIVE_ESCAPES no longer exists.

### 6.5. Main Encoding Logic Summary

The encoder identifies a DP-encodable prefix by scanning the front of
intermediate_buffer once per replacement alphabet, up to MAX_DP_ANALYSIS_BYTES
bytes, and taking the alphabet whose scan reaches furthest — smallest identifier
winning a tie. The resulting prefix is then globally evaluated: only if it meets
the minimum length and is at least as compact as standard block encoding is it
encoded in DP mode. Otherwise, the encoder falls back to block encoding for that
segment, guaranteeing optimal compactness.

Two properties of Section 4.2 do the work that version 0.2.0 needed extra
machinery for. Because representability under an alphabet is a property of a
single byte, the prefix is found in one forward scan rather than a Pass 1 that
discovers a mask and a Pass 2 that re-reads the window against it. And because
the alphabet is injective, one byte always becomes one character, so a prefix
bounded at 1024 bytes always fits one signal and the multi-segment splitting
rule disappears.

One rule from version 0.2.0 survives unchanged: when a candidate prefix falls
back to block encoding, only its largest exact multiple of 4 bytes is
block-encoded immediately; any 1-3 trailing bytes are left in
intermediate_buffer for the next iteration rather than padded on the spot
(Section 6.1, step 2.b), since padding a non-final remainder would be
indistinguishable from the start of the next block to a decoder and would
misalign everything that follows.

### 6.6. Encoding Complexity

This section is normative. It constrains the *cost* of producing the output, not
the output itself: an implementation that satisfies it emits exactly the same
characters as one that does not.

**Requirement.** An encoder SHALL encode an input of N bytes in time linear in
N. Specifically, the total work an encoder performs in Dynamic Prefix
Identification (Section 6.1, step 1) across the whole input SHALL be O(N); an
encoder SHALL NOT re-scan the same input byte an unbounded number of times.

**Why this needs saying.** Step 1 scans up to MAX_DP_ANALYSIS_BYTES bytes per
alphabet, and step 2.b may then consume as few as 4 bytes — so a literal
implementation performs up to 8 * 1024 byte inspections to advance 4 bytes,
which is 2048 inspections per byte of input. Unlike version 0.2.0, where the
same asymmetry produced genuinely quadratic behaviour, this is bounded and
therefore linear in N; but a constant factor of 2048 is a denial-of-service
surface in its own right, and it is reached by ordinary input rather than only
by crafted input. Any run of bytes that no alphabet can represent for 20 bytes
at a stretch — arbitrary binary data, for instance — hits it on every iteration.

**Required technique.** The property to exploit is that a scan's result at a
position inside a run is determined by the run, not by the position: if the
first byte not representable under alphabet a lies at offset e, then for every
position p < e the scan under a stops at e, and L(a) is min(e - p,
MAX_DP_ANALYSIS_BYTES). An implementation SHALL exploit this, or an equivalent
property, rather than rescanning. The reference implementations keep, for each
of the eight alphabets, the offset of the next byte not representable under it,
recompute an entry only once the position has reached or passed it, and derive
L(a) by subtraction. Each of the eight offsets therefore advances monotonically
across the whole input, so the total scanning work is O(N) with a constant
factor of 8 rather than 8 * 1024.

Any other approach with the same asymptotic bound is equally conformant; the
cached offsets are an implementation detail, the linearity is the requirement.

**Verification.** An implementation claiming conformance SHOULD be checked
against an input of N pseudorandom bytes, which drives the encoder into the
block-mode branch on nearly every iteration while keeping short representable
runs available to scan. Encoding time for such input SHALL grow linearly in N.

## 7. Decoding Algorithm

### 7.1. General Decoding Principles

A Base85N decoder SHALL process input streams expecting characters from
Alphabet-N for all Base85N constructs.
When consuming the input stream, a decoder MUST ignore: space (U+0020),
horizontal tab (U+0009), line feed (U+000A), and carriage return (U+000D)
encountered between distinct Base85N characters or DP structures. This
whitespace-ignoring rule does not alter the interpretation of characters within
DP data.

A Base85N decoder processes an input stream (after stripping inter-character
whitespace as defined above):

 * Read up to 5 Alphabet-N characters. If End Of File (EOF) or insufficient
   non-whitespace Alphabet-N characters are available to form a full 5-character
   group, and these do not constitute a valid partial final block according to
   standard Base85 rules for decoding, this may be an error or the end of data.
 * Convert the 5 input characters to their integer values (0-84) using
   ALPHABET_N_CHARS_STR. Any character not in Alphabet-N is an error. Use
   Base85DigitsToValue (Section 8) to get decodedValue.
 * If `0 <= decodedValue < 2^32`: It's a standard Base85N block. Convert
   decodedValue to 4 bytes (Big-Endian). Append these bytes to the output. (For
   partial final blocks, fewer bytes are output, see below).
 * If `decodedValue >= 2^32`: It's a Dynamic Passthrough (DP) signal.
   1. Calculate SignalPayload = decodedValue - `2^32`.
   2. Validate SignalPayload. It MUST be in the range 0 to `2^13 - 1`. If not,
      it's an error (Undefined or Reserved Signal Value).
   3. Extract AlphabetID_3bit and Length_10bit_encoded_value (L_enc) from
      SignalPayload (see Section 9). L_output_chars = L_enc + 1, which is in the
      range 1 to 1024.
   4. Read exactly L_output_chars Alphabet-N characters from the input stream
      immediately following the signal. These characters form the
      transformed_DP_data. If fewer than L_output_chars characters are
      available, it's an error (Unexpected End of Stream). Any character not in
      Alphabet-N is an error.
   5. DP Data Interpretation: let A be the replacement alphabet named by
      AlphabetID_3bit (Section 4.2). For each character c of transformed_DP_data,
      in order:
      * If c is the donor character for R-Set index j under A, append the ASCII
        value of R_Char[j] (Section 4.1) to the output.
      * Otherwise, append ord(c) to the output.

      Every character of transformed_DP_data produces exactly one output byte;
      there is no state to carry from one character to the next, and no
      character can be consumed as part of a longer construct.
 * Handle final partial blocks if any remain after all full blocks and DP
   segments are processed. If the input stream ends with 2, 3, or 4 Alphabet-N
   characters that form a partial group, these are decoded by (conceptually)
   padding them with the character representing value 84 ('#') to make a
   5-character group, converting to a 32-bit number, and then taking the first
   1, 2, or 3 bytes respectively. Any character not in Alphabet-N is an error.
   * The padded group's value MUST be less than `2^32`; a decoder MUST reject a
     trailing group whose padded value reaches `2^32` as an Invalid Final Block
     error (Section 10) rather than reducing it modulo `2^32`. Section 6.2
     truncates a group whose value is below `2^32`, and re-padding the retained
     characters with '#' raises that value by at most 84, 7224 or 614124 for a
     3-, 2- or 1-byte remainder respectively — never across `2^32`. A group that
     does cross it therefore cannot have been produced by this specification's
     encoder, and masking it would silently accept several distinct character
     sequences as encodings of the same bytes.

## 8. Value/Digit Conversion

CharToValue (converting a character from Alphabet-N to its integer value 0-84)
and ValueToChar (converting an integer value 0-84 to its Alphabet-N character)
operations exclusively use Alphabet-N as defined in Section 4.
Standard Base85 arithmetic applies for converting 4 bytes to a 32-bit unsigned
integer and then to five Base85 digits, and vice-versa, using Big-Endian byte
order.

 * Base85DigitsToValue(digits[5]): val = ((( (d0*85 + d1)*85 + d2)*85 + d3)*85 + d4)
 * ValueToBase85Digits(value, digits[5]): for i from 4 down to 0: digits[i] = value % 85; value /= 85 (integer division)

## 9. Signal Interpretation and Parameter Encoding

For a 5-character sequence (from Alphabet-N) decoded to decodedValue:

 * Standard Block: `0 <= decodedValue < 2^32`. The decodedValue directly
   represents the 32-bit unsigned integer from a 4-byte group.
 * Dynamic Passthrough (DP) Signal: `decodedValue >= 2^32`.
   The actual parameters for DP mode are encoded in SignalPayload.
   SignalPayload = decodedValue - `2^32`.
 * Total bits for DP parameters: 13. SignalPayload SHALL range from 0 to
   `2^13 - 1` = 8191.
 * Payload Encoding (13 bits total):
   SignalPayload = (AlphabetID_3bit << 10) | Length_10bit_encoded_value
   * AlphabetID_3bit (Bits 10-12 of SignalPayload, where bit 0 is LSB): the
     identifier, 0 to 7, of the replacement alphabet (Section 4.2) under which
     the following segment's data was transformed. All eight values are defined;
     none is reserved.
   * Length_10bit_encoded_value (Bits 0-9 of SignalPayload): an unsigned 10-bit
     integer (L_enc). The character length of the transformed_DP_data segment
     that immediately follows this 5-character signal is **L_enc + 1**, i.e. a
     value from 1 to 1024.

     The bias of one exists because a segment of zero characters carries no
     data and no conforming encoder emits one; spending a code point on it
     would cost the length field its ability to reach 1024, which
     MAX_DP_ANALYSIS_BYTES requires. A decoder MUST NOT interpret L_enc = 0 as
     an empty segment.
 * Reserved/Undefined:
   The maximum block encoded value is `2^32 - 1` and the maximum used for
   Base85N signals is `2^13 - 1`. So any decodedValue greater than (`2^32`) +
   `2^13 - 1` MUST be treated as an error.

## 10. Error Handling

Implementations MUST detect and report errors, including but not limited to:

 * Invalid Characters during Decoding: Any character encountered in the input
   stream (after allowed whitespace stripping) that is not part of Alphabet-N.
 * Invalid Final Block Length/Padding: Incorrectly encoded final partial block
   that does not conform to standard Base85 rules for handling 1, 2, or 3
   trailing bytes (as per Section 7.1).
 * Unexpected End of Stream: EOF reached when more characters were expected
   (e.g., in the middle of a 5-character group, during a DP signal, or when
   reading transformed_DP_data as specified by a DP signal's length field).
 * Undefined or Reserved Signal Value Encountered: A decodedValue indicating a
   DP signal whose SignalPayload (i.e., decodedValue - `2^32`) falls into the
   reserved/undefined range (i.e., is greater than `2^13 - 1`).
 * Invalid Dynamic Passthrough Signal Parameters:
   * SignalPayload outside the valid 0 to `2^13 - 1` range.
   * L_enc + 1 implies reading more transformed_DP_data characters than are
     available in the stream.
 * Invalid Partial Block Encoding / Overrun: During decoding of a partial block,
   if the decoded value implies more bytes than are supposed to be represented
   by that partial block. Concretely, per Section 7.1: a trailing group of
   exactly 1 character, or a trailing group of 2 to 4 characters whose
   '#'-padded value is not less than `2^32`.

Version 0.2.0's "Dangling Escape Character" condition no longer exists: DP data
contains no escape sequences, so no character in it can be left without its
partner.

## 11. Encoding Mode

Base85N has one standard encoding behavior which dynamically chooses between two
internal strategies as detailed in Section 6:

 * Dynamic Passthrough (DP) Mode: attempted for the leading segment of the
   current data that meets the minimum length requirement
   (MIN_PASSTHROUGH_BYTES) under at least one of the eight replacement
   alphabets. DP mode is chosen for this prefix if it is at least as efficient
   (i.e., the output is not longer than) that of Block Mode for that same
   prefix.
 * Block Mode: Standard Base85 encoding (4 bytes to 5 Alphabet-N characters,
   with handling for final partial blocks) is used if DP mode is not suitable
   for an identified prefix (i.e., not more efficient), or if no suitable prefix
   for DP processing can be identified at the current point in the input stream.
   In the latter case, a smaller segment (typically 4 bytes or less) is
   processed via block mode.

The encoder makes this choice adaptively for segments of the input data
according to the algorithm in Section 6.

## 12. Reference Implementations

This repository contains conformant library implementations of Base85N,
with test suites, in five languages:

 * [`rust/`](https://github.com/keywan-ghadami/base85n/tree/main/rust) — a Rust crate (`cargo test`)
 * [`go/`](https://github.com/keywan-ghadami/base85n/tree/main/go) — a Go module (`go test ./...`)
 * [`typescript/`](https://github.com/keywan-ghadami/base85n/tree/main/typescript) — a TypeScript/npm package (`npm test`)
 * [`c/`](https://github.com/keywan-ghadami/base85n/tree/main/c) — a C library (`make test` / CMake + CTest)
 * [`python/`](https://github.com/keywan-ghadami/base85n/tree/main/python) — a Python package (`pytest`); this is also the
   project's original reference implementation, used to generate the
   golden test vectors below

Each implementation follows Section 6.1 above exactly (the single-scan prefix
identification, the smallest-identifier tie-break, and the Block Mode fallback
rules). A shared set of golden encode/decode test vectors, generated from a
reference implementation of this algorithm, is used by every language's test
suite and lives in
[`testvectors/vectors.json`](https://github.com/keywan-ghadami/base85n/blob/main/testvectors/vectors.json) (and the
equivalent [`testvectors/vectors.tsv`](https://github.com/keywan-ghadami/base85n/blob/main/testvectors/vectors.tsv)).

A second shared set,
[`testvectors/adversarial_vectors.json`](https://github.com/keywan-ghadami/base85n/blob/main/testvectors/adversarial_vectors.json)
(and the equivalent
[`testvectors/adversarial_vectors.tsv`](https://github.com/keywan-ghadami/base85n/blob/main/testvectors/adversarial_vectors.tsv)),
targets `decode`'s robustness against untrusted input rather than plain
round-tripping. Each entry is either `"kind": "must_fail"` (decoding must
be rejected with the given `error_code`, one of the Section 10
conditions, and must never crash) or `"kind": "valid"` (a spec-legal input
no encoder following Section 6.1 would ever produce, decoding to the
given `expected_hex`). It covers these categories:

 * `unicode_position` — multi-byte Unicode characters (emoji, a combining
   mark, an astral-plane character requiring a UTF-16 surrogate pair)
   placed where a mismatch between "character position" and a language's
   actual storage/encoding unit (UTF-8 byte, UTF-16 code unit, Unicode
   codepoint) could misparse, misindex, or crash instead of cleanly
   rejecting the input.
 * `invalid_signal` — reserved/out-of-range signal payloads alongside the
   adjacent still-valid boundary case, and signals declaring more data
   than remains in the stream.
 * `alphabet_selection` — every one of the eight alphabet identifiers decoded
   over the same segment data, so that a decoder that ignores the identifier,
   truncates it, or confuses two alphabets sharing a donor character is caught.
 * `partial_block` — trailing groups of 1 to 4 characters, including the
   '#'-padded values immediately below and at `2^32`.

## 13. Security Considerations

Base85N is an encoding, not a cryptographic transform. It provides no
confidentiality, no integrity protection, and no authentication. Encoded text
is trivially reversible by anyone.

The largest security-relevant surface of this specification is the *decoder*,
because a decoder is by nature fed data that the receiving system did not
produce. The *encoder* is a smaller but real surface, because encoders are
routinely fed text that the encoding system did not author. The following
properties are normative for implementations:

 * An encoder MUST meet the linear-time bound of Section 6.6. The bounded
   lookahead of MAX_DP_ANALYSIS_BYTES means a non-conforming encoder is slow by
   a large constant factor rather than quadratic as in version 0.2.0, but a
   factor of 2048 reached by ordinary binary input is still a denial-of-service
   surface, and the condition MUST NOT be treated as adversarial-only.
 * A decoder MUST treat its input as untrusted. Every error condition in
   Section 10 MUST be detected and reported to the caller; an implementation
   MUST NOT read outside its input buffer, index past the declared end of a DP
   segment, or terminate the process on malformed input.
 * A DP signal's length field is attacker-controlled. It MUST be validated
   against the number of characters actually remaining in the stream (Section
   10, "Invalid Dynamic Passthrough Signal Parameters") before those characters
   are read, and MUST NOT be used to size a read or a copy without that check.
   Note that the bias of one (Section 9) means the smallest declared length is 1
   and the largest is 1024; an implementation that reads L_enc without adding
   one will under-read every segment.
 * A DP signal's alphabet identifier is attacker-controlled, but every one of
   its eight values is defined, so it cannot itself select an out-of-range
   table. An implementation MUST still mask it to 3 bits rather than assume the
   payload was well-formed.
 * Output length is attacker-influenced. Because DP mode is exactly 1:1,
   decoded output is at most slightly smaller than the input; there is no
   decompression amplification in Base85N, but an implementation that allocates
   on the basis of a declared length rather than of available input can still be
   made to over-allocate. Implementations SHOULD bound the total input size they
   accept from untrusted peers.
 * Decoded output is arbitrary binary data, including NUL bytes, control
   characters, and byte sequences that are not valid UTF-8. Callers MUST NOT
   assume decoded output is printable, NUL-terminated, or text.
 * Base85N's alphabet was chosen for protocol friendliness, and DP mode can
   make encoded output partially resemble the plaintext — more so in 0.3.0 than
   in 0.2.0, since escape characters no longer interrupt it. Neither property
   makes encoded output safe to interpolate into HTML, SQL, shell commands, or
   any other context that requires escaping. Context-appropriate escaping is
   still the caller's responsibility, both before encoding and after decoding.
 * Base85N encoding is deterministic and its DP/Block decision is
   data-dependent, so the *length* and *structure* of the output leak
   information about the input. Version 0.3.0 leaks slightly more than 0.2.0:
   a segment's alphabet identifier is visible in the signal and narrows what
   kind of text the segment holds. Implementations MUST NOT rely on Base85N to
   hide such properties, and MUST NOT use it as any part of a constant-time
   path.

Operational guidance, the current state of testing, and known gaps are tracked
in
[SECURITY.md](https://github.com/keywan-ghadami/base85n/blob/main/SECURITY.md).

## 14. Changes from Version 0.2.0

This section is informative.

Version 0.3.0 changes the wire format. Output produced under 0.2.0 does not
decode correctly under 0.3.0, and the shared test vectors are regenerated
rather than extended. The draft notice at the top of this document, and of
0.2.0, reserves the right to do this while the specification is at 0.x.

**What changed**

 * *Dynamic Passthrough is escape-free.* 0.2.0 activated individual R-Set
   characters through a 13-bit mask and escaped, with a `~` prefix, any literal
   occurrence of a replacement character whose bit was set. 0.3.0 instead
   selects one of eight fixed replacement alphabets (Section 4.2), each
   injective, so a literal that would have needed escaping is simply not
   representable in that segment and the segment ends before it.
 * *The signal shrank from 22 payload bits to 13* (Section 9): a 3-bit alphabet
   identifier and a 10-bit length, against 0.2.0's 13-bit mask and 9-bit length.
   Any payload above `2^13 - 1` is now reserved.
 * *Segments grew from at most 511 characters to at most 1024*, and the length
   field is stored biased by one so that 10 bits reach 1024.
 * *A new constant, MAX_DP_ANALYSIS_BYTES = 1024*, bounds how much input the
   encoder examines per DP decision (Section 6.4). In 0.2.0 such a bound was an
   optional implementation choice; making it normative is what fixes a segment
   at one signal.
 * *MAX_CONSECUTIVE_ESCAPES is gone*, along with the escape character's special
   role. `~` is now an ordinary Alphabet-N character (Section 4.3).
 * *Prefix identification is a single forward scan per alphabet*, replacing
   0.2.0's Pass 1 / Pass 2 procedure (Section 6.1). Two passes existed because
   an escaping decision depended on a mask that was still being discovered;
   with a fixed alphabet there is no such dependency.
 * *DP Output Segmentation is gone.* 0.2.0 split a candidate prefix longer than
   511 transformed characters across several signals and had to avoid splitting
   a two-character escape pair. A 0.3.0 candidate is at most 1024 bytes and
   1024 characters, so it is always one segment.
 * *A normative tie-break was added* (Section 6.1, step 1): when several
   alphabets reach equally far, the smallest identifier wins. 0.2.0 needed no
   such rule because it had nothing to choose between.
 * *The "Dangling Escape Character" error is gone* (Section 10).
 * *The partial-final-block bound is stated normatively* (Section 7.1): a
   trailing group whose '#'-padded value reaches `2^32` is rejected rather than
   reduced modulo `2^32`. This was added to 0.2.0 after its initial publication
   and is carried here unchanged.

**What did not change**

The Alphabet-N table (Section 4), the R-Set characters and their indices
(Section 4.1), Big-Endian ordering (Section 5), block mode including its
partial-final-block handling (Section 6.2), base-85 digit conversion (Section
8), MIN_PASSTHROUGH_BYTES, and the boundary between block values and signal
values at `2^32`.

**Effect on size**

Measured over a corpus of JSON, Markdown, CSS, tab-separated data and source
code in five languages, and expressed as encoded characters per input byte:
`countries.json` moves from 1.033 to about 1.005, its minified form from 1.053
to about 1.005, and the CommonMark specification from 1.123 to about 1.020.
Binary input is unchanged at the block-mode ratio of 1.25. See
[`bench/results/`](https://github.com/keywan-ghadami/base85n/tree/main/bench/results)
for the measured numbers.
