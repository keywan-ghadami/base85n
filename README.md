Base85N - Draft - May 11, 2025
1. Abstract
This document defines Base85N, a binary-to-text encoding scheme using a single 85-character alphabet (Alphabet-N) selected for broad compatibility and protocol friendliness. It features a 4-byte-to-5-character core conversion mechanism. An enhanced Dynamic Passthrough (DP) mode allows for more efficient and readable representation of byte sequences that are largely compatible with Alphabet-N but may contain certain predefined R-Set characters. An enhanced Dynamic Passthrough (DP) mode enables efficient, partially human-readable representation of compatible byte sequences using only Alphabet-N characters. It operates through selective substitution and escaping, achieving near 1:1 efficiency in favorable cases. A fallback to standard Base85N block encoding is used when DP is not more efficient, or if the original data contains bytes that cannot be represented as literals within Alphabet-N. The scheme supports padding-free encoding and decoding.
2. Introduction
Binary data, such as cryptographic keys, identifiers, or media files, often needs representation as text. Common formats like JSON, XML, HTML, etc., often impose character set restrictions. Base64 is common but inefficient (approx. 33% overhead). Base85 variants are denser. This specification defines Base85N, a Base85 variant aiming for high efficiency combined with a broadly compatible, single alphabet (Alphabet-N). It includes a Dynamic Passthrough (DP) mechanism intended to represent byte sequences containing predefined R-Set characters by replacing them with Alphabet-N characters if those R-Set characters are individually activated. This can improve efficiency and readability for suitable data compared to pure Base85N block encoding. DP commitment requires a minimum input data length (MIN_PASSTHROUGH_BYTES).
2.1. Key Features and Rationale
 * High Data Density: Core 4-byte to 5-character Base85N conversion offers better density than Base64.
 * Protocol-Friendly Alphabet (Alphabet-N): Base85N exclusively uses Alphabet-N, based on (but distinct from) z85 with minor modifications, which aims for broad compatibility (including HTML, XML, JSON contexts).
 * Dynamic Passthrough (DP) using Alphabet-N: Attempts to directly represent sequences of bytes. DP mode exclusively uses Alphabet-N rules, its escape character (~), and its character set for literal representation. If R-Set characters are present and activated, they are replaced by "passthrough-safe" special characters derived from Alphabet-N. DP encoded data follows a signal that specifies its exact character length (0-511). Long DP-suitable sequences can be transmitted as multiple consecutive DP-signaled segments.
 * Adaptive Block Mode Fallback Strategy: The encoding algorithm (detailed in Section 6) identifies the longest possible prefix of the current input data that is suitable for DP encoding (meets minimum length and character representability requirements).
   * If such a prefix is found, its DP-encoded length is compared to its standard block-encoded length. DP mode is chosen for this prefix only if it is strictly shorter.
   * If DP mode is not chosen for this prefix (either because it's not strictly shorter, or no suitable prefix meeting minimum length and representability was found), the encoder falls back to standard Base85N block processing. If a DP-suitable prefix was identified but found inefficient for DP, that entire prefix is block-encoded. Otherwise (no suitable DP prefix found), a smaller, standard-sized block (typically 4 bytes, or fewer at stream end) is block-encoded.
 * Padding-Free: Handles input of any length using standard Base85N partial block encoding in Block mode.
 * Fixed Escape Character: ~ (tilde, from Alphabet-N) is the exclusive escape character for Base85N, used in Dynamic Passthrough mode.
3. Conformance Requirements
The keywords "MUST", "MUST NOT", "REQUIRED", "SHALL", "SHALL NOT", "SHOULD", "SHOULD NOT", "RECOMMENDED", "MAY", and "OPTIONAL" are to be interpreted as described in RFC 2119.
4. Alphabet (Alphabet-N)
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
| 70-79 | * ? `` ` `` _ ~ ( ) [ ] { } |
| 80-84 | @ % $ # |

String Representation for Implementations:
ALPHABET_N_CHARS_STR = '0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ.-:+=^!/*?`_~()[]{}@%$#'
4.1. R-Set Characters (Alphabet_R)
The R-Set defines a standardized set of 13 characters that are candidates for substitution in Dynamic Passthrough (DP) mode. If an R-Set character (with R-Set Index j) is activated in the signal mask, this character from the input (identified by its ASCII value) is replaced by the j-th "passthrough-safe" special character (allowedPassthroughSafeReplacementCharacters[j], see Section 4.2). The R-Set characters are assigned fixed indices 0-12.

| Index (j) | R_Char (Conceptual) | ASCII Value | Notes |
|---|-----|-----|-----|
| 0 | (space) | 32 | Space |
| 1 | " | 34 | Double quote |
| 2 | ' | 39 | Single quote |
| 3 | , | 44 | Comma |
| 4 | ; | 59 | Semicolon |
| 5 | \ | 92 | Backslash |
| 6 | | | 124 | Pipe symbol |
| 7 | < | 60 | Less-than symbol |
| 8 | > | 62 | Greater-than symbol |
| 9 | & | 96 | Ampersand |
| 10 | \t (tab) | 9 | Horizontal Tab |
| 11 | \n (newline) | 10 | Line Feed |
| 12 | \r (car. ret) | 13 | Carriage Return |

An encoder checks for the ASCII values of R-Set characters in the input. If an R-Set character with R-Set Index j is found and its corresponding mask bit j is set, it is replaced by `allowedPassthroughSafeReplacementCharacters[j]`.
4.2. Allowed Passthrough-Safe Replacement Characters
A fixed, ordered list of 13 "allowed passthrough-safe" special characters from Alphabet-N is defined. These are used to replace R-Set characters in DP mode. This list is derived from the 23 special characters present in Alphabet-N (characters at indices 62-84 of ALPHABET_N_CHARS_STR).
The derivation is as follows:
 * Identify the 23 special characters from Alphabet-N (indices 62-84: `.-:+=^!/*?`_~()[]{}@%$#`).
 * Exclude Alphabet-N's designated fixed Escape Character (~, at index 74).
 * Exclude three additional predefined characters: . (period, index 62), - (hyphen, index 63), and _ (underscore, index 73).
 * The first 13 characters from the remaining list, ordered by their original index in Alphabet-N, form the "allowed passthrough-safe replacement characters":
   * : (index 64) `allowedPassthroughSafeReplacementCharacters[0]` (replaces R-Set Space, R-Set index j=0)
   * + (index 65) `allowedPassthroughSafeReplacementCharacters[1]` (replaces R-Set ", R-Set index j=1)
   * = (index 66) `allowedPassthroughSafeReplacementCharacters[2]` (replaces R-Set ', R-Set index j=2)
   * ^ (index 67) `allowedPassthroughSafeReplacementCharacters[3]` (replaces R-Set ,, R-Set index j=3)
   * ! (index 68) `allowedPassthroughSafeReplacementCharacters[4]` (replaces R-Set ;, R-Set index j=4)
   * / (index 69) `allowedPassthroughSafeReplacementCharacters[5]` (replaces R-Set \, R-Set index j=5)
   * * (index 70) `allowedPassthroughSafeReplacementCharacters[6]` (replaces R-Set |, R-Set index j=6)
   * ? (index 71) `allowedPassthroughSafeReplacementCharacters[7]` (replaces R-Set <, R-Set index j=7)
   * `` ` `` (index 72) `allowedPassthroughSafeReplacementCharacters[8]` (replaces R-Set >, R-Set index j=8)
   * ( (index 75) `allowedPassthroughSafeReplacementCharacters[9]` (replaces R-Set `` ` ``, R-Set index j=9)
   * ) (index 76) `allowedPassthroughSafeReplacementCharacters[10]` (replaces R-Set \t, R-Set index j=10)
   * [ (index 77) `allowedPassthroughSafeReplacementCharacters[11]` (replaces R-Set \n, R-Set index j=11)
   * ] (index 78) `allowedPassthroughSafeReplacementCharacters[12]` (replaces R-Set \r, R-Set index j=12)
4.3. Escape Character
The character ~ (tilde, at index 74 of Alphabet-N) SHALL be the fixed escape character for Base85N. It is used in Dynamic Passthrough (DP) mode to represent:
a) Literal occurrences of ~ itself (encoded as ~~).
b) Literal occurrences of any of the 13 `allowedPassthroughSafeReplacementCharacters[j]` if these characters appear in the original byte sequence AND the j-th bit in the RSetIndividualActivationMask for the current DP segment is set (encoded as ~ followed by the character).

