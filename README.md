# Base85N Encoding Specification – Draft – May 10, 2025

## 1. Abstract
This document defines Base85N, a binary-to-text encoding scheme using an 85-character alphabet selected for broad compatibility. It features a 4-byte-to-5-character core conversion mechanism. An enhanced Dynamic Passthrough (DP) mode allows for more efficient and readable representation of byte sequences that are largely compatible with the main alphabet but may contain certain predefined R-Set characters. DP mode uses a 13-bit signaling mechanism to deterministically activate individual R-Set characters for substitution. If an activated R-Set character (with R-Set Index `j`) is present in the input, it is replaced by the `j`-th character from a defined list of 13 "passthrough-safe" special characters. A fallback to standard Base85 block encoding (Block Mode) is used when DP is unsuitable according to fixed rules. It supports padding-free encoding and decoding, and interoperability with z85 data. The DP signaling mechanism (part of a 5-character Base85 sequence where the decoded value is $\ge 2^{32}$) encodes the R-Set individual activation mask, active alphabet variant (N/Z), and data length.

## 2. Introduction
Binary data, such as cryptographic keys, identifiers, or media files, often needs representation as text. Common formats like JSON, XML, HTML, etc., often impose character set restrictions. Base64 is common but inefficient (approx. 33% overhead). Base85 variants are denser but often use problematic characters.
This specification defines Base85N, aiming for high efficiency combined with a broadly compatible alphabet (Alphabet-N). It includes a Dynamic Passthrough (DP) mechanism intended to represent byte sequences containing predefined R-Set characters by replacing them with characters from a "passthrough-safe" subset of 13 special characters from the main alphabet if those R-Set characters are individually activated. This can improve efficiency and readability for suitable data compared to pure Base85 block encoding. DP commitment requires a minimum length (`MIN_PASSTHROUGH_BYTES`).

### 2.1. Key Features and Rationale
* **High Data Density:** Core 4-byte to 5-character Base85 conversion offers better density than Base64.
* **Protocol-Friendly Alphabet:** Alphabet-N avoids characters commonly needing escaping.
* **Dynamic Passthrough (DP):** Attempts to directly represent sequences of bytes. If R-Set characters (with R-Set Index `j`) are present, and they are individually activated via a deterministically calculated signal mask, they are replaced by the `j`-th "passthrough-safe" special character from a defined list of 13. This is intended to keep text-like data readable and easily embeddable.
* **Block Mode Fallback:** If `z85Strict` mode is active, or if the data chunk does not meet the minimum length requirement for DP (`MIN_PASSTHROUGH_BYTES`), encoding reverts to standard Base85 block processing. Otherwise, Dynamic Passthrough mode (with a deterministically calculated `RSetIndividualActivationMask`) is used.
* **Adaptive Mode Logic:** The encoding process dynamically selects between DP and Block modes based on the fixed rules defined herein.
* **Padding-Free:** Handles input of any length using standard Base85 partial block encoding in Block mode.
* **z85 Interoperability:** Decoders handle z85 data. Encoders offer modes for z85 alphabet use or strict z85 output.
* **Fixed Escape Character:** `~` (tilde) is the designated escape character for Alphabet N. `>` (greater than) is the designated escape character for Alphabet Z. This escape character is used to literally represent itself or any of the 13 "passthrough-safe" replacement characters if they appear in the original data.

## 3. Conformance Requirements
The keywords "MUST", "MUST NOT", "REQUIRED", "SHALL", "SHALL NOT", "SHOULD", "SHOULD NOT", "RECOMMENDED", "MAY", and "OPTIONAL" are to be interpreted as described in RFC 2119.

## 4. Alphabets and Replacement Sets
Base85N MUST support the following 85-character main alphabets: Variant N and Variant Z. The characters are assigned unique integer values from 0 to 84.

