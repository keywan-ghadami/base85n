# Base85N Encoding Specification – Draft – May 10, 2025

## 1. Abstract
This document defines Base85N, a binary-to-text encoding scheme using an 85-character alphabet selected for broad compatibility. It features a 4-byte-to-5-character core conversion mechanism. An enhanced Dynamic Passthrough (DP) mode allows for more efficient and readable representation of byte sequences that are largely compatible with the main alphabet but may contain certain predefined R-Set characters. DP mode uses a 13-bit signaling mechanism to deterministically activate individual R-Set characters for substitution. If an activated R-Set character (with R-Set Index `j`) is present in the input, it is replaced by the `j`-th character from a defined list of 13 "passthrough-safe" special characters. A fallback to standard Base85 block encoding (Block Mode) is used when DP is unsuitable according to fixed rules. It supports padding-free encoding and decoding, and interoperability with z85 data. The DP signaling mechanism (part of a 5-character Base85 sequence where the decoded value is $\ge 2^{32}$) encodes the R-Set individual activation mask, active alphabet variant (N/Z), and data length.

## 2. Introduction
Binary data, such as cryptographic keys, identifiers, or media files, often needs representation as text. Common formats like JSON, XML, HTML, etc., often impose character set restrictions. Base64 is common but inefficient (approx. 33% overhead). Base85 variants are denser but often use problematic characters.
This specification defines Base85N, aiming for high efficiency combined with a broadly compatible alphabet (Alphabet-N). It includes a Dynamic Passthrough (DP) mechanism intended to represent byte sequences containing predefined R-Set characters by replacing them with characters from a "passthrough-safe" subset of 13 special characters from the main alphabet if those R-Set characters are individually activated. This can improve efficiency and readability for suitable data compared to pure Base85 block encoding. DP commitment requires a minimum length (`MIN_PASSTHROUGH_BYTES`).

### 2.1. Key Features and Rationale
* **High Data Density:** Core 4-byte to 5-character Base85 conversion offers better density than Base64.
* **Protocol-Friendly Alphabet:** Alphabet-N, based on z85 with minor modifications, aims for broad compatibility.
* **Dynamic Passthrough (DP):** Attempts to directly represent sequences of bytes. If R-Set characters (with R-Set Index `j`) are present, and they are individually activated via a deterministically calculated signal mask, they are replaced by the `j`-th "passthrough-safe" special character from a defined list of 13. This is intended to keep text-like data readable and easily embeddable.
* **Block Mode Fallback:** If `z85Strict` mode is active, or if the data chunk does not meet the minimum length requirement for DP (`MIN_PASSTHROUGH_BYTES`), encoding reverts to standard Base85 block processing. Otherwise, Dynamic Passthrough mode (with a deterministically calculated `RSetIndividualActivationMask`) is used.
* **Adaptive Mode Logic:** The encoding process dynamically selects between DP and Block modes based on the fixed rules defined herein.
* **Padding-Free:** Handles input of any length using standard Base85 partial block encoding in Block mode.
* **z85 Interoperability:** Decoders handle z85 data. Encoders offer modes for z85 alphabet use or strict z85 output. Base85N's Alphabet Z is identical to the z85 standard.
* **Fixed Escape Character:** `~` (tilde) is the designated escape character for Alphabet N. `>` (greater than) is the designated escape character for Alphabet Z. This escape character is used to literally represent itself or any of the 13 "passthrough-safe" replacement characters if they appear in the original data.

## 3. Conformance Requirements
The keywords "MUST", "MUST NOT", "REQUIRED", "SHALL", "SHALL NOT", "SHOULD", "SHOULD NOT", "RECOMMENDED", "MAY", and "OPTIONAL" are to be interpreted as described in RFC 2119.

## 4. Alphabets
Base85N defines two primary 85-character alphabets: Variant Z and Variant N. Each character is assigned a unique integer value from 0 to 84. Alphabet Z is identical to the standard z85 encoding alphabet. Alphabet N is derived from Alphabet Z by changing two characters at specific positions.

The character assignments for each alphabet, corresponding to their integer values (indices), are presented below.

