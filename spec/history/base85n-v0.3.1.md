# Base85N: A Protocol-Friendly Binary-to-Text Encoding

| Field | Value |
|---|---|
| Version | 0.3.1 |
| Status | Draft |
| Date | 2026-08-13 |
| Editor | Keywan Ghadami |
| License | MPL-2.0 |
| Canonical URI | https://github.com/keywan-ghadami/base85n/blob/main/spec/base85n-v0.3.1.md |

> **Draft status.** This is a 0.x draft specification. It is implemented and tested (see Section 12), but it has not been through independent review and the wire format is not yet frozen. Anything in this document MAY change in a subsequent 0.x version. Do not use Base85N for data that must remain decodable by future versions without a migration path.
> 
> **AI-assisted authorship.** Large parts of this document, and all reference implementations, were produced with AI assistance. See the repository README and SECURITY.md before relying on it.

## 1. Abstract
This document defines Base85N, a binary-to-text encoding scheme using a single 85-character alphabet (Alphabet-N) selected for broad compatibility and a protocol-friendly character repertoire. It features a 4-byte-to-5-character core conversion mechanism. An enhanced Dynamic Passthrough (DP) mode enables efficient, partially human-readable representation of compatible byte sequences using only Alphabet-N characters.

DP operates by selecting, per segment, one of eight fixed replacement alphabets: each is Alphabet-N with a small number of its rarest characters given up so that frequently occurring characters outside Alphabet-N — space, newline, quotes, and other common symbols — can be carried directly. Because each replacement alphabet is injective, DP needs no escape mechanism and achieves exactly 1:1 efficiency on its data, plus a 5-character signal per segment. A fallback to standard Base85N block encoding is used when DP would be less efficient, or if the original data contains bytes that no replacement alphabet can represent.

The scheme supports padding-free encoding and decoding.

## 2. Introduction
Binary data, such as cryptographic keys, identifiers, or media files, often needs representation as text. Common formats like JSON, XML, and HTML often impose character set restrictions. Base64 is common but inefficient (approx. 33% overhead). Base85 variants are denser. This specification defines Base85N, a Base85 variant aiming for high efficiency combined with a broadly compatible, single alphabet (Alphabet-N).

It includes a Dynamic Passthrough (DP) mechanism intended to represent byte sequences that consist largely of printable text. The difficulty such a mechanism has to solve is that the useful characters do not fit: Alphabet-N has 85 characters, and the common characters that ordinary text is full of — space above all — are not among them. Adding all 13 required characters would require 98 distinct output characters, which 85 cannot carry.

Base85N resolves this by trading. A replacement alphabet gives up k of Alphabet-N's rarest characters and uses the freed positions to carry k external characters instead. The result is still a set of 85 output characters, and the mapping from representable input byte to output character is still injective — so no escape character is needed to tell a substituted character from a literal one. What a segment pays instead is that its k given-up characters are simply not representable in that segment; encountering one ends the segment. Eight such alphabets are defined, and the encoder picks one per segment, so the trade can follow the shape of the data. DP commitment requires a minimum input data length (MIN_PASSTHROUGH_BYTES).

### 2.1. Key Features and Rationale
* **High Data Density:** Core 4-byte to 5-character Base85N conversion offers better density than Base64.
* **Protocol-Friendly Character Repertoire:** Base85N exclusively uses Alphabet-N, based on (but distinct from) z85 with minor modifications. It is designed to avoid characters that require frequent escaping in generic string contexts, though context-specific escaping (e.g., HTML entities) remains the caller's responsibility.
* **Dynamic Passthrough (DP):** Attempts to directly represent sequences of bytes. DP mode exclusively emits Alphabet-N characters. Each DP segment names one of the eight replacement alphabets of Section 4.2, and its data is that alphabet applied byte by byte.
* **Escape-Free:** Because a replacement alphabet is injective, DP data contains no escape sequences. One input byte always produces exactly one output character, so a DP segment's character length equals its byte length.
* **Adaptive Block Mode Fallback Strategy:** The encoding algorithm (Section 6) processes data in segments, scanning for a prefix that some replacement alphabet can represent.
  * If such a prefix is found, its DP-encoded length is compared to its standard block-encoded length. DP mode is chosen for this prefix only if it is shorter or equal.
  * If DP mode is not chosen, the encoder falls back to standard Base85N block processing for a single 4-byte group (or fewer, at stream end) and identifies a prefix again from the next group.