**Alphabet Definition:**
* Values 0-9: Characters '0' through '9'
* Values 10-35: Characters 'A' through 'Z'
* Values 36-61: Characters 'a' through 'z'
* Value 62: Character `.` (period)
* Value 63: Character `-` (hyphen)
* Value 64: Character `:` (colon)
* Value 65: Character `+` (plus)
* Value 66: Character `=` (equals)
* Value 67: Character `^` (caret)
* Value 68: Character `!` (exclamation mark)
* Value 69: Character `/` (slash)
* Value 70: Character `*` (asterisk)
* Value 71: Character `?` (question mark)
* Value 72: Character `&` (ampersand)
* Value 73:
    * Alphabet N: Character `_` (underscore)
    * Alphabet Z: Character `<` (less-than)
* Value 74:
    * Alphabet N: Character `~` (tilde)
    * Alphabet Z: Character `>` (greater-than)
* Value 75: Character `(` (left parenthesis)
* Value 76: Character `)` (right parenthesis)
* Value 77: Character `[` (left square bracket)
* Value 78: Character `]` (right square bracket)
* Value 79: Character `{` (left curly brace)
* Value 80: Character `}` (right curly brace)
* Value 81: Character `@` (at sign)
* Value 82: Character `%` (percent)
* Value 83: Character `$` (dollar)
* Value 84: Character `#` (hash)

### 4.1. R-Set Characters (Alphabet_R)
The R-Set defines a standardized set of 13 characters. In Dynamic Passthrough (DP) mode, if an R-Set character (with R-Set Index `j`) is activated in the signal mask, this character from the input is replaced by the `j`-th "passthrough-safe" special character from the list defined in Section 4.2. The R-Set characters are assigned fixed indices 0-12 for mapping and mask bit correspondence:

| Index (j) | R_N Char        | R_Z Char        | ASCII Value (N) | ASCII Value (Z) | Notes                 |
| :-------- | :-------------- | :-------------- | :-------------- | :-------------- | :-------------------- |
| 0         | (space)         | (space)         | 32              | 32              | Space                 |
| 1         | `"`             | `"`             | 34              | 34              | Double quote          |
| 2         | `'`             | `'`             | 39              | 39              | Single quote          |
| 3         | `,`             | `,`             | 44              | 44              | Comma                 |
| 4         | `;`             | `;`             | 59              | 59              | Semicolon             |
| 5         | `\`             | `\`             | 92              | 92              | Backslash             |
| 6         | `|`             | `|`             | 124             | 124             | Pipe symbol           |
| 7         | `<`             | `_`             | 60              | 95              | Variant-specific      |
| 8         | `>`             | `~`             | 62              | 126             | Variant-specific      |
| 9         | ``` ` ```       | ``` ` ```       | 96              | 96              | Backtick / Grave accent |
| 10        | `\t` (tab)      | `\t` (tab)      | 9               | 9               | Horizontal Tab        |
| 11        | `\n` (newline)  | `\n` (newline)  | 10              | 10              | Line Feed             |
| 12        | `\r` (car. ret) | `\r` (car. ret) | 13              | 13              | Carriage Return       |

### 4.2. Allowed Passthrough-Safe Replacement Characters
A fixed, ordered list of 13 "allowed passthrough-safe" special characters is defined. These characters are used to replace R-Set characters: when an R-Set character (with R-Set Index `j`) is activated via the signal mask and present in the input, it is replaced by the `j`-th character from this list.

This list is derived from the 23 special characters of the main Base85N alphabet (values 62-84 in Section 4) by applying variant-specific exclusions and then taking the first 13 available characters, ordered by their Base85N value.
* **Exclusions for Alphabet N:**
    * The fixed Escape Character: `~` (Value 74).
    * Additional predefined characters: `.` (Value 62), `-` (Value 63), `_` (Value 73).
* **Exclusions for Alphabet Z:**
    * The fixed Escape Character: `>` (Value 74).
    * Additional predefined characters: `.` (Value 62), `-` (Value 63), `<` (Value 73).

After applying these respective exclusions, the process of taking the first 13 characters from the remaining special characters (ordered by their Base85N value) yields the *same* ordered list for both Alphabet N and Alphabet Z.

The common ordered list of 13 "allowed passthrough-safe replacement characters" is:
1.  `:` (Value 64)
2.  `+` (Value 65)
3.  `=` (Value 66)
4.  `^` (Value 67)
5.  `!` (Value 68)
6.  `/` (Value 69)
7.  `*` (Value 70)
8.  `?` (Value 71)
9.  `&` (Value 72)
10. `(` (Value 75)
11. `)` (Value 76)
12. `[` (Value 77)
13. `]` (Value 78)

(The characters `{`, `}`, `@`, `%`, `$`, `#` from the main alphabet's special characters, as well as the initially excluded `.`, `-`, `_` (for N) / `<` (for Z), are not part of this replacement list and are thus not specially escaped unless they are the active escape character itself).