| Values (Indices) | Alphabet Z Characters             | Alphabet N Characters             | Notes (N vs. Z)                          |
| :--------------- | :-------------------------------- | :-------------------------------- | :--------------------------------------- |
| 0-9              | `0 1 2 3 4 5 6 7 8 9`           | `0 1 2 3 4 5 6 7 8 9`           |                                          |
| 10-19            | `a b c d e f g h i j`           | `a b c d e f g h i j`           |                                          |
| 20-29            | `k l m n o p q r s t`           | `k l m n o p q r s t`           |                                          |
| 30-39            | `u v w x y z A B C D`           | `u v w x y z A B C D`           |                                          |
| 40-49            | `E F G H I J K L M N`           | `E F G H I J K L M N`           |                                          |
| 50-59            | `O P Q R S T U V W X`           | `O P Q R S T U V W X`           |                                          |
| 60-69            | `Y Z . - : + = ^ ! /`           | `Y Z . - : + = ^ ! /`           |                                          |
| 70-79            | `* ? & < > ( ) [ ] {`           | `* ? & _ ~ ( ) [ ] {`           | Index 73: N=`_` (Z=`<`), Index 74: N=`~` (Z=`>`) |
| 80-84            | `} @ % $ #` (Indices 80-84)     | `} @ % $ #` (Indices 80-84)     |                                          |

**String Representations for Implementations:**

`ALPHABET_Z_CHARS_STR = '0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ.-:+=^!/*?&<>()[]{}\@%$#'`

`ALPHABET_N_CHARS_STR = '0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ.-:+=^!/*?&_~()[]{}\@%$#'`
*(Note: Differs from Alphabet Z at index 73 ('_' instead of '<') and index 74 ('~' instead of '>')).*

### 4.1. R-Set Characters (Alphabet_R)
The R-Set defines a standardized set of 13 characters. In Dynamic Passthrough (DP) mode, if an R-Set character (with R-Set Index `j`) is activated in the signal mask, this character from the input (identified by its ASCII value) is replaced by the `j`-th "passthrough-safe" special character from the list defined in Section 4.2. The R-Set characters are assigned fixed indices 0-12 for this mapping and for the bits in the `RSetIndividualActivationMask`.

| Index (j) | R_Char (Conceptual) | ASCII Value | Notes                 |
| :-------- | :------------------ | :---------- | :-------------------- |
| 0         | (space)             | 32          | Space                 |
| 1         | `"`                 | 34          | Double quote          |
| 2         | `'`                 | 39          | Single quote          |
| 3         | `,`                 | 44          | Comma                 |
| 4         | `;`                 | 59          | Semicolon             |
| 5         | `\`                 | 92          | Backslash             |
| 6         | `|`                 | 124         | Pipe symbol           |
| 7         | `<`                 | 60          | Less-than symbol      |
| 8         | `>`                 | 62          | Greater-than symbol   |
| 9         | ``` ` ```           | 96          | Backtick / Grave accent |
| 10        | `\t` (tab)          | 9           | Horizontal Tab        |
| 11        | `\n` (newline)      | 10          | Line Feed             |
| 12        | `\r` (car. ret)     | 13          | Carriage Return       |

Note: The R-Set characters `<` (ASCII 60, R-Set Index 7) and `>` (ASCII 62, R-Set Index 8) have different positions (values) within Alphabet N versus Alphabet Z. Specifically:
* In Alphabet Z (z85): `<` is at index 73, `>` is at index 74.
* In Alphabet N: `<` is at index 70 (value of `*` in Z), `>` is at index 62 (value of `.` in Z), while `_` is at index 73 and `~` is at index 74.
An encoder checks for the ASCII values of R-Set characters in the input. If an R-Set character with conceptual R-Set Index `j` is found and its corresponding mask bit `j` is set, it is replaced by `allowedPassthroughSafeReplacementCharacters[j]`.

### 4.2. Allowed Passthrough-Safe Replacement Characters
A fixed, ordered list of 13 "allowed passthrough-safe" special characters is defined. These characters are used to replace R-Set characters: when an R-Set character from Section 4.1 (with R-Set Index `j`) is activated via the signal mask and present in the input, it is replaced by the `j`-th character from this list.

This list is derived from the 23 special characters present in the active alphabet (characters at indices 62-84 of the strings defined in Section 4). The derivation is as follows for each active alphabet (N or Z):
1. Identify the 23 special characters from the active alphabet string (characters at indices 62-84).
2. Exclude the active variant's designated fixed Escape Character (this is the character at index 74 of the active alphabet: `~` for N, `>` for Z).
3. Exclude three additional predefined characters based on their common forms:
    * The character `.` (period, at index 62 of the active alphabet).
    * The character `-` (hyphen, at index 63 of the active alphabet).
    * The character at index 73 of the active alphabet (`_` for N, `<` for Z).
