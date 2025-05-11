# Base85N Encoding Specification – Draft – May 11, 2025

## 1. Abstract
This document defines Base85N, a binary-to-text encoding scheme using an 85-character alphabet selected for broad compatibility. It features a 4-byte-to-5-character core conversion mechanism. An enhanced Dynamic Passthrough (DP) mode allows for more efficient and readable representation of byte sequences that are largely compatible with the main alphabet but may contain certain predefined R-Set characters. DP mode uses a 13-bit signaling mechanism to deterministically activate individual R-Set characters for substitution. If an activated R-Set character (with R-Set Index `j`) is present in the input, it is replaced by the `j`-th character from a defined list of 13 "passthrough-safe" special characters. A fallback to standard Base85 block encoding (Block Mode) is used when DP is unsuitable according to fixed rules. It supports padding-free encoding and decoding, and interoperability with z85 data. The DP signaling mechanism (part of a 5-character Base85 sequence where the decoded value is $\ge 2^{32}$) encodes the R-Set individual activation mask, active alphabet variant (N/Z), and the length of the encoded output data.

## 2. Introduction
Binary data, such as cryptographic keys, identifiers, or media files, often needs representation as text. Common formats like JSON, XML, HTML, etc., often impose character set restrictions. Base64 is common but inefficient (approx. 33% overhead). Base85 variants are denser but often use problematic characters.
This specification defines Base85N, aiming for high efficiency combined with a broadly compatible alphabet (Alphabet-N). It includes a Dynamic Passthrough (DP) mechanism intended to represent byte sequences containing predefined R-Set characters by replacing them with characters from a "passthrough-safe" subset of 13 special characters from the main alphabet if those R-Set characters are individually activated. This can improve efficiency and readability for suitable data compared to pure Base85 block encoding. DP commitment requires a minimum length (`MIN_PASSTHROUGH_BYTES`).

### 2.1. Key Features and Rationale
* **High Data Density:** Core 4-byte to 5-character Base85 conversion offers better density than Base64.
* **Protocol-Friendly Alphabet:** Alphabet-N, based on z85 with minor modifications, aims for broad compatibility.
* **Dynamic Passthrough (DP):** Attempts to directly represent sequences of bytes. If R-Set characters (with R-Set Index `j`) are present, and they are individually activated via a deterministically calculated signal mask, they are replaced by the `j`-th "passthrough-safe" special character from a defined list of 13. This is intended to keep text-like data readable and easily embeddable.
* **Block Mode Fallback:** If `z85Strict` mode is active, or if the data chunk does not meet the minimum length requirement for DP (`MIN_PASSTHROUGH_BYTES`), or if the resulting output length in DP mode is not suitable for encoding in the DP signal, encoding reverts to standard Base85 block processing. Otherwise, Dynamic Passthrough mode (with a deterministically calculated `RSetIndividualActivationMask`) is used.
* **Adaptive Mode Logic:** The encoding process dynamically selects between DP and Block modes based on the fixed rules defined herein.
* **Padding-Free:** Handles input of any length using standard Base85 partial block encoding in Block mode.
* **z85 Interoperability and Decoder Flexibility:** Decoders are designed to handle character sequences from both Alphabet N and Alphabet Z seamlessly for standard blocks by mapping characters unique to N or Z (like `_`, `~`, `<`, `>`) to their respective numeric values. Dynamic Passthrough (DP) blocks explicitly signal their mode (N or Z). This ensures robust decoding of z85 data and Base85N data. Encoders offer modes for producing z85 or Base85N (Alphabet N) compliant output.
* **Fixed Escape Character:** `~` (tilde) is the designated escape character for Alphabet N. `>` (greater than) is the designated escape character for Alphabet Z. This escape character is used to literally represent itself or certain replacement characters under specific conditions (see Section 4.3).

## 3. Conformance Requirements
The keywords "MUST", "MUST NOT", "REQUIRED", "SHALL", "SHALL NOT", "SHOULD", "SHOULD NOT", "RECOMMENDED", "MAY", and "OPTIONAL" are to be interpreted as described in RFC 2119.

## 4. Alphabets
Base85N defines two primary 85-character alphabets: Variant Z and Variant N. Each character is assigned a unique integer value from 0 to 84. Alphabet Z is identical to the standard z85 encoding alphabet. Alphabet N is derived from Alphabet Z by changing two characters at specific positions. The character assignments for each alphabet, corresponding to their integer values (indices), are presented below.

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