### 4.3. Fixed Escape Character
* For Alphabet N, the tilde character `~` (Value 74 in Alphabet N) SHALL be the fixed escape character.
* For Alphabet Z, the greater-than character `>` (Value 74 in Alphabet Z) SHALL be the fixed escape character.

The active escape character is used in DP mode to represent:
a) Literal occurrences of the escape character itself.
b) Literal occurrences of any of the 13 "allowed passthrough-safe replacement characters" (defined in Section 4.2) if these characters appear in the original byte sequence.

## 5. Endianness
All conversions between multi-byte integers and byte sequences MUST use Big-Endian byte order.

## 6. Encoding Algorithm
The encoding algorithm processes an input byte stream. Key parameters include the active `encoding_variant` ('N' or 'Z') and the `z85Strict` flag. An `intermediate_buffer` accumulates bytes.
When `intermediate_buffer` is considered for DP (e.g., upon reaching `MIN_PASSTHROUGH_BYTES`, or at end-of-stream for remaining buffer content):
* If `z85Strict` is true or the length of the `intermediate_buffer` is less than `MIN_PASSTHROUGH_BYTES`, the buffer contents SHALL be processed using Block Mode.
* Otherwise (Dynamic Passthrough mode is applied):
    a.  The 13-bit `RSetIndividualActivationMask` SHALL be determined as follows: for each R-Set character (identified by its R-Set Index `j` from 0 to 12, as defined in Section 4.1), the corresponding bit `j` in the `RSetIndividualActivationMask` SHALL be set to 1 if and only if the R-Set character with R-Set Index `j` is present in the `intermediate_buffer`. Otherwise, the bit SHALL be set to 0.
    b.  A 5-character DP signal is generated (see Section 9 for payload details, including the `RSetIndividualActivationMask`, Mode, and Length).
    c.  The `intermediate_buffer` (referred to as `original_data`) is transformed to `transformed_escaped_data` as follows: For each character `char_orig` in `original_data`:
        i.  If `char_orig` is an R-Set character with R-Set Index `j` (0-12 as defined in Section 4.1) AND bit `j` in the `RSetIndividualActivationMask` is set: Append `allowedPassthroughSafeReplacementCharacters[j]` (from Section 4.2) to `transformed_escaped_data`.
        ii. Else (if `char_orig` is not an R-Set character to be replaced as above):
            1.  If `char_orig` is identical to the `activeFixedEscapeCharacter` (Section 4.3) OR `char_orig` is one of the 13 `allowedPassthroughSafeReplacementCharacters` (Section 4.2): Append the `activeFixedEscapeCharacter` to `transformed_escaped_data`, then append `char_orig`.
            2.  Else: Append `char_orig` directly to `transformed_escaped_data`.
    d.  The DP signal followed by `transformed_escaped_data` is written to the output stream.
    e.  The `intermediate_buffer` is cleared.
Input bytes not processed as part of a DP sequence are handled via Block Mode (accumulated and encoded in 4-byte quanta or as partial blocks at stream end).

### 6.1. Required State Information
* Configuration Information: `encoding_variant` ('N' or 'Z'), `z85Strict` flag, `activeMainAlphabet`, the ordered list of 13 `allowedPassthroughSafeReplacementCharacters`, `activeFixedEscapeCharacter`.
* Dynamic State Information: `intermediate_buffer`.

### 6.2. Constants
* `MIN_PASSTHROUGH_BYTES` = 20
* `MAX_PASSTHROUGH_BYTES` = 1020. This maximum length is chosen as it is a multiple of 5, aligning with the 5-byte block granularity used for encoding the length parameter within the DP signal (see Section 9). Data chunks for DP MUST have a length that is a multiple of 5, within the range [`MIN_PASSTHROUGH_BYTES`, `MAX_PASSTHROUGH_BYTES`].