* **Bounded Lookahead:** The encoder analyses at most MAX_DP_ANALYSIS_BYTES (1024) bytes when identifying a DP prefix, so a DP segment carries at most 1024 bytes and its signal's length field never has to describe more.
* **Padding-Free Canonical Encoding:** Handles input of any length using standard Base85N partial block encoding, enforcing a single canonical representation for truncated trailing blocks.

## 3. Conventions and Terminology
The key words "MUST", "MUST NOT", "REQUIRED", "SHALL", "SHALL NOT", "SHOULD", "SHOULD NOT", "RECOMMENDED", "NOT RECOMMENDED", "MAY", and "OPTIONAL" in this document are to be interpreted as described in BCP 14 [RFC2119] [RFC8174] when, and only when, they appear in all capitals, as shown here.

## 4. Alphabet (Alphabet-N)
Base85N uses a single 85-character alphabet, referred to as Alphabet-N. Each character is assigned a unique integer value from 0 to 84.

The character assignments for Alphabet-N, corresponding to their integer values (indices), are presented below:

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

**String Representation for Implementations:**
`ALPHABET_N_CHARS_STR = '0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ.-:+=^!/*?\`_~()[]{}@%$#'`

### 4.1. R-Set Characters (Alphabet_R)
The R-Set defines a standardized set of 13 characters that are candidates for substitution in Dynamic Passthrough (DP) mode. These are characters that occur frequently in real text but are not part of Alphabet-N. The R-Set characters are assigned fixed indices 0-12; a replacement alphabet (Section 4.2) refers to them by these indices.

| Index (j) | R_Char (Conceptual) | ASCII Value | Notes |
|---|---|---|---|
| 0 | (space) | 32 | Space |
| 1 | `"` | 34 | Double quote |
| 2 | `'` | 39 | Single quote |
| 3 | `,` | 44 | Comma |
| 4 | `;` | 59 | Semicolon |
| 5 | `\` | 92 | Backslash |
| 6 | `\|` | 124 | Pipe symbol |
| 7 | `<` | 60 | Less-than symbol |
| 8 | `>` | 62 | Greater-than symbol |
| 9 | `&` | 38 | Ampersand |
| 10 | `\t` (tab) | 9 | Horizontal Tab |
| 11 | `\n` (newline) | 10 | Line Feed |
| 12 | `\r` (car. ret) | 13 | Carriage Return |

### 4.2. Replacement Alphabets
Eight replacement alphabets are defined, identified by the values 0 through 7. A DP segment's signal (Section 9) names exactly one of them, and that alphabet governs the whole segment.

A replacement alphabet is a set of substitutions. Each substitution names an R-Set index j (Section 4.1) and a donor character, which is an Alphabet-N character. Within a segment encoded under that alphabet:

* An input byte equal to R_Char[j] SHALL be written as the donor character;
* An input byte equal to the donor character is not representable under that alphabet, and therefore cannot appear in that segment at all;
* Every other Alphabet-N character represents itself;
* Every other byte is not representable.

No donor character appears twice within one alphabet, and no R-Set index appears twice, so the mapping from representable input byte to output character is injective. This property makes an escape character unnecessary: a donor character in DP data has exactly one meaning, fixed by the alphabet identifier in the signal.

The eight alphabets:

| ID | Name | Substitutions (R_Char -> donor character) |
|---|---|---|
| 0 | none | *(no substitutions; all 85 Alphabet-N characters represent themselves)* |
| 1 | text | space->`^` LF->`@` CR->`%` TAB->`$` |
| 2 | prose | space->`^` LF->`@` `,`->`%` `"`->`$` `'`->`?` `;`->`!` |
| 3 | markup | space->`^` LF->`@` `<`->`%` `>`->`$` `&`->`?` `"`->`!` `'`->`~` `,`->`{` |
| 4 | json | space->`^` LF->`@` `"`->`%` `,`->`$` `\`->`?` CR->`!` |
| 5 | code | space->`^` LF->`@` `,`->`%` `;`->`$` `"`->`?` `'`->`!` TAB->`~` `>`->`\`` |
| 6 | shell | space->`^` LF->`@` `\|`->`%` `\`->`$` `"`->`?` `'`->`!` `&`->`~` `;`->`#` |
| 7 | full | space->`^` LF->`@` CR->`%` TAB->`$` `,`->`?` `;`->`!` `"`->`~` `'`->`#` `<`->`*` `>`->`+` `&`->`=` `\|`->`_` `\`->`\`` |