`ALPHABET_Z_CHARS_STR = '0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ.-:+=^!/*?&<>()[]{}@%$#'`

`ALPHABET_N_CHARS_STR = '0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ.-:+=^!/*?&_~()[]{}@%$#'`
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
A fixed, ordered list of 13 "allowed passthrough-safe" special characters is defined. These characters are used to replace R-Set characters: when an R-Set character from Section 4.1 (with R-Set Index `j`) is activated via the signal mask and present in the input, it is replaced by the `j`-th character from this list. This list is derived from the 23 special characters present in the active alphabet (characters at indices 62-84 of the strings defined in Section 4). The derivation is as follows for each active alphabet (N or Z):
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

(The characters `{`, `}`, `@`, `%`, `$`, `#` from the main alphabet's special characters block, as well as the initially excluded `.`, `-`, `_` (for N) / `<` (for Z), are not part of this replacement list and are thus not specially escaped unless they are the active escape character itself, or meet the conditions in Section 4.3.b).

### 4.3. Fixed Escape Character
* For Alphabet N, the character `~` (at index 74 of Alphabet N) SHALL be the fixed escape character.
* For Alphabet Z, the character `>` (at index 74 of Alphabet Z) SHALL be the fixed escape character.
The active escape character is used in DP mode to represent:
a) Literal occurrences of the escape character itself.
b) Literal occurrences of any of the 13 "allowed passthrough-safe replacement characters" (defined in Section 4.2) *if these characters appear in the original byte sequence AND the specific replacement character (identified by its R-Set Index `j`) is actively being used in the current Dynamic Passthrough block (i.e., bit `j` corresponding to that R-Set Index is set in the `RSetIndividualActivationMask` for that block)*.

## 5. Endianness
All conversions between multi-byte integers and byte sequences MUST use Big-Endian byte order.