### 6.3. Main Encoding Logic (Conceptual)
The encoder manages an `intermediate_buffer`. It attempts to apply DP when the buffer meets `MIN_PASSTHROUGH_BYTES` (and its length is a multiple of 5) and is not in `z85Strict` mode. The `RSetIndividualActivationMask` is determined based on buffer content by checking for the presence of each of the 13 R-Set characters. If DP is applied, a signal is generated, R-Set characters (activated by their corresponding mask bit and present in buffer) are replaced using direct index mapping to the 13 replacement characters, necessary original characters are escaped, and the transformed buffer is output. Otherwise, standard Base85N Block Mode encoding is used. (Note: If buffer length is not a multiple of 5 at end-of-stream, and the remaining portion is less than `MIN_PASSTHROUGH_BYTES` or otherwise unsuitable for a final DP attempt, it would default to Block Mode for final partial block encoding).

## 7. Decoding Algorithm
A Base85N decoder processes an input stream:
* Read up to 5 input characters. If EOF or insufficient characters for a final block, handle error or end.
* Convert the 5 characters to `decodedValue` using `Base85DigitsToValue`. `CharToValue` (which checks N and Z alphabets) is used for validation of input characters.
* If $0 \le \text{decodedValue} < 2^{32}$: It's a standard Base85 block. Convert to 4 bytes, append to output.
* If $\text{decodedValue} \ge 2^{32}$: It's a DP signal.
    a.  Calculate `SignalPayload` = `decodedValue` - $2^{32}$.
    b.  Validate `SignalPayload` against the defined range (0 to $2^{22}-1$). If out of range, it's an error or a reserved value.
    c.  Extract `RSetIndividualMask_13bit`, `Mode_1bit`, and `Length_8bit_encoded_value` from `SignalPayload` as per Section 9.
    d.  Determine `activeFixedEscapeCharacter` and the ordered list of 13 `allowedPassthroughSafeReplacementCharacters` based on `Mode_1bit`.
    e.  Calculate the actual byte length of the DP data from `Length_8bit_encoded_value`. Read that many characters from the input stream as the `transformed_DP_data`. This length MUST be a multiple of 5.
    f.  Iterate through `transformed_DP_data` to reconstruct the original byte sequence:
        i.  If the current character is the `activeFixedEscapeCharacter`, the next character is taken literally, appended to the output, and the input pointer is advanced by two characters.
        ii. Else, let the current character be C. Check if C is the `p`-th character (0-indexed) in the `allowedPassthroughSafeReplacementCharacters` list (i.e., `C == allowedPassthroughSafeReplacementCharacters[p]`).
            1.  If yes, AND `p < 13` (i.e., `p` is a valid R-Set Index), AND bit `p` in the `RSetIndividualMask_13bit` (from the signal) is set: Append `RSetCharacter[p]` (the R-Set character with R-Set Index `p` as defined in Section 4.1) to output. Advance input pointer by one.
            2.  Else (C is not in the list, or `p >= 13`, or bit `p` in the mask is not set): C MUST be treated as a literal character. Append C to output. Advance input pointer by one.
* Handle final partial blocks (if any remaining input is < 5 chars) as standard Base85 partial decoding.

## 8. Value/Digit Conversion
* `ValueToBase85Digits(numericValue, numDigits)`: Converts `numericValue` to fixed number of Base85 digits (0-84). Requires `numericValue` < $85^{\text{numDigits}}$.
* `Base85DigitsToValue(inputDigits, numDigits)`: Converts Base85 digits (0-84) to `numericValue`. $\text{numericValue} = \sum_{i=0}^{\text{numDigits}-1} (\text{inputDigits}[i] \times 85^{(\text{numDigits}-1-i)})$.