4. The first 13 characters from the remaining list of special characters, ordered by their original index in the active alphabet, form the "allowed passthrough-safe replacement characters".

Due to the consistent positions of `.`, `-`, and the characters at indices 73 and 74 within both Alphabet N and Alphabet Z (relative to the start of the special character block at index 62), this process yields the **same ordered list of 13 characters** for both alphabets.

The common ordered list of 13 "allowed passthrough-safe replacement characters" is:
1.  `:` (character at index 64 of the main alphabets)
2.  `+` (character at index 65 of the main alphabets)
3.  `=` (character at index 66 of the main alphabets)
4.  `^` (character at index 67 of the main alphabets)
5.  `!` (character at index 68 of the main alphabets)
6.  `/` (character at index 69 of the main alphabets)
7.  `*` (character at index 70 of the main alphabets)
8.  `?` (character at index 71 of the main alphabets)
9.  `&` (character at index 72 of the main alphabets)
10. `(` (character at index 75 of the main alphabets)
11. `)` (character at index 76 of the main alphabets)
12. `[` (character at index 77 of the main alphabets)
13. `]` (character at index 78 of the main alphabets)

(The characters `{`, `}`, `@`, `%`, `$`, `#` from the main alphabet's special characters block, as well as the initially excluded `.`, `-`, `_` (for N) / `<` (for Z), are not part of this replacement list and are thus not specially escaped unless they are the active escape character itself).

### 4.3. Fixed Escape Character
* For Alphabet N, the character `~` (at index 74 of Alphabet N) SHALL be the fixed escape character.
* For Alphabet Z, the character `>` (at index 74 of Alphabet Z) SHALL be the fixed escape character.

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
    a.  The 13-bit `RSetIndividualActivationMask` SHALL be determined as follows: for each R-Set character (identified by its R-Set Index `j` from 0 to 12, as defined in Section 4.1), the corresponding bit `j` in the `RSetIndividualActivationMask` SHALL be set to 1 if and only if the R-Set character (based on its ASCII value from Section 4.1) is present in the `intermediate_buffer`. Otherwise, the bit SHALL be set to 0.
    b.  A 5-character DP signal is generated (see Section 9 for payload details, including the `RSetIndividualActivationMask`, Mode, and Length).
    c.  The `intermediate_buffer` (referred to as `original_data`) is transformed to `transformed_escaped_data` as follows: For each `char_orig_byte_value` in `original_data` (byte values):
        i.  Iterate `j` from 0 to 12 (R-Set Indices). If `char_orig_byte_value` matches the ASCII value of the R-Set character for R-Set Index `j` (from Section 4.1) AND bit `j` in the `RSetIndividualActivationMask` is set: Append `allowedPassthroughSafeReplacementCharacters[j]` (the character string from Section 4.2) to `transformed_escaped_data`. Mark as replaced and proceed to next `char_orig_byte_value`.
        ii. If not replaced above:
            1.  If `char_orig_byte_value` is the byte value of the `activeFixedEscapeCharacter` (Section 4.3) OR `char_orig_byte_value` is the byte value of one of the 13 `allowedPassthroughSafeReplacementCharacters` (Section 4.2): Append the `activeFixedEscapeCharacter` (string) to `transformed_escaped_data`, then append `chr(char_orig_byte_value)`.
            2.  Else: Append `chr(char_orig_byte_value)` directly to `transformed_escaped_data`.
    d.  The DP signal followed by `transformed_escaped_data` is written to the output stream.
    e.  The `intermediate_buffer` is cleared.
Input bytes not processed as part of a DP sequence are handled via Block Mode (accumulated and encoded in 4-byte quanta or as partial blocks at stream end).

### 6.1. Required State Information
* Configuration Information: `encoding_variant` ('N' or 'Z'), `z85Strict` flag, `activeMainAlphabet` (the 85-character string for the active variant from Section 4), the ordered list of 13 `allowedPassthroughSafeReplacementCharacters` (as strings), `activeFixedEscapeCharacter` (as string).
* Dynamic State Information: `intermediate_buffer`.

### 6.2. Constants
* `MIN_PASSTHROUGH_BYTES` = 20
* `MAX_PASSTHROUGH_BYTES` = 1020. This maximum length is chosen as it is a multiple of 5, aligning with the 5-byte block granularity used for encoding the length parameter within the DP signal (see Section 9). Data chunks for DP MUST have a length that is a multiple of 5, within the range [`MIN_PASSTHROUGH_BYTES`, `MAX_PASSTHROUGH_BYTES`].

### 6.3. Main Encoding Logic (Conceptual)
The encoder manages an `intermediate_buffer`. It attempts to apply DP when the buffer meets `MIN_PASSTHROUGH_BYTES` (and its length is a multiple of 5) and is not in `z85Strict` mode. The `RSetIndividualActivationMask` is determined based on buffer content by checking for the presence of each of the 13 R-Set characters (by their ASCII values). If DP is applied, a signal is generated, R-Set characters (activated by their corresponding mask bit and present in buffer) are replaced using direct index mapping to the 13 replacement characters, necessary original characters are escaped, and the transformed buffer is output. Otherwise, standard Base85N Block Mode encoding is used.

## 7. Decoding Algorithm
A Base85N decoder processes an input stream:
* Read up to 5 input characters. If EOF or insufficient characters for a final block, handle error or end.
* Convert the 5 characters to `decodedValue` using `Base85DigitsToValue` (using `CharToValue` which references Section 4 alphabets).
* If $0 \le \text{decodedValue} < 2^{32}$: It's a standard Base85 block. Convert to 4 bytes, append to output.
* If $\text{decodedValue} \ge 2^{32}$: It's a DP signal.
    a.  Calculate `SignalPayload` = `decodedValue` - $2^{32}$.
    b.  Validate `SignalPayload` against the defined range (0 to $2^{22}-1$). If out of range, it's an error or a reserved value.
    c.  Extract `RSetIndividualMask_13bit`, `Mode_1bit`, and `Length_8bit_encoded_value` from `SignalPayload` as per Section 9.
    d.  Determine `activeFixedEscapeCharacter` (string) and the ordered list of 13 `allowedPassthroughSafeReplacementCharacters` (strings) based on `Mode_1bit`.
    e.  Calculate the actual byte length of the DP data from `Length_8bit_encoded_value`. Read that many characters from the input stream as the `transformed_DP_data`. This length MUST be a multiple of 5.
    f.  Iterate through `transformed_DP_data` to reconstruct the original byte sequence:
        i.  If the current character is the `activeFixedEscapeCharacter`, the next character is taken literally (as a byte), appended to the output, and the input pointer is advanced by two characters.
        ii. Else, let the current character be C. Check if C is the `p`-th character (0-indexed) in the `allowedPassthroughSafeReplacementCharacters` list (i.e., `C == allowedPassthroughSafeReplacementCharacters[p]`).
            1.  If yes, AND `p < 13` (i.e., `p` is a valid R-Set Index), AND bit `p` in the `RSetIndividualMask_13bit` (from the signal) is set: Append the ASCII value of `RSetCharacter[p]` (the R-Set character with R-Set Index `p` as defined by its ASCII value in Section 4.1) to output as a byte. Advance input pointer by one.
            2.  Else (C is not in the list, or `p >= 13`, or bit `p` in the mask is not set): C MUST be treated as a literal character. Append its byte value (`ord(C)`) to output. Advance input pointer by one.
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
        * `Mode_1bit` (Bit 8 of `SignalPayload`): 0 for Alphabet-N, 1 for Alphabet-Z.
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
* **Default Base85N Mode:** Uses Alphabet-N (as defined in Section 4), new DP logic with 13 R-Set characters individually activated (using direct index mapping to the 13 replacement characters) and `~` escape.
* **z85 Alphabet Mode:** Uses Alphabet-Z (as defined in Section 4, which is identical to z85 standard alphabet), new DP logic with 13 R-Set characters individually activated (using direct index mapping to the 13 replacement characters) and `>` escape. Output uses Z-alphabet characters but may include DP signals.
* **z85 Strict Mode:** `encoding_variant` is 'Z', `z85Strict` is true. DP encoding mode MUST be disabled. Output is pure z85 (using Alphabet Z).

## Appendix A: Helper Functions and Methods (Conceptual Descriptions)
(This appendix describes the conceptual operation of functions and methods that would be part of an implementation.)

### A.1 External Helper Functions (Assumed to exist)
* `CharToValue(char_c, variant)`: Converts a character `char_c` to its Base85N integer value (0-84) based on the `variant` (N or Z, using alphabets from Section 4). Returns error indicator if `char_c` is not in the specified variant's alphabet.
* `ValueToChar(value_v, variant)`: Converts a Base85N integer value `value_v` (0-84) to its character representation for the `variant` (using alphabets from Section 4).
* `GetAllowedPassthroughSafeReplacementCharacters(variant)`: Returns the ordered list of 13 "allowed" special character strings (as per Section 4.2).
* `GetActiveFixedEscapeCharacter(variant)`: Returns the character at index 74 of the active alphabet string (`~` for N, `>` for Z).
* `EncodeStandardBlocks(byte_sequence, variant, outputStream)`: Implements standard Base85 encoding for full 4-byte quanta and final partial blocks.

### A.2 Encoder Class Methods (Conceptual for Base85NEncoder)
* `constructor(variant, z85Strict)`: Initializes configuration by loading the correct alphabet string (N or Z from Section 4), the list of 13 allowed replacement characters (Section 4.2), and the active escape character (Section 4.3) based on the `variant`.
* `generateDPSignalPayload(RSetIndividualMask_13bit, Mode_1bit, Length_8bit_encoded_value)`: Calculates the 22-bit `SignalPayload` integer value according to the bit-shifting and ORing logic defined in Section 9.
* `transformAndEscapeBufferForDP(original_buffer_bytes, RSetIndividualActivationMask, variant)`:
    * Retrieves the list of 13 `allowedPassthroughSafeReplacementCharacters` (as strings or their byte values) and `activeFixedEscapeCharacter` (as string or its byte value) for the `variant`.
    * Retrieves the R-Set character ASCII values (from Section 4.1).
    * Initializes an empty list for `transformed_escaped_chars`.
    * Iterates through `original_buffer_bytes` byte by byte (`byte_val`):
        a.  Iterate `j` from 0 to 12. If `byte_val` matches `RSetCharacterASCIIValue[j]` AND bit `j` in `RSetIndividualActivationMask` is set: Append the string `allowedPassthroughSafeReplacementCharacters[j]` to `transformed_escaped_chars`. Mark as replaced and break inner loop.
        b.  If not replaced:
            i.  If `byte_val` is the byte value of `activeFixedEscapeCharacter` OR `byte_val` is the byte value of one of the 13 `allowedPassthroughSafeReplacementCharacters`: Append the string `activeFixedEscapeCharacter` to `transformed_escaped_chars`, then append `chr(byte_val)`.
            ii. Else: Append `chr(byte_val)` to `transformed_escaped_chars`.
    * Returns `"".join(transformed_escaped_chars)`.
* `encodeStream(inputStream, outputStream)`: (As described previously, managing `intermediate_buffer`, deciding between DP/Block mode, calling helpers).

### A.3 Decoder Helper Concepts (Conceptual for Base85NDecoder)
* `decodeDPData(transformed_data_chars_string, RSetIndividualActivationMask, variant)`:
    * Retrieves `activeFixedEscapeCharacter` (string) and the list of 13 `allowedPassthroughSafeReplacementCharacters` (strings) for the `variant`.
    * Retrieves R-Set character ASCII values (Section 4.1).
    * Initializes an empty `original_data_bytes` (bytearray).
    * Iterates through `transformed_data_chars_string` using an input pointer:
        a.  If the character at the pointer is `activeFixedEscapeCharacter`: Increment pointer, append the byte value of the character at the new pointer position to `original_data_bytes`. Increment pointer again.
        b.  Else (character is not an escape prefix): Let current char be C (string).
            i.  Check if C is the `p`-th character (0-indexed) in the `allowedPassthroughSafeReplacementCharacters` list.
            ii. If yes, AND `p < 13`, AND bit `p` in `RSetIndividualActivationMask` is set: Append `RSetCharacterASCIIValue[p]` (from Section 4.1) to `original_data_bytes`.
            iii.Else: Append `ord(C)` (byte value of C) to `original_data_bytes`.
            iv. Increment pointer.
    * Returns `original_data_bytes`.