## 6. Encoding Algorithm
The encoding algorithm processes an input byte stream. Key parameters include the active `encoding_variant` ('N' or 'Z') and the `z85Strict` flag. An `intermediate_buffer` accumulates bytes.
When `intermediate_buffer` is considered for DP (e.g., upon reaching `MIN_PASSTHROUGH_BYTES`, or at end-of-stream for remaining buffer content):
* If `z85Strict` is true or the length of the `intermediate_buffer` is less than `MIN_PASSTHROUGH_BYTES`, the buffer contents SHALL be processed using Block Mode.
* Otherwise (Dynamic Passthrough mode is applied):
    a. The 13-bit `RSetIndividualActivationMask` SHALL be determined as follows: for each R-Set character (identified by its R-Set Index `j` from 0 to 12, as defined in Section 4.1), the corresponding bit `j` in the `RSetIndividualActivationMask` SHALL be set to 1 if and only if the R-Set character (based on its ASCII value from Section 4.1) is present in the `intermediate_buffer`. Otherwise, the bit SHALL be set to 0.
    b. The `intermediate_buffer` (referred to as `original_data`) SHALL be transformed to a temporary `transformed_escaped_data_candidate` string as follows: For each `char_orig_byte_value` in `original_data` (byte values):
        i. Iterate `j` from 0 to 12 (R-Set Indices). If `char_orig_byte_value` matches the ASCII value of the R-Set character for R-Set Index `j` (from Section 4.1) AND bit `j` in the `RSetIndividualActivationMask` is set: Append `allowedPassthroughSafeReplacementCharacters[j]` (the character string from Section 4.2) to `transformed_escaped_data_candidate`. Mark as replaced and proceed to next `char_orig_byte_value`.
        ii. If `char_orig_byte_value` was not replaced in step (i) (i.e., it's not an R-Set character that is active in the `RSetIndividualActivationMask` and present in the input):
            1.  Determine if `char_orig_byte_value` needs escaping. It needs escaping if:
                a.  `char_orig_byte_value` is the byte value of the `activeFixedEscapeCharacter` (Section 4.3).
                OR
                b.  `char_orig_byte_value` corresponds to the byte value of one of the 13 `allowedPassthroughSafeReplacementCharacters` (let this specific character be `allowedPassthroughSafeReplacementCharacters[k]` for some R-Set Index `k` between 0 and 12), AND bit `k` of the `RSetIndividualActivationMask` (determined in step a) is set to 1. This condition ensures that the literal replacement character is only escaped if it's also actively being deployed as a substitute for an R-Set character in the current DP block.
            2.  If it needs escaping (based on the conditions in step ii.1.a or ii.1.b): Append the `activeFixedEscapeCharacter` (string) to `transformed_escaped_data_candidate`, and then append `chr(char_orig_byte_value)` (the original character itself).
            3.  Else (if it does not need escaping): Append `chr(char_orig_byte_value)` directly to `transformed_escaped_data_candidate`.
    c. Let `L_output_chars` be the length in characters of `transformed_escaped_data_candidate`.
    d. An `Length_8bit_encoded_value` ($L_{enc}$) SHALL be calculated if possible: $L_{enc} = (L_{output\_chars} / 5) - 4$.
    e. If $L_{output\_chars}$ is a multiple of 5, $20 \le L_{output\_chars} \le 1020$, and $0 \le L_{enc} \le 200$:
        i. A 5-character DP signal SHALL be generated (see Section 9 for payload details, including the `RSetIndividualActivationMask`, Mode, and this $L_{enc}$ as `Length_8bit_encoded_value`).
        ii. The DP signal followed by `transformed_escaped_data_candidate` SHALL be written to the output stream.
        iii. The `intermediate_buffer` SHALL be cleared.
    f. Else (if the `transformed_escaped_data_candidate` length is not suitable for DP signal encoding, i.e., it does not meet the criteria in step e):
        i. The contents of the `intermediate_buffer` SHALL be processed using Block Mode.
        ii. The `intermediate_buffer` SHALL be cleared.
Input bytes not processed as part of a DP sequence are handled via Block Mode (accumulated and encoded in 4-byte quanta or as partial blocks at stream end).

### 6.1. Required State Information
* Configuration Information: `encoding_variant` ('N' or 'Z'), `z85Strict` flag, `activeMainAlphabet` (the 85-character string for the active variant from Section 4), the ordered list of 13 `allowedPassthroughSafeReplacementCharacters` (as strings), `activeFixedEscapeCharacter` (as string).
* Dynamic State Information: `intermediate_buffer`.

### 6.2. Constants
* `MIN_PASSTHROUGH_BYTES` = 20 (This refers to the minimum original input length in bytes to attempt DP processing).
The length of the `transformed_escaped_data` in DP mode (referred to as `L_output_chars` in Section 6.c) MUST be a multiple of 5. The encodable range for `L_output_chars`, as defined by the `Length_8bit_encoded_value` in the DP signal (see Section 9), is from 20 to 1020 characters, inclusive. An encoder MUST ensure these conditions for the output length if DP mode is used.

### 6.3. Main Encoding Logic (Conceptual)
The encoder manages an `intermediate_buffer`. It attempts to apply DP when the buffer meets `MIN_PASSTHROUGH_BYTES` (and is not in `z85Strict` mode). The `RSetIndividualActivationMask` is determined. The buffer is transformed into a candidate output string. If this candidate string's length is suitable for DP signal encoding (correct multiple of 5 and within encodable range), a DP signal (including the output length) is generated and prepended to the transformed string for output. Otherwise, standard Base85N Block Mode encoding is used for the original buffer contents.

## 7. Decoding Algorithm

### 7.1. General Decoding Principles
A Base85N decoder MUST be capable of processing input streams that may contain character sequences corresponding to either Alphabet N or Alphabet Z for standard blocks, without requiring a fixed mode initialization for the entire stream. For Dynamic Passthrough (DP) blocks, the `Mode_1bit` within the DP signal explicitly indicates whether the DP block's parameters and its data portion are to be interpreted according to Alphabet N or Alphabet Z (see Section 7.d). A decoder SHOULD NOT be required to track or validate the consistency of the alphabet mode (N or Z) used across different blocks (standard or DP) within a single stream; the interpretation is block-local (for DP) or character-value based (for standard blocks).

Furthermore, when consuming the input stream, a decoder MUST ignore the following whitespace characters: space (U+0020), horizontal tab (U+0009), line feed (U+000A), and carriage return (U+000D). These characters SHALL be skipped when encountered *between* Base85N characters that form encoding blocks or Dynamic Passthrough (DP) signals and their associated data segments. This rule allows for arbitrary formatting (e.g., line wrapping, indentation) of the encoded Base85N data. This whitespace-ignoring rule does not alter the processing of R-Set characters (which include space, tab, LF, CR as defined in Section 4.1) when they are part of the *original data* being encoded and subsequently represented or replaced within a DP block, nor does it apply to characters taken literally after an escape character within DP data, or to characters that are part of the defined Base85N alphabets themselves.

A Base85N decoder processes an input stream (after accounting for ignorable whitespace as per Section 7.1):
* Read up to 5 Base85N encoding characters. If EOF or insufficient non-whitespace encoding characters for a final block (and not a valid partial block), handle error or end.
* Convert the 5 input characters of the block to their corresponding integer values (0-84). This character-to-value conversion for standard blocks (where the final `decodedValue` will be less than $2^{32}$) SHALL be performed as follows:
    * Characters that are defined at the same position (index) with the same glyph in both Alphabet N and Alphabet Z (as detailed in Section 4) map to that common index.
    * The character `_` (underscore) maps to value 73.
    * The character `~` (tilde) maps to value 74.
    * The character `<` (less-than) maps to value 73.
    * The character `>` (greater-than) maps to value 74.
    Any character encountered in a standard block that is not covered by this combined mapping is considered an invalid character. Once the 5 characters are converted to their 5 integer values, these values are used in the `Base85DigitsToValue` calculation (as defined in Section 8) to obtain the `decodedValue`. A compliant encoder will produce blocks consistently using either N-specific or Z-specific characters for values 73 and 74, not a mix for the same value within a single block.
* If $0 \le \text{decodedValue} < 2^{32}$: It's a standard Base85 block. Convert to 4 bytes, append to output.
* If $\text{decodedValue} \ge 2^{32}$: It's a DP signal.
    a.  Calculate `SignalPayload` = `decodedValue` - $2^{32}$.
    b. Validate `SignalPayload` against the defined range (0 to $2^{22}-1$). If out of range, it's an error or a reserved value.
    c.  Extract `RSetIndividualMask_13bit`, `Mode_1bit`, and `Length_8bit_encoded_value` ($L_{enc}$) from `SignalPayload` as per Section 9.
    d. Determine `activeFixedEscapeCharacter` (string) and the ordered list of 13 `allowedPassthroughSafeReplacementCharacters` (strings) based on `Mode_1bit`.
    e. Calculate the actual character length of the `transformed_DP_data` (let this be `L_output_chars`) from `Length_8bit_encoded_value` ($L_{enc}$) using the formula $L_{output\_chars} = (L_{enc} + 4) \times 5$ (see Section 9). If $L_{enc}$ is outside the valid range (0-200), this indicates an error.
    f. Read `L_output_chars` Base85N encoding characters (skipping ignorable whitespace) from the input stream as the `transformed_DP_data`. This length MUST be a multiple of 5 and within the valid range [20, 1020].
    g. Iterate through `transformed_DP_data` to reconstruct the original byte sequence:
        i. If the current character is the `activeFixedEscapeCharacter`, the next character is taken literally (as a byte), appended to the output, and the input pointer is advanced by two characters.
        ii. Else, let the current character be C. Check if C is the `p`-th character (0-indexed) in the `allowedPassthroughSafeReplacementCharacters` list (i.e., `C == allowedPassthroughSafeReplacementCharacters[p]`).
            1.  If yes, AND `p < 13` (i.e., `p` is a valid R-Set Index), AND bit `p` in the `RSetIndividualMask_13bit` (from the signal) is set: Append the ASCII value of `RSetCharacter[p]` (the R-Set character with R-Set Index `p` as defined by its ASCII value in Section 4.1) to output as a byte. Advance input pointer by one.
            2.  Else (C is not in the list, or `p >= 13`, or bit `p` in the mask is not set): C MUST be treated as a literal character. Append its byte value (`ord(C)`) to output. Advance input pointer by one.
* Handle final partial blocks (if any remaining input is < 5 non-whitespace chars) as standard Base85 partial decoding.

## 8. Value/Digit Conversion
* `ValueToBase85Digits(numericValue, numDigits)`: Converts `numericValue` to fixed number of Base85 digits (0-84). Requires `numericValue` < $85^{\text{numDigits}}$.
* `Base85DigitsToValue(inputDigits, numDigits)`: Converts Base85 digits (0-84) to `numericValue`. $\text{numericValue} = \sum_{i=0}^{\text{numDigits}-1} (\text{inputDigits}[i] \times 85^{(\text{numDigits}-1-i)})$.

## 9. Signal Interpretation and Parameter Encoding
For a 5-character sequence decoded to `decodedValue` (after potential whitespace stripping and character-to-value conversion as per Section 7):
* **Standard Block:** $0 \le \text{decodedValue} < 2^{32}$. These are standard Base85 encoded blocks.
* **Dynamic Passthrough (DP) Signal:** $\text{decodedValue} \ge 2^{32}$. The `SignalPayload` = `decodedValue` - $2^{32}$ encodes the DP parameters.
The total number of bits for these parameters is 22. `SignalPayload` SHALL range from 0 to $2^{22}-1 = 4194303$.
* **Payload Encoding (22 bits total):**
        The 22 bits of `SignalPayload` are structured as: (`RSetIndividualMask_13bit` << 9) | (`Mode_1bit` << 8) | `Length_8bit_encoded_value`
        * `RSetIndividualMask_13bit` (Bits 9-21 of `SignalPayload`): A 13-bit mask. Bit `j` (0-12) corresponds to the R-Set character with R-Set Index `j` (as defined in Section 4.1). If set, this R-Set character, if found in the original data, is subject to replacement by `allowedPassthroughSafeReplacementCharacters[j]`.
        * `Mode_1bit` (Bit 8 of `SignalPayload`): 0 for Alphabet-N, 1 for Alphabet-Z.
        * `Length_8bit_encoded_value` (Bits 0-7 of `SignalPayload`): Encodes the character length of the `transformed_escaped_data` (i.e., the output data characters following the signal) for the DP block. Let this encoded value be $L_{enc}$. The value of $L_{enc}$ MUST be between 0 and 200, inclusive. Values 201 to 255 for $L_{enc}$ are reserved.
        The actual character length of the output data (`ActualOutputCharLength`) is derived from $L_{enc}$ using the formula: $\text{ActualOutputCharLength} = (L_{enc} + 4) \times 5$.
        This means $L_{enc}=0$ corresponds to 20 characters of output data, $L_{enc}=1$ to 25 characters, ..., up to $L_{enc}=200$ for 1020 characters.
        An encoder MUST ensure that the length of the `transformed_escaped_data` for DP, when using this encoding mechanism, is a multiple of 5 and falls within the range [20, 1020] characters (see also Section 6.e).
* **Reserved/Undefined:** Any `decodedValue` where $2^{32} + (2^{22}-1) < \text{decodedValue} < 85^5$. Encoders MUST NOT generate these. Decoders SHOULD report an error.

## 10. Error Handling
Implementations MUST detect and report errors, including:
* Invalid Characters during Decoding (e.g. a character in a standard block not covered by the combined mapping in Section 7, or a character in DP data that does not conform to the signaled alphabet and is not an escape sequence or replacement character; after ignorable whitespace has been skipped).
* Invalid Final Block Length (1 char of non-ignorable data).
* Unexpected End of Stream (while expecting more non-ignorable characters).
* Undefined or Reserved Signal Value Encountered.
* Invalid Dynamic Passthrough Signal Parameters (e.g., `Length_8bit_encoded_value` ($L_{enc}$) is outside the defined range [0, 200] or maps to an output length outside the defined range of 20-1020 characters; or the actual number of characters read for `transformed_DP_data` does not match the signaled length).
* Invalid Partial Block Encoding / Overrun.

## 11. z85 Compatibility and Encoding Modes
* **Default Base85N Mode:** Uses Alphabet-N (as defined in Section 4), new DP logic with 13 R-Set characters individually activated (using direct index mapping to the 13 replacement characters) and `~` escape. Output uses N-alphabet characters (e.g. `_`, `~` for values 73, 74) and DP signals will indicate Mode 0 (N).
* **z85 Alphabet Mode:** Uses Alphabet-Z (as defined in Section 4, which is identical to z85 standard alphabet), new DP logic with 13 R-Set characters individually activated (using direct index mapping to the 13 replacement characters) and `>` escape. Output uses Z-alphabet characters (e.g. `<`, `>` for values 73, 74) and DP signals will indicate Mode 1 (Z).
* **z85 Strict Mode:** `encoding_variant` is 'Z', `z85Strict` is true. DP encoding mode MUST be disabled. Output is pure z85 (using Alphabet Z, including `<` and `>` for values 73, 74).