## 9. Signal Interpretation and Parameter Encoding
For a 5-character sequence decoded to `decodedValue`:
* **Standard Block:** $0 \le \text{decodedValue} < 2^{32}$. These are standard Base85 encoded blocks.
* **Dynamic Passthrough (DP) Signal:** $\text{decodedValue} \ge 2^{32}$.
    The `SignalPayload` = `decodedValue` - $2^{32}$ encodes the DP parameters. The total number of bits for these parameters is 22. `SignalPayload` SHALL range from 0 to $2^{22}-1 = 4194303$.
    * **Payload Encoding (22 bits total):**
        The 22 bits of `SignalPayload` are structured as: (`RSetIndividualMask_13bit` << 9) | (`Mode_1bit` << 8) | `Length_8bit_encoded_value`
        * `RSetIndividualMask_13bit` (Bits 9-21 of `SignalPayload`): A 13-bit mask. Bit `j` (0-12) corresponds to the R-Set character with R-Set Index `j` (as defined in Section 4.1). If set, this R-Set character, if found in the original data, is subject to replacement by `allowedPassthroughSafeReplacementCharacters[j]`.
        * `Mode_1bit` (Bit 8 of `SignalPayload`): 0 for Alphabet-N (with `~` escape), 1 for Alphabet-Z (with `>` escape).
        * `Length_8bit_encoded_value` (Bits 0-7 of `SignalPayload`): Encodes the length of the original byte sequence of the DP block. The value $L_{enc}$ stored here (0-255) maps to actual byte lengths. For lengths from 20 to 1020 bytes (inclusive) in 5-byte increments: $\text{ActualByteLength} = (L_{enc} + 4) \times 5$. This means $L_{enc}=0$ corresponds to 20 bytes, $L_{enc}=1$ to 25 bytes, ..., up to $L_{enc}=200$ for 1020 bytes. Values $L_{enc} = 201$ to 255 are reserved. An encoder MUST ensure that the original byte sequence length for DP is a multiple of 5.
* **Reserved/Undefined:** Any `decodedValue` where $2^{32} + (2^{22}-1) < \text{decodedValue} < 85^5$. Encoders MUST NOT generate these. Decoders SHOULD report an error.

## 10. Error Handling
Implementations MUST detect and report errors, including:
* Invalid Characters during Decoding.
* Invalid Final Block Length (1 char).
* Unexpected End of Stream.
* Undefined or Reserved Signal Value Encountered.
* Invalid Dynamic Passthrough Signal Parameters (e.g., `Length_8bit_encoded_value` maps to a length outside the defined range of 20-1020 bytes, unless an extension for reserved values is defined; or encoded DP data length not matching signaled length).
* Invalid Partial Block Encoding / Overrun.

## 11. z85 Compatibility and Encoding Modes
* **Default Base85N Mode:** Uses Alphabet-N, new DP logic with 13 R-Set characters individually activated (using direct index mapping to the 13 replacement characters) and `~` escape.
* **z85 Alphabet Mode:** Uses Alphabet-Z, new DP logic with 13 R-Set characters individually activated (using direct index mapping to the 13 replacement characters) and `>` escape. Output uses Z-alphabet characters but may include DP signals.
* **z85 Strict Mode:** `encoding_variant` is 'Z', `z85Strict` is true. DP encoding mode MUST be disabled. Output is pure z85.

## Appendix A: Helper Functions and Methods (Conceptual Descriptions)
(This appendix describes the conceptual operation of functions and methods that would be part of an implementation.)

### A.1 External Helper Functions (Assumed to exist)
* `CharToValue(char_c, variant)`: Converts a character `char_c` to its Base85N integer value (0-84) based on the `variant` (N or Z). Returns error indicator if `char_c` is not in the specified variant's alphabet.
* `ValueToChar(value_v, variant)`: Converts a Base85N integer value `value_v` (0-84) to its character representation for the `variant`.
* `GetAllowedPassthroughSafeReplacementCharacters(variant)`: Returns the ordered list of 13 "allowed" special characters (as per Section 4.2).
* `GetActiveFixedEscapeCharacter(variant)`: Returns `~` for N, `>` for Z (as per Section 4.3).
* `EncodeStandardBlocks(byte_sequence, variant, outputStream)`: Implements standard Base85 encoding for full 4-byte quanta and final partial blocks.