### 4.3. Escape Character
Base85N has no escape character. The character `~` (tilde, at index 74 of Alphabet-N) is an ordinary Alphabet-N character: it represents itself in DP data under every alphabet that does not use it as a donor, and it is a donor under alphabets 3, 5 and 6.

## 5. Endianness
All conversions between multi-byte integers and byte sequences MUST use Big-Endian byte order.

## 6. Encoding Algorithm
The encoding algorithm processes an input byte stream, conceptually accumulating data into an `intermediate_buffer`. It exclusively uses Alphabet-N. The algorithm adaptively chooses between Dynamic Passthrough (DP) mode and standard Block mode for segments of the input data.

### 6.1. Main Encoding Loop
Initialize `output_string = ""`.

Input data is conceptually accumulated into `intermediate_buffer`. An encoder operating on a stream MUST buffer sufficient lookahead—up to MAX_DP_ANALYSIS_BYTES bytes—to determine the longest representable prefix, or determine that the end of the input has been reached, before making a processing decision.

The loop continues as long as `intermediate_buffer` is not empty. Inside the loop, the following sequence of steps SHALL be performed:

**1. Dynamic Prefix Identification**
A byte B is said to be representable under alphabet a when either:
* B equals R_Char[j] for some R-Set index j that alphabet a substitutes, or
* chr(B) is in ALPHABET_N_CHARS_STR and chr(B) is not a donor character of alphabet a.

Every other byte is not representable under a. Representability depends only on B and a.

For each alphabet identifier a from 0 to 7, the encoder SHALL determine L(a): the number of leading bytes of `intermediate_buffer` that are representable under a, bounded above by MAX_DP_ANALYSIS_BYTES. The scan for a stops at the first byte not representable under a, or after MAX_DP_ANALYSIS_BYTES bytes, whichever comes first.

`dp_alphabet` SHALL be the identifier a for which L(a) is greatest. If several alphabets achieve the same greatest L(a), the numerically smallest such identifier SHALL be chosen. This tie-break is normative.

`dp_candidate_prefix` SHALL be the first L(dp_alphabet) bytes of `intermediate_buffer`, and `L_transformed` SHALL be L(dp_alphabet).

An implementation MAY compute the eight scans in any order or interleaved, and MAY stop a scan early once it cannot beat the best L(a) found so far, provided the chosen `dp_alphabet` and `dp_candidate_prefix` are those specified above.

**2. Decision and Processing**