Example for Section 4.3.b:
Assume the R-Set character with index `j=0` (Space, ASCII 32, see table in 4.1) is activated in the current `RSetIndividualActivationMask` of the DP segment.
The character at position `j=0` of the `allowedPassthroughSafeReplacementCharacters` list is, according to Section 4.2, the colon (`:`).
Consequence for encoding in DP mode:
* A space character (ASCII 32) in the original data stream is replaced by a colon (`:`) in the transformed DP data stream.
* If an actual colon (ASCII 58) now occurs in the original data stream, it MUST be encoded as a tilde followed by the colon (`~:`) in the transformed DP data stream. This is necessary so that the decoder can distinguish this original colon from a colon that represents a replaced space.
If the R-Set character for space (index `j=0`) had not been activated in the mask, an original colon in the data stream (since it is part of Alphabet-N and not the escape character `~` itself) would appear directly as a colon (`:`) in the transformed DP data stream.
5. Endianness
All conversions between multi-byte integers and byte sequences MUST use Big-Endian byte order.
6. Encoding Algorithm
The encoding algorithm processes an input byte stream, accumulating data into an intermediate_buffer. It exclusively uses Alphabet-N. The algorithm adaptively chooses between Dynamic Passthrough (DP) mode and standard Block mode for segments of the input data.
6.1. Main Encoding Loop
Initialize output_string = "".
Input data is progressively accumulated into intermediate_buffer.
The loop continues as long as intermediate_buffer is not empty.
Inside the loop:
 * Identify Longest DP-Candidate Prefix:
   The encoder SHALL identify the longest possible contiguous prefix from the start of intermediate_buffer (hereinafter dp_candidate_prefix) that meets both of the following conditions:
   a.  The length of dp_candidate_prefix MUST be \ge MIN_PASSTHROUGH_BYTES.
   b.  All bytes within dp_candidate_prefix MUST be representable under DP rules. A byte is representable if:
   i.  Its character representation chr(byte) is in ALPHABET_N_CHARS_STR and is not ~ and is not one of the 13 `allowedPassthroughSafeReplacementCharacters[j]` for which the j-th bit in the R-Set mask (see below, based on this dp_candidate_prefix) would be set.
   ii. It is the ASCII value of an R-Set character R_Char[j] for which the j-th bit in the R-Set mask (determined based on the content of dp_candidate_prefix) would be set (it will be replaced).
   iii. It is the ASCII value of ~ (it will be escaped as ~~).
   iv. Its character representation chr(byte) is one of the `allowedPassthroughSafeReplacementCharacters[j]` AND the j-th bit in the R-Set mask (for R_Char[j]) is set (it will be escaped as ~ + character).
   The first byte that does not meet these conditions terminates the dp_candidate_prefix.
 * Decision and Processing:
   * Case A: A dp_candidate_prefix was identified (meeting conditions 1.a and 1.b).
     The encoder SHALL then perform a DP Suitability and Efficiency Check for this dp_candidate_prefix:
     i.  Determine the preliminary RSetIndividualActivationMask (13 bits) based solely on the R-Set characters present within dp_candidate_prefix.
     ii. Transform dp_candidate_prefix into a sequence of transformed_escaped_data_chars according to DP rules using the determined mask:
     * Replace active R-Set characters with their corresponding `allowedPassthroughSafeReplacementCharacters`.
     * Escape ~ as ~~.
     * Escape any original `allowedPassthroughSafeReplacementCharacters[j]` if the j-th bit in the mask is set, as ~ + character.
     * Other representable characters remain literals.
     iii. Calculate conceptual_dp_output_length:
     Let L_{transformed} be the length of transformed_escaped_data_chars.
     num_segments = ceil(L_{transformed} / MAX_DP_OUTPUT_CHARS_PER_SIGNAL).
     conceptual_dp_output_length = (num_segments * 5) + L_{transformed}. (5 characters per DP signal).
     iv. Calculate block_mode_output_length for dp_candidate_prefix:
     block_mode_output_length = ceil(length(dp_candidate_prefix) / 4) * 5.
     v.  Comparison:
     * If conceptual_dp_output_length < block_mode_output_length (DP is strictly shorter):
     The dp_candidate_prefix SHALL be encoded using DP mode.
     The transformed_escaped_data_chars are divided into one or more segments, each not exceeding MAX_DP_OUTPUT_CHARS_PER_SIGNAL characters.
     For each segment, a 5-character DP signal (encoding the RSetIndividualActivationMask from 2.A.i and the actual character length of that segment's data) is generated (see Section 9).
     The signal(s) and their corresponding transformed_escaped_data_chars segment(s) are appended to output_string.
     * Else (DP is NOT strictly shorter for this dp_candidate_prefix):
     The entire dp_candidate_prefix SHALL be encoded using ProcessWithBlockMode (see Section 6.2). The result is appended to output_string.
   * Case B: No dp_candidate_prefix was identified (no prefix met both length (1.a) and representability (1.b) criteria from Step 1; e.g., an unrepresentable character occurred before MIN_PASSTHROUGH_BYTES could be satisfied, or intermediate_buffer itself is shorter than MIN_PASSTHROUGH_BYTES).
     Let bytes_to_process_block be min(4, length(intermediate_buffer)). If bytes_to_process_block == 0 (i.e., intermediate_buffer is empty), the loop terminates. Otherwise:
     The first bytes_to_process_block bytes of intermediate_buffer SHALL be encoded using ProcessWithBlockMode. The result is appended to output_string.
 * Update intermediate_buffer:
   The bytes corresponding to the data processed (either the dp_candidate_prefix in Case A, or bytes_to_process_block in Case B) SHALL be removed from the beginning of intermediate_buffer.
 * The loop (Step 1-3) repeats until intermediate_buffer is empty.
   The final output_string is the Base85N encoded result.
6.2. ProcessWithBlockMode(buffer_to_encode)
This function encodes buffer_to_encode using standard Base85N block encoding.
It processes the input in 4-byte full blocks. Each 4-byte block is treated as a 32-bit unsigned integer (Big-Endian) and converted into five Base85N characters using Alphabet-N (see Section 8 for conversion).
If buffer_to_encode is not a multiple of 4 bytes (i.e., a partial final block of 1, 2, or 3 bytes remains), these trailing bytes are padded with zero bytes (conceptually, to the right) to form a 4-byte block. This 4-byte block is then converted to 5 Base85N characters. From this 5-character group, only the first 2, 3, or 4 characters are taken as the encoded output for original 1, 2, or 3 trailing bytes, respectively. The output of this function is the string of Alphabet-N characters.
6.3. Required State Information
 * Dynamic State Information: intermediate_buffer (stores bytes from the input stream awaiting processing).
6.4. Constants
 * MIN_PASSTHROUGH_BYTES = 20 (Minimum original input length of a segment to attempt DP processing, as per Step 1.a).
 * MAX_DP_OUTPUT_CHARS_PER_SIGNAL = 511 (Max character length of a DP segment's data, derived from the 9-bit length field in the DP signal).
6.5. Main Encoding Logic Summary
The encoder exclusively uses Alphabet-N. It adaptively processes an intermediate_buffer of input bytes. For each processing step, it first attempts to identify the longest possible prefix of the buffer that is at least MIN_PASSTHROUGH_BYTES long and contains only bytes representable under DP rules (literals, R-Set replacements, or escapes).
If such a "DP-candidate prefix" is found, the encoder calculates the potential output length if this prefix were DP-encoded (including signals and transformed data) and compares it to the output length if the same prefix were encoded using standard Base85N block encoding. DP mode is chosen for this prefix only if its output is strictly shorter. If DP is chosen, the prefix is transformed (R-Set characters replaced, necessary characters escaped), and then this transformed data is segmented if its length exceeds MAX_DP_OUTPUT_CHARS_PER_SIGNAL. Each segment is preceded by a DP signal.
If DP mode is not chosen for an identified DP-candidate prefix (because it's not more efficient), that entire prefix is encoded using standard block mode.
If no suitable DP-candidate prefix (meeting minimum length and representability) can be identified at the start of the intermediate_buffer, a smaller, standard-sized chunk (typically 4 bytes, or fewer if at the end of the stream) is processed using standard block mode.
This adaptive process continues until all input is encoded.
7. Decoding Algorithm
7.1. General Decoding Principles
A Base85N decoder SHALL process input streams expecting characters from Alphabet-N for all Base85N constructs.
When consuming the input stream, a decoder MUST ignore: space (U+0020), horizontal tab (U+0009), line feed (U+000A), and carriage return (U+000D) encountered between distinct Base85N characters or DP structures. This whitespace-ignoring rule does not alter R-Set character processing within original data (when decoding DP segments) or escaped literals in DP data.
A Base85N decoder processes an input stream (after stripping inter-character whitespace as defined above):
 * Read up to 5 Alphabet-N characters. If End Of File (EOF) or insufficient non-whitespace Alphabet-N characters are available to form a full 5-character group, and these do not constitute a valid partial final block according to standard Base85 rules for decoding, this may be an error or the end of data.
 * Convert the 5 input characters to their integer values (0-84) using ALPHABET_N_CHARS_STR. Any character not in Alphabet-N is an error. Use Base85DigitsToValue (Section 8) to get decodedValue.
 * If $0 \le \text{decodedValue} < 2^{32}$: It's a standard Base85N block. Convert decodedValue to 4 bytes (Big-Endian). Append these bytes to the output. (For partial final blocks, fewer bytes are output, see below).
 * If $\text{decodedValue} \ge 2^{32}$: It's a Dynamic Passthrough (DP) signal.
   a.  Calculate SignalPayload = decodedValue - $2^{32}$.
   b.  Validate SignalPayload. It MUST be in the range 0 to $2^{22}-1$. If not, it's an error (Undefined or Reserved Signal Value).
   c.  Extract RSetIndividualMask_13bit and Length_9bit_encoded_value (L_{enc}) from SignalPayload (see Section 9). L_output_chars = Length_9bit_encoded_value (which is 0-511).
   d.  Read exactly L_output_chars Alphabet-N characters from the input stream immediately following the signal. These characters form the transformed_DP_data. If fewer than L_output_chars characters are available, it's an error (Unexpected End of Stream). Any character not in Alphabet-N is an error.
   e.  DP Data Interpretation: Convert transformed_DP_data back to original bytes:
   i.   Initialize an empty decoded_byte_sequence.
   ii.  Initialize an index idx = 0 for transformed_DP_data.
   iii. While idx < L_output_chars:
   * char1 = transformed_DP_data[idx].
   * If char1 == '~':
   * Increment idx. If idx $\ge$ L_output_chars, this is an error (Dangling escape character).
   * char2 = transformed_DP_data[idx].
   * Append ord(char2) (ASCII value of char2) to decoded_byte_sequence.
   * Else if char1 is `allowedPassthroughSafeReplacementCharacters[j]` (for some index j from 0-12) AND bit j in the decoded RSetIndividualMask_13bit is set:
   * Append the ASCII value of the j-th R-Set character (R_Char[j] from Section 4.1) to decoded_byte_sequence.
   * Else (it's a direct literal character from Alphabet-N):
   * Append ord(char1) to decoded_byte_sequence.
   * Increment idx.
     iv. Append all bytes from decoded_byte_sequence to the main output stream.
 * Handle final partial blocks if any remain after all full blocks and DP segments are processed. If the input stream ends with 2, 3, or 4 Alphabet-N characters that form a partial group, these are decoded by (conceptually) padding them with the character representing value 84 ('#') to make a 5-character group, converting to a 32-bit number, and then taking the first 1, 2, or 3 bytes respectively. Any character not in Alphabet-N is an error.
8. Value/Digit Conversion
CharToValue (converting a character from Alphabet-N to its integer value 0-84) and ValueToChar (converting an integer value 0-84 to its Alphabet-N character) operations exclusively use Alphabet-N as defined in Section 4.
Standard Base85 arithmetic applies for converting 4 bytes to a 32-bit unsigned integer and then to five Base85 digits, and vice-versa, using Big-Endian byte order.
 * Base85DigitsToValue(digits[5]): val = ((( (d0*85 + d1)*85 + d2)*85 + d3)*85 + d4)
 * ValueToBase85Digits(value, digits[5]): for i from 4 down to 0: digits[i] = value % 85; value /= 85 (integer division)
9. Signal Interpretation and Parameter Encoding
For a 5-character sequence (from Alphabet-N) decoded to decodedValue:
 * Standard Block: $0 \le \text{decodedValue} < 2^{32}$. The decodedValue directly represents the 32-bit unsigned integer from a 4-byte group.
 * Dynamic Passthrough (DP) Signal: $\text{decodedValue} \ge 2^{32}$.
   The actual parameters for DP mode are encoded in SignalPayload.
   SignalPayload = decodedValue - $2^{32}$.
 * Total bits for DP parameters: 22. SignalPayload SHALL range from 0 to $2^{22}-1 = 4194303$.
 * Payload Encoding (22 bits total):
   SignalPayload = (RSetIndividualMask_13bit << 9) | Length_9bit_encoded_value
   * RSetIndividualMask_13bit (Bits 9-21 of SignalPayload, where bit 0 is LSB): A 13-bit mask where the j-th bit corresponds to the j-th character in the R-Set (Section 4.1). If a bit is set, the corresponding R-Set character was present in the original data and replaced by its designated `allowedPassthroughSafeReplacementCharacters` entry.
   * Length_9bit_encoded_value (Bits 0-8 of SignalPayload): An unsigned 9-bit integer (L_{enc}) that encodes the exact character length (0-511) of the transformed_DP_data segment that immediately follows this 5-character signal. The ActualOutputCharLength of the DP data segment is L_{enc}.
 * Reserved/Undefined:
The maximum block encoded value is $2^32-1$ and the maximum used for Base85N signals is ($2^{22} - 1$). So any \text{decodedValue} greater then ($2^{32}$) + ($2^{22} - 1$) must be treated as error.
   
10. Error Handling
Implementations MUST detect and report errors, including but not limited to:
 * Invalid Characters during Decoding: Any character encountered in the input stream (after allowed whitespace stripping) that is not part of Alphabet-N and is not part of a valid DP structure.
 * Invalid Final Block Length/Padding: Incorrectly encoded final partial block that does not conform to standard Base85 rules for handling 1, 2, or 3 trailing bytes (as per Section 7.1).
 * Unexpected End of Stream: EOF reached when more characters were expected (e.g., in the middle of a 5-character group, during a DP signal, or when reading transformed_DP_data as specified by a DP signal's length field).
 * Undefined or Reserved Signal Value Encountered: A decodedValue indicating a DP signal whose SignalPayload (i.e., decodedValue - $2^{32}$) falls into the reserved/undefined range (i.e., is greater than $2^{22}-1$).
 * Invalid Dynamic Passthrough Signal Parameters:
   * SignalPayload outside the valid 0 to $2^{22}-1$ range.
   * Length_9bit_encoded_value (L_{enc}) implies reading more transformed_DP_data characters than are available in the stream.
 * Dangling Escape Character: An escape character (~) encountered at the very end of transformed_DP_data segment during DP decoding.
 * Invalid Partial Block Encoding / Overrun: During decoding of a partial block, if the decoded value implies more bytes than are supposed to be represented by that partial block (relevant for some Base85 variants, less so here given the explicit length taken for partial blocks).
11. Encoding Mode
Base85N has one standard encoding behavior which dynamically chooses between two internal strategies as detailed in Section 6:
 * Dynamic Passthrough (DP) Mode: This mode is attempted for the longest possible prefix of the current data that meets minimum length requirements (MIN_PASSTHROUGH_BYTES) and for which all its bytes are representable within DP rules (using Alphabet-N for literals, R-Set replacements, or escapes). DP mode is chosen for this prefix only if it is determined to be strictly more efficient (results in a shorter encoded output including the DP signal(s)) than Block Mode for that same prefix.
 * Block Mode: Standard Base85 encoding (4 bytes to 5 Alphabet-N characters, with handling for final partial blocks) is used if DP mode is not suitable for an identified prefix (i.e., not more efficient), or if no suitable prefix for DP processing (meeting minimum length and representability) can be identified at the current point in the input stream. In the latter case, a smaller segment (typically 4 bytes or less) is processed via block mode.
   The encoder makes this choice adaptively for segments of the input data according to the algorithm in Section 6.