### A.2 Encoder Class Methods (Conceptual for Base85NEncoder)
* `constructor(variant, z85Strict)`: Initializes configuration by loading alphabets, the list of 13 allowed replacement characters, and the active escape character based on the `variant`.
* `generateDPSignalPayload(RSetIndividualMask_13bit, Mode_1bit, Length_8bit_encoded_value)`: Calculates the 22-bit `SignalPayload` integer value according to the bit-shifting and ORing logic defined in Section 9.
* `transformAndEscapeBufferForDP(original_buffer, RSetIndividualActivationMask, variant)`:
    * Retrieves the list of 13 `allowedPassthroughSafeReplacementCharacters` and `activeFixedEscapeCharacter` for `variant`.
    * Initializes an empty `transformed_escaped_buffer`.
    * Iterates through `original_buffer` character by character (`char_orig`):
        a.  If `char_orig` is an R-Set character with R-Set Index `j` AND bit `j` in `RSetIndividualActivationMask` is set: Append `allowedPassthroughSafeReplacementCharacters[j]` to `transformed_escaped_buffer`.
        b.  Else (if `char_orig` is not an R-Set character to be replaced):
            i.  If `char_orig` is identical to `activeFixedEscapeCharacter` OR `char_orig` is one of the 13 `allowedPassthroughSafeReplacementCharacters`: Append `activeFixedEscapeCharacter` to `transformed_escaped_buffer`, then append `char_orig`.
            ii. Else: Append `char_orig` to `transformed_escaped_buffer`.
    * Returns `transformed_escaped_buffer`.
* `encodeStream(inputStream, outputStream)`: The main encoding loop.
    * Manages an `intermediate_buffer`.
    * When `intermediate_buffer` reaches `MIN_PASSTHROUGH_BYTES` (and its length is a multiple of 5, up to `MAX_PASSTHROUGH_BYTES`) and `z85Strict` is false:
        * Calculates the `RSetIndividualActivationMask` based on the content of `intermediate_buffer` by checking for the presence of each of the 13 R-Set characters (as per Section 6.a).
        * Calls `transformAndEscapeBufferForDP` with `intermediate_buffer`, the calculated `RSetIndividualActivationMask` and `encoding_variant`.
        * Determines `Mode_1bit` from `encoding_variant`.
        * Calculates `Length_8bit_encoded_value` from `intermediate_buffer.length()`.
        * Calls `generateDPSignalPayload` to get the 22-bit `SignalPayload`.
        * Converts $2^{32} + \text{SignalPayload}$ to 5 Base85N characters using `ValueToBase85Digits` and `ValueToChar`. Writes these 5 signal characters to `outputStream`.
        * Writes the `transformed_escaped_buffer` (from `transformAndEscapeBufferForDP`) to `outputStream`.
        * Clears `intermediate_buffer`.
    * If DP is not applied (e.g., buffer too short, `z85Strict`, or length not multiple of 5 for DP), characters are kept in `intermediate_buffer` for standard block processing. `EncodeStandardBlocks` is called for full 4-byte quanta, or at end-of-stream for any remaining 1-3 bytes.

### A.3 Decoder Helper Concepts (Conceptual for Base85NDecoder)
* `decodeDPData(transformed_data_chars, RSetIndividualActivationMask, variant)`:
    * Retrieves `activeFixedEscapeCharacter` and the list of 13 `allowedPassthroughSafeReplacementCharacters` for the `variant`.
    * Initializes an empty `original_data_buffer`.
    * Iterates through `transformed_data_chars` using an input pointer:
        a.  If the character at the pointer is `activeFixedEscapeCharacter`: Increment pointer, append the character at the new pointer position to `original_data_buffer`. Increment pointer again.
        b.  Else (character is not an escape prefix): Let current char be C.
            i.  Check if C is the `p`-th character (0-indexed) in the `allowedPassthroughSafeReplacementCharacters` list.
            ii. If yes, AND `p < 13`, AND bit `p` in `RSetIndividualActivationMask` is set: Append `RSetCharacter[p]` to `original_data_buffer`.
            iii.Else: Append C (as a literal) to `original_data_buffer`.
            iv. Increment pointer.
    * Returns `original_data_buffer`.