**a. DP Suitability Check:** 
A boolean flag `use_dp_mode` is initialized to false. The encoder SHALL check if both of the following conditions are met:
* The length of `dp_candidate_prefix` is >= MIN_PASSTHROUGH_BYTES.
* The calculated `conceptual_dp_output_length` is less than or equal to the `block_mode_output_length` for the `dp_candidate_prefix`, where:
  * `conceptual_dp_output_length = 5 + L_transformed` (one 5-character signal followed by the segment's characters);
  * `block_mode_output_length = ceil(length(dp_candidate_prefix) / 4) * 5`.

If both conditions are true, `use_dp_mode` SHALL be set to true.

**b. Processing Execution:**
If `use_dp_mode` is true:
* A 5-character signal carrying `dp_alphabet` and the exact character length of the segment (Section 9) SHALL be emitted, immediately followed by the `L_transformed` transformed characters: for each byte of `dp_candidate_prefix` in order, the donor character if the byte is a substituted R_Char under `dp_alphabet`, otherwise the byte's own Alphabet-N character.
* The resulting string SHALL be appended to `output_string`.
* The bytes corresponding to `dp_candidate_prefix` SHALL be removed from `intermediate_buffer`.

Because MAX_DP_ANALYSIS_BYTES bounds `dp_candidate_prefix` at 1024 bytes and the transformation is 1:1, a candidate prefix is always carried by exactly one signal.

If `use_dp_mode` is false:
A block of exactly min(4, length(`intermediate_buffer`)) bytes SHALL be encoded using ProcessWithBlockMode (see Section 6.2). The result is appended to `output_string`, and those bytes SHALL be removed from `intermediate_buffer`.

Note that this consumes one 4-byte group and no more, regardless of how long `dp_candidate_prefix` was. A candidate that failed the suitability check is not block-encoded as a unit: only its first group is, and the next iteration runs Dynamic Prefix Identification again from four bytes further on.

Every branch of step 2.b removes at least 1 byte from `intermediate_buffer` whenever it is non-empty, strictly reducing the number of bytes remaining.
The loop repeats until `intermediate_buffer` is empty.

### 6.2. ProcessWithBlockMode(buffer_to_encode)
This function encodes `buffer_to_encode` using standard Base85N block encoding. It processes the input in 4-byte full blocks. Each 4-byte block is treated as a 32-bit unsigned integer (Big-Endian) and converted into five Base85N characters using Alphabet-N (Section 8).

If `buffer_to_encode` is not a multiple of 4 bytes (i.e., a partial final block of 1, 2, or 3 bytes remains), these trailing bytes are padded with zero bytes (conceptually, to the right) to form a 4-byte block. This 4-byte block is then converted to 5 Base85N characters. From this 5-character group, only the first 2, 3, or 4 characters are taken as the encoded output for original 1, 2, or 3 trailing bytes, respectively.

This padding/truncation behavior is only meaningful when `buffer_to_encode` represents the final remaining bytes of the entire input. A decoder can only recognize a partial final block by reaching the actual end of input. Every other call site in this specification SHALL therefore only pass a `buffer_to_encode` whose length is an exact multiple of 4 bytes.

### 6.3. Required State Information
* **Dynamic State Information:** `intermediate_buffer` (stores bytes from the input stream awaiting processing).

### 6.4. Constants
* `MAX_DP_ANALYSIS_BYTES = 1024`: Maximum number of leading input bytes examined when identifying a Dynamic Passthrough prefix.
* `MAX_DP_OUTPUT_CHARS_PER_SIGNAL = 1024`: Maximum character length of a DP segment's data.
* `MIN_PASSTHROUGH_BYTES = 20`: Minimum length of a candidate prefix for DP processing to be attempted.

### 6.5. Main Encoding Logic Summary
The encoder identifies a DP-encodable prefix by scanning the front of `intermediate_buffer` once per replacement alphabet, up to MAX_DP_ANALYSIS_BYTES bytes, and taking the alphabet whose scan reaches furthest — smallest identifier winning a tie. The resulting prefix is then checked against the minimum length and efficiency conditions. If it meets both, it is encoded in DP mode. Otherwise, the encoder falls back to block encoding for a single 4-byte segment, guaranteeing optimal compactness before evaluating again.

### 6.6. Encoding Complexity
This section is normative. It constrains the computational cost of producing the output, not the output itself: an implementation that satisfies it emits exactly the same characters as one that does not.

**Requirement.** An encoder SHALL perform only O(N) total input-byte inspections over the entire encoding operation for an input of N bytes. An implementation SHALL NOT repeatedly inspect the same input byte an unbounded number of times.

The fixed constants in this specification do not change this requirement: MAX_DP_ANALYSIS_BYTES and the number of replacement alphabets are bounded constants. An implementation MAY use any algorithm that satisfies the requirement, provided that it produces exactly the output specified by Section 6.1.

**Rationale.** A straightforward implementation can perform up to eight bounded scans of up to MAX_DP_ANALYSIS_BYTES bytes during each Dynamic Prefix Identification step. Because a failed DP candidate may consume only four input bytes, this can result in a large constant amount of repeated work per input byte. Although such an implementation remains mathematically linear in N, the constant factor can be significant for ordinary binary input. Implementations SHOULD therefore avoid unnecessary repeated scanning of input data.

**Verification.** An implementation claiming conformance SHOULD be tested with inputs of increasing size N, including pseudorandom binary data that causes frequent Block Mode processing. The measured encoding time SHOULD exhibit linear growth as N increases.

**Implementation Note (Informative).**
The following techniques may be useful for achieving O(N) performance with a small constant overhead. They are implementation choices and are not required for conformance.
* **Single-Pass State Tracking:** Instead of performing eight independent scans, an implementation can execute a single forward pass over the data. By maintaining a bitmask of currently viable replacement alphabets, the encoder can eliminate an alphabet as soon as it encounters a byte that is not representable under that alphabet. The surviving alphabets after each position are exactly those that can represent the prefix ending at that position. This allows the longest representable prefix and its numerically-smallest qualifying alphabet to be determined without eight independent scans.
* **Batch Block Encoding:** An implementation MAY batch consecutive Block Mode groups when it has independently established that the specified encoding algorithm will select Block Mode for each group in that range. Because Block Mode is stateless and each full group is encoded independently, such batching does not change the resulting output.

## 7. Decoding Algorithm

### 7.1. General Decoding Principles
A Base85N decoder SHALL process input streams expecting characters from Alphabet-N for all Base85N constructs.

When consuming the input stream, a decoder MUST ignore: space (U+0020), horizontal tab (U+0009), line feed (U+000A), and carriage return (U+000D) encountered between distinct Base85N characters or DP structures. This whitespace-ignoring rule does not alter the interpretation of characters within DP data.

The decoder processes the input as a sequence of Base85N constructs. A construct is either a complete 5-character Block Mode group, a 5-character DP signal followed immediately by its declared DP data segment, or a partial final Block Mode group (permitted only at the end of the input stream).

* Read up to 5 Alphabet-N characters.
  * If End Of File (EOF) is encountered after a complete Base85N construct, decoding terminates successfully.
  * If EOF is encountered while a complete 5-character block, DP signal payload, or DP data segment is still required, the decoder MUST report an Unexpected End of Stream error.
* Convert the 5 input characters to their integer values (0-84) using ALPHABET_N_CHARS_STR. Any character not in Alphabet-N is an error. Use Base85DigitsToValue (Section 8) to get `decodedValue`.
* If `0 <= decodedValue < 2^32`: It's a standard Base85N block. Convert `decodedValue` to 4 bytes (Big-Endian). Append these bytes to the output.
* If `decodedValue >= 2^32`: It's a Dynamic Passthrough (DP) signal.
  * Calculate `SignalPayload = decodedValue - 2^32`.
  * Validate `SignalPayload`. It MUST be in the range 0 to 2^13 - 1. If not, it is an error (Undefined or Reserved Signal Value).
  * Extract `AlphabetID_3bit` and `Length_10bit_encoded_value` (`L_enc`) from `SignalPayload` (Section 9). `L_output_chars = L_enc + 1`, which is in the range 1 to 1024.
  * Read exactly `L_output_chars` Alphabet-N characters from the input stream immediately following the signal. These characters form the `transformed_DP_data`. If fewer than `L_output_chars` characters are available, it is an error (Unexpected End of Stream). Any character not in Alphabet-N is an error.
  * DP Data Interpretation: let A be the replacement alphabet named by `AlphabetID_3bit` (Section 4.2). For each character c of `transformed_DP_data`, in order:
    * If c is the donor character for R-Set index j under A, append the ASCII value of R_Char[j] (Section 4.1) to the output.
    * Otherwise, append ord(c) to the output.
* Handle final partial blocks if any remain after all full blocks and DP segments are processed. If the input stream ends with 2, 3, or 4 Alphabet-N characters that form a partial group, these are decoded by conceptually padding them with the character representing value 84 ('#') to make a 5-character group, converting to a 32-bit number, and then taking the first 1, 2, or 3 bytes respectively. Any character not in Alphabet-N is an error.
  * Canonical Enforcement: For a partial final block, the decoder MUST verify that the supplied characters are exactly the canonical prefix produced by encoding the resulting byte sequence padded with zero bytes to four bytes. If the characters do not match this canonical encoding, the decoder MUST reject it as an Invalid Final Block error.

## 8. Value/Digit Conversion
CharToValue (converting a character from Alphabet-N to its integer value 0-84) and ValueToChar (converting an integer value 0-84 to its Alphabet-N character) operations exclusively use Alphabet-N. Standard Base85 arithmetic applies for converting 4 bytes to a 32-bit unsigned integer and then to five Base85 digits, using Big-Endian byte order.

* `Base85DigitsToValue(digits[5])`: `val = ((( (d0*85 + d1)*85 + d2)*85 + d3)*85 + d4)`
* `ValueToBase85Digits(value, digits[5])`: `for i from 4 down to 0: digits[i] = value % 85; value /= 85` (integer division)

## 9. Signal Interpretation and Parameter Encoding
For a 5-character sequence (from Alphabet-N) decoded to `decodedValue`:

| Value Range | Interpretation |
|---|---|
| 0 to `2^32 - 1` | Standard 4-byte Block |
| `2^32` to `2^32 + 8191` | Dynamic Passthrough (DP) Signal |
| `2^32 + 8192` to `85^5 - 1` | Invalid / Reserved |

* **Standard Block:** `0 <= decodedValue < 2^32`. The `decodedValue` directly represents the 32-bit unsigned integer from a 4-byte group.
* **Dynamic Passthrough (DP) Signal:** `decodedValue >= 2^32`.
  The parameters for DP mode are encoded in `SignalPayload`.
  `SignalPayload = decodedValue - 2^32`.
* **Total bits for DP parameters:** 13. `SignalPayload` SHALL range from 0 to `2^13 - 1` = 8191.
* **Payload Encoding (13 bits total):**
  `SignalPayload = (AlphabetID_3bit << 10) | Length_10bit_encoded_value`
  * `AlphabetID_3bit` (Bits 10-12 of `SignalPayload`, where bit 0 is LSB): the identifier, 0 to 7, of the replacement alphabet (Section 4.2). All eight values are defined; none is reserved.
  * `Length_10bit_encoded_value` (Bits 0-9 of `SignalPayload`): an unsigned 10-bit integer (`L_enc`). The character length of the `transformed_DP_data` segment that immediately follows this signal is `L_enc + 1`, i.e. a value from 1 to 1024. A decoder MUST NOT interpret `L_enc = 0` as an empty segment.
* **Reserved/Undefined:** The maximum block encoded value is `2^32 - 1` and the maximum used for Base85N signals is `2^13 - 1`. So any `decodedValue` greater than `(2^32) + 2^13 - 1` MUST be treated as an error.

## 10. Error Handling
Implementations MUST detect and report errors, including but not limited to:
* **Invalid Characters during Decoding:** Any character encountered in the input stream (after allowed whitespace stripping) that is not part of Alphabet-N.
* **Unexpected End of Stream:** EOF reached while a complete 5-character block, DP signal payload, or DP data segment is still required.
* **Undefined or Reserved Signal Value Encountered:** A `decodedValue` indicating a DP signal whose `SignalPayload` falls into the reserved/undefined range.
* **Invalid Dynamic Passthrough Signal Parameters:**
  * `SignalPayload` outside the valid 0 to `2^13 - 1` range.
  * `L_enc + 1` implies reading more `transformed_DP_data` characters than are available in the stream.
* **Invalid Final Block Encoding / Overrun:**
  * A trailing group of exactly 1 character.
  * A trailing group of 2 to 4 characters whose '#'-padded value is not less than `2^32`.
  * A trailing group that does not perfectly match the canonical zero-padded encoding (as per Section 7.1).

## 11. Encoding Mode
Base85N dynamically chooses between two internal strategies (Section 6):
* **Dynamic Passthrough (DP) Mode:** The encoder identifies the longest representable prefix under the eight replacement alphabets, subject to MAX_DP_ANALYSIS_BYTES. DP mode is selected if that prefix has at least MIN_PASSTHROUGH_BYTES bytes and its encoded conceptual length is no greater than the corresponding Block Mode length.
* **Block Mode:** Standard Base85 encoding is used if DP mode is not suitable for an identified prefix, or if no suitable prefix for DP processing can be identified at the current point in the input stream. Either way exactly one 4-byte group (or fewer, at stream end) is processed via block mode before the choice is made again.

## 12. Reference Implementations
This repository contains conformant library implementations of Base85N, with test suites, in five languages:
* `rust/` — a Rust crate (`cargo test`)
* `go/` — a Go module (`go test ./...`)
* `typescript/` — a TypeScript/npm package (`npm test`)
* `c/` — a C library (`make test` / CMake + CTest)
* `python/` — a Python package (`pytest`)

Each implementation follows Section 6.1 above exactly. A shared set of golden encode/decode test vectors is used by every language's test suite and lives in `testvectors/vectors.json`.

A second shared set, `testvectors/adversarial_vectors.json`, targets decode's robustness against untrusted input. It covers categories like `unicode_position`, `invalid_signal`, `alphabet_selection`, and `partial_block`.

The test suite enforces programmatic bijectivity and collision checks for all eight replacement alphabets, asserting that no R-Set character appears twice, no donor character acts as a literal, and `Encode(x) -> Decode(x)` resolves perfectly across all 8 × 256 mapping combinations and arbitrary segment boundaries.

## 13. Security Considerations
Base85N is an encoding, not a cryptographic transform. It provides no confidentiality, no integrity protection, and no authentication. Encoded text is trivially reversible by anyone.

The largest security-relevant surface of this specification is the decoder, because a decoder is by nature fed data that the receiving system did not produce. The following properties are normative for implementations:
* An encoder MUST meet the linear-time bound of Section 6.6.
* A decoder MUST treat its input as untrusted. Every error condition in Section 10 MUST be detected and reported to the caller; an implementation MUST NOT read outside its input buffer, index past the declared end of a DP segment, or terminate the process on malformed input.
* A DP signal's length field is attacker-controlled. It MUST be validated against the number of characters actually remaining in the stream before those characters are read, and MUST NOT be used to size a read or a copy without that check.
* A DP signal's alphabet identifier is attacker-controlled, but every one of its eight values is defined. An implementation MUST still mask it to 3 bits rather than assume the payload was well-formed.
* Output length is attacker-influenced. Decoded output is at most slightly smaller than the input; there is no decompression amplification in Base85N, but an implementation that allocates on the basis of a declared length can still be made to over-allocate. Implementations SHOULD bound the total input size they accept from untrusted peers.
* Decoded output is arbitrary binary data, including NUL bytes, control characters, and byte sequences that are not valid UTF-8. Callers MUST NOT assume decoded output is printable, NUL-terminated, or text.
* Base85N encoding is deterministic and its DP/Block decision is data-dependent, so the length and structure of the output leak information about the input. A segment's alphabet identifier is visible in the signal and narrows what kind of text the segment holds. Implementations MUST NOT rely on Base85N to hide such properties, and MUST NOT use it as any part of a constant-time path.
