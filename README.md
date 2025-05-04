# Base85N Encoding Specification v1.0-Draft

## 1. Abstract

This document defines Base85N, a binary-to-text encoding scheme using an 85-character alphabet selected for broad compatibility. It features a 4-byte-to-5-character block encoding mechanism, a mandated 'Standard Passthrough' mode for efficient handling of suitable byte sequences up to a defined limit, a mandated 'Dynamic Passthrough' mode for efficiently handling sequences containing specific non-alphabet characters, padding-free encoding of final partial blocks, and decoding support for z85 data.

## 2. Introduction

Binary data, such as cryptographic keys, identifiers, or media files, frequently needs to be represented as text for reliable transmission or storage within systems designed primarily for text. Common protocols and formats like JSON, XML, HTML, CSS, HTTP headers, and cookies often impose character set limitations or require escaping for certain characters. While established encoding schemes like Base64 are widely supported, they incur a significant size overhead (approximately 33%). Base85 encoding variants offer higher data density, but often utilize characters (e.g., `"`, `&`, `'`, `<`, `>`, `\`) that are problematic or require escaping when embedded in these common formats.

This specification defines Base85N, a binary-to-text encoding scheme designed to address these challenges. It aims to provide high efficiency, approaching that of raw binary for certain data patterns, combined with an alphabet optimized for broad compatibility across diverse embedding contexts without requiring further escaping.

### 1.1. Key Features and Rationale

Base85N incorporates several key design features to achieve its goals:

* **Mandated Standard Passthrough Mechanism for Efficiency and Readability:** For input data containing sequences suitable for direct transmission (e.g., sections of plain ASCII text consisting only of Base85N characters), Base85N includes a 'Standard Passthrough' mode. This mechanism MUST be used whenever a suitable sequence of sufficient length ($L_{\text{min\_std\_bytes}}$) is encountered (see Section 6.2). It offers significant advantages: drastically reducing encoding overhead, approaching a 1:1 size ratio (plus minimal signalling overhead), and preserving the human readability of embedded text. These benefits are particularly notable compared to continuous block encoding. The maximum length of a single standard passthrough sequence is capped at $L_{\text{max}}$ blocks (4096 bytes) to ensure predictability and limit buffering requirements.
* **Mandated Dynamic Passthrough Mechanism for Mixed Content:** To efficiently handle input sequences containing a mix of characters inside and outside the standard Base85N alphabet (specifically, common non-Base85N ASCII characters like space, quotes, line breaks, etc.), Base85N includes a mandatory 'Dynamic Passthrough' mode (see Section 6.5). When applicable, this mode maps the non-Base85N characters to designated Base85N characters using a dynamically selected map, while passing through existing Base85N characters directly. This improves encoding efficiency for certain types of mixed binary/text data compared to standard block encoding, while maintaining compatibility. The maximum length is also capped at $L_{\text{max\_dyn}}$ bytes (4096 bytes).
* **Padding-Free Design:** Input data whose length is not a multiple of 4 bytes is handled cleanly without requiring padding characters. Base85N encodes the final 1, 2, or 3 bytes into exactly 2, 3, or 4 output characters, respectively. This simplifies encoding and decoding logic and avoids potential issues related to padding handling or data truncation common in other schemes like Base64.
* **Protocol-Friendly Default Alphabet:** The default 85-character alphabet was carefully selected to exclude characters (`"`, `&`, `'`, `;`, `<`, `=`, `>`, `\`, `|`) that frequently cause parsing issues or require escaping when embedded within common formats like JSON string values, HTML/XML attributes, quoted HTTP cookie values, and CSV fields. This improves reliability and simplifies usage in these contexts.
* **z85 Interoperability and Replacement:** Designed for strong interoperability with z85 (ZeroMQ Base-85), Base85N decoders correctly process standard z85-encoded data. Base85N encoders enhance this compatibility by offering selectable modes:
    * **z85 Alphabet Mode:** Encoders can be configured to use the standard z85 character alphabet instead of the Base85N default alphabet. When operating in this mode, the Base85N Standard Passthrough and Dynamic Passthrough mechanisms MUST be applied for all eligible byte sequences according to the rules defined in this specification (including the $L_{\text{max}}$ / $L_{\text{max\_dyn}}$ limits).
    * **z85 Strict Mode:** For complete compatibility, encoders can operate in a 'z85 Strict' mode. This mode uses the z85 alphabet and disables the Base85N Standard and Dynamic Passthrough mechanisms, ensuring the generated output is identical to that of a native z85 encoder for the same input.
        This flexibility, particularly the 'z85 Strict' mode, allows Base85N libraries to serve as direct, feature-compatible replacements for existing z85 implementations.

## 3. Conformance Requirements

The key words "MUST", "MUST NOT", "REQUIRED", "SHALL", "SHALL NOT", "SHOULD", "SHOULD NOT", "RECOMMENDED", "MAY", and "OPTIONAL" are to be interpreted as described in RFC 2119.

## 4. Alphabet

Base85N MUST support the following 85-character alphabets, Variant N and Z. The table shows the character, and its corresponding Base85N numerical value (0-84). `Alphabet-N` refers to the characters in the 'N' column.

| N   | Z   | Wert |
| --- | --- | ---- |
| 0   | 0   | 0    |
| 1   | 1   | 1    |
| 2   | 2   | 2    |
| 3   | 3   | 3    |
| 4   | 4   | 4    |
| 5   | 5   | 5    |
| 6   | 6   | 6    |
| 7   | 7   | 7    |
| 8   | 8   | 8    |
| 9   | 9   | 9    |
| a   | a   | 10   |
| b   | b   | 11   |
| c   | c   | 12   |
| d   | d   | 13   |
| e   | e   | 14   |
| f   | f   | 15   |
| g   | g   | 16   |
| h   | h   | 17   |
| i   | i   | 18   |
| j   | j   | 19   |
| k   | k   | 20   |
| l   | l   | 21   |
| m   | m   | 22   |
| n   | n   | 23   |
| o   | o   | 24   |
| p   | p   | 25   |
| q   | q   | 26   |
| r   | r   | 27   |
| s   | s   | 28   |
| t   | t   | 29   |
| u   | u   | 30   |
| v   | v   | 31   |
| w   | w   | 32   |
| x   | x   | 33   |
| y   | y   | 34   |
| z   | z   | 35   |
| A   | A   | 36   |
| B   | B   | 37   |
| C   | C   | 38   |
| D   | D   | 39   |
| E   | E   | 40   |
| F   | F   | 41   |
| G   | G   | 42   |
| H   | H   | 43   |
| I   | I   | 44   |
| J   | J   | 45   |
| K   | K   | 46   |
| L   | L   | 47   |
| M   | M   | 48   |
| N   | N   | 49   |
| O   | O   | 50   |
| P   | P   | 51   |
| Q   | Q   | 52   |
| R   | R   | 53   |
| S   | S   | 54   |
| T   | T   | 55   |
| U   | U   | 56   |
| V   | V   | 57   |
| W   | W   | 58   |
| X   | X   | 59   |
| Y   | Y   | 60   |
| Z   | Z   | 61   |
| .   | .   | 62   |
| -   | -   | 63   |
| :   | :   | 64   |
| +   | +   | 65   |
| =   | =   | 66   |
| ^   | ^   | 67   |
| !   | !   | 68   |
| /   | /   | 69   |
| * | * | 70   |
| ?   | ?   | 71   |
| \`  | &   | 72   |
| _   | <   | 73   |
| ~   | >   | 74   |
| (   | (   | 75   |
| )   | )   | 76   |
| [   | [   | 77   |
| ]   | ]   | 78   |
| {   | {   | 79   |
| }   | }   | 80   |
| @   | @   | 81   |
| %   | %   | 82   |
| $   | $   | 83   |
| #   | #   | 84   |

## 5. Endianness

All conversions between multi-byte integers and byte sequences MUST use Big-Endian byte order.

## 6. Encoding Algorithm

A function `ValueToChar(value)` MUST return the corresponding character from the currently selected alphabet (N or Z) for a valid input value $v$ ($0 \le v \le 84$).

Input: Byte sequence $B_{\text{in}}$
Output: Encoded stream $S_{\text{out}}$

Constants:
* $L_{\text{min}} = 5$ (minimum number of 4-byte blocks for standard passthrough signal)
* $L_{\text{min\_std\_bytes}} = 20$ (minimum byte length for standard passthrough sequence, $L_{\text{min}} \times 4$)
* $L_{\text{max}} = 1024$ (maximum number of 4-byte blocks for standard passthrough signal, $4096$ bytes)
* $L_{\text{min\_dyn}} = 20$ (minimum number of bytes for dynamic passthrough)
* $L_{\text{max\_dyn}} = 4096$ (maximum number of bytes for dynamic passthrough)
* $Alphabet_R = \{' ':0, '"':1, '&':2, '\'':3, ',':4, ';':5, '<':6, '>':7, '\\':8, '|':9, '\n':10, '\r':11, '\t':12\}$ (Set of characters handled by Dynamic Passthrough, with assigned indices 0-12. Assumes standard ASCII/UTF-8 values for `\n`, `\r`, `\t`).

Processing Steps:
* Initialize $S_{\text{out}} = \text{empty}$, $idx = 0$.
* Processing Loop: While $idx < \text{length}(B_{\text{in}})$:
    a.  **Check for Standard Passthrough Applicability:**
        i.  Determine the maximum number of contiguous 4-byte blocks $L_{\text{contiguous}}$ starting at $idx$ suitable for Standard Base85N Passthrough (Section 6.2).
        ii. If $L_{\text{contiguous}} \ge L_{\text{min}}$:
            * Calculate passthrough length in blocks $L = \min(L_{\text{contiguous}}, L_{\text{max}})$.
            * Proceed to Step 6.1 (Standard Passthrough Output) using length $L$. Continue loop.
    b.  **Check for Dynamic Passthrough Applicability (if Standard Passthrough was not applied):**
        i.  The encoder MUST attempt to use Dynamic Passthrough (Section 6.5) starting at $idx$.
        ii. If Dynamic Passthrough is successfully applied (finds a valid map $n$ and results in a length $L_{\text{dyn}} \ge L_{\text{min\_dyn}}$), proceed as defined in Section 6.5. Continue loop.
    c.  **Standard/Partial Block Encoding (if neither Passthrough was applied):**
        * Proceed to Step 6.3 (Standard/Partial Block Encoding). Continue loop.

### 6.1 Base85N Standard Passthrough Signal and Output

(This step is executed only if $L_{\text{contiguous}} \ge L_{\text{min}}$ as determined in Step 2.a.ii)

* The number of 4-byte blocks $L$ to send as passthrough bytes has been calculated as $L = \min(L_{\text{contiguous}}, L_{\text{max}})$, where $L_{\text{min}} \le L \le L_{\text{max}}$. Let $L_{\text{bytes}} = 4 \times L$. Ensure $idx + L_{\text{bytes}} \le \text{length}(B_{\text{in}})$.
* Calculate signal value $X = 2^{32} + L$. (This ensures $2^{32} + L_{\text{min}} \le X \le 2^{32} + L_{\text{max}}$). The encoder MUST ensure $X < 85^5$ (which holds since $2^{32} + 1024 < 85^5$).
* Convert $X$ to 5 Base85 digits ($s_1$..$s_5$) using `ValueToBase85Digits(X, 5)`. (See Section 8).
* Append the 5 signal characters `ValueToChar`($s_1$)...`ValueToChar`($s_5$) to $S_{\text{out}}$ using the currently selected alphabet.
* Append the next $L_{\text{bytes}}$ from $B_{\text{in}}$ (starting at $idx$) directly to $S_{\text{out}}$. (Note: $S_{\text{out}}$ now contains a mix of Base85N characters and raw bytes).
* Increment $idx$ by $L_{\text{bytes}}$. Continue loop at Step 2.

### 6.2 Definition of 'Suitable' for Standard Passthrough Mode

A sequence of input bytes is considered suitable for the Standard Passthrough mechanism if it meets the following criteria:

* **Character Membership:** Every byte in the sequence must have a value corresponding to the ASCII value of a character that is part of the currently selected Base85N alphabet (either Variant N or Variant Z).
* **Alignment:** The sequence must start at an index $idx$ which is a multiple of 4.
* **Length:** The sequence length must be a multiple of 4 bytes and be at least $L_{\text{min\_std\_bytes}}$ (20 bytes) long.

If a contiguous sequence of input bytes starting at the current position $idx$ (where $idx$ is a multiple of 4) is suitable and has a length corresponding to $L_{\text{contiguous}} \ge L_{\text{min}}$ blocks, the encoder MUST use the Standard Passthrough mechanism as described in Step 6.1.

### 6.3 Standard/Partial Block Encoding

(This step is executed if neither Standard nor Dynamic Passthrough was applied)

* Determine remaining bytes $R = \text{length}(B_{\text{in}}) - idx$.
* If $R \ge 4$:
    a.  Read 4 bytes starting at $idx$.
    b.  Treat these bytes as a 32-bit Big-Endian unsigned integer $N$.
    c.  Convert $N$ to 5 Base85 digits ($d_1$..$d_5$) using `ValueToBase85Digits(N, 5)`. (See Section 8).
    d.  Append the 5 characters `ValueToChar`($d_1$)...`ValueToChar`($d_5$) to $S_{\text{out}}$.
    e.  Increment $idx$ by 4.
* Else ($R$ is 1, 2, or 3 - final partial block):
    a.  Read the remaining $k = R$ bytes.
    b.  Pad these $k$ bytes with $4-k$ zero bytes on the right to form a 4-byte sequence.
    c.  Treat this 4-byte sequence as a 32-bit Big-Endian unsigned integer $N_k$.
    d.  Convert $N_k$ to 5 Base85 digits ($d_1$..$d_5$) using `ValueToBase85Digits(N_k, 5)`.
    e.  Append the first $k+1$ characters `ValueToChar`($d_1$)...`ValueToChar`($d_{k+1}$) to $S_{\text{out}}$.
    f.  Set $idx = \text{length}(B_{\text{in}})$. (End of input).
* Continue loop at Step 2.

### 6.4 Implementation Note on Passthrough Lookahead

* To determine the maximum $L_{\text{contiguous}}$ (for Standard Passthrough) or $L_{\text{potential}}$ (for Dynamic Passthrough), encoders MUST effectively look ahead in the input stream. Implementations may achieve this using internal buffering or other strategies.
* For Standard Passthrough, the lookahead should check for suitability (Section 6.2) for up to $L_{\text{max}}$ blocks ($4 \times L_{\text{max}}$ bytes).
* For Dynamic Passthrough, the lookahead should scan for characters in `Alphabet-N` or $Alphabet_R$ for up to $L_{\text{max\_dyn}}$ bytes.
* The lookahead stops at the respective maximum length, the end of the input stream, or the end of the suitable/applicable sequence, whichever comes first.
* Both Passthrough mechanisms are mandatory; they MUST be triggered whenever their respective conditions (including minimum length) are met. The length $L$ or $L_{\text{dyn}}$ used in the signal MUST be calculated according to the rules (including `min` with max length and stopping conditions). This ensures deterministic output for any given input and encoder configuration (alphabet choice).

### 6.5 Base85N Dynamic Passthrough

This mechanism provides an alternative way to handle sequences of bytes that contain characters not suitable for Standard Passthrough (Section 6.2), specifically those listed in $Alphabet_R$. It works by mapping these $Alphabet_R$ characters to characters within the `Alphabet-N` (using the N variant) based on a dynamically selected map, while passing through other characters directly.

#### 6.5.1 Applicability and Map Selection

* This mode is considered at index $idx$ only if Standard Passthrough (Section 6.1) was not applicable (i.e., $L_{\text{contiguous}} < L_{\text{min}}$).
* The encoder scans the input $B_{\text{in}}$ starting from $idx$ to identify the longest potential sequence $S$ (up to $L_{\text{max\_dyn}}$ bytes or end-of-input) containing only bytes whose values correspond to characters in `Alphabet-N` or $Alphabet_R$. Let the length of this potential sequence be $L_{\text{potential}}$.
* If $L_{\text{potential}} < L_{\text{min\_dyn}}$, Dynamic Passthrough is not used; proceed to Section 6.3.
* If $L_{\text{potential}} \ge L_{\text{min\_dyn}}$, the encoder attempts to find a valid mapping round $n$:
    a.  Iterate $n$ from 0 to 4.
    b.  For each $n$, define the set of potential replacement characters using the N alphabet:
        `PotentialReplacements`($n$) $= \{ \text{ValueToChar}( (Alphabet_R[c] + n) \pmod{85} ) \text{ for } c \text{ in } Alphabet_R \}$
    c.  **Initial Collision Check:** Scan the potential sequence $S$. If any character $c_s$ in $S$ *which is not in $Alphabet_R$* is found to be present in the set `PotentialReplacements`($n$), then map $n$ is invalid for this sequence $S$. Continue to the next $n$.
    d.  If a map $n$ passes the collision check, it is selected as the $valid\_n$. Stop iterating $n$.
* If no $valid\_n$ is found ($n=0..4$ all fail the collision check), Dynamic Passthrough is not used; proceed to Section 6.3.

#### 6.5.2 Determining Length and Stopping Condition

* Once a $valid\_n$ is selected, the encoder determines the actual passthrough length $L_{\text{dyn}}$. It scans the input $B_{\text{in}}$ starting at $idx$, up to a maximum of $\min(L_{\text{potential}}, L_{\text{max\_dyn}})$ bytes.
* **Stopping Condition:** The scan stops *before* processing byte $b$ if the character $c$ corresponding to $b$ is found within the set `PotentialReplacements`($valid\_n$).
* The final length $L_{\text{dyn}}$ is the number of bytes successfully scanned before the stopping condition was met, or before exceeding limits.
* If the final $L_{\text{dyn}} < L_{\text{min\_dyn}}$, Dynamic Passthrough is abandoned for this attempt; proceed to Section 6.3.

#### 6.5.3 Dynamic Passthrough Signal and Output

* If a $valid\_n$ was found and $L_{\text{dyn}} \ge L_{\text{min\_dyn}}$:
    a.  **Calculate Signal Value:** Determine the signal value $X_{\text{dyn}}$ which encodes both the map $valid\_n$ ($0 \le valid\_n \le 4$) and the length $L_{\text{dyn}}$ ($L_{\text{min\_dyn}} \le L_{\text{dyn}} \le L_{\text{max\_dyn}}$).
        * Let $X_{\text{start\_dyn}} = (2^{32} + L_{\text{max}}) + 1$.
        * Let $L_{\text{range\_dyn}} = L_{\text{max\_dyn}} - L_{\text{min\_dyn}} + 1$. (This value is 4077).
        * Let $L_{\text{index}} = L_{\text{dyn}} - L_{\text{min\_dyn}}$. (This ranges from 0 to 4076).
        * Calculate $X_{\text{dyn}} = X_{\text{start\_dyn}} + (valid\_n \times L_{\text{range\_dyn}}) + L_{\text{index}}$.
        * The encoder MUST ensure $X_{\text{dyn}} < 85^5$.
    b.  **Convert and Append Signal:** Convert $X_{\text{dyn}}$ to 5 Base85 digits ($s_1$..$s_5$) using `ValueToBase85Digits`($X_{\text{dyn}}$, 5). Append the 5 signal characters `ValueToChar`($s_1$)...`ValueToChar`($s_5$) to $S_{\text{out}}$ using the currently selected alphabet.
    c.  **Append Encoded Data:** Iterate through the $L_{\text{dyn}}$ bytes of the input sequence starting at $idx$. For each byte $b$:
        i.  Let $c_{\text{in}}$ be the character corresponding to the value of $b$.
        ii. If $c_{\text{in}}$ is in $Alphabet_R$: Calculate the mapped value $mapped\_val = (Alphabet_R[c_{\text{in}}] + valid\_n) \pmod{85}$. Append `ValueToChar`($mapped\_val$) to $S_{\text{out}}$.
        iii. Else ($c_{\text{in}}$ must be in `Alphabet-N`): Append $c_{\text{in}}$ directly to $S_{\text{out}}$.
    d.  Increment $idx$ by $L_{\text{dyn}}$. Continue the main processing loop (Step 2 in Section 6).

## 7. Decoding Algorithm

A mapping function `CharToValue(c)` MUST return the integer value (0-84) corresponding to the input character `c` by checking against both Variant N and Variant Z alphabets, or an error indicator for invalid input characters `c` (characters not present in either alphabet).

Input: Encoded stream $S_{\text{in}}$ (can be a mix of Base85N characters and raw bytes from Standard Passthrough, or encoded characters from Dynamic Passthrough)
Output: Byte sequence $B_{\text{out}}$

Constants: (Repeated for clarity)
* $L_{\text{min}} = 5$
* $L_{\text{max}} = 1024$
* $L_{\text{min\_dyn}} = 20$
* $L_{\text{max\_dyn}} = 4096$
* $Alphabet_R$ (as defined in Section 6)

Processing Steps:
* Initialize $B_{\text{out}} = \text{empty}$, $idx = 0$.
* While $idx < \text{len}(S_{\text{in}})$:
    a.  Check Remaining Length: Determine remaining characters/bytes $R = \text{len}(S_{\text{in}}) - idx$.
    b.  If $R < 2$: If $R == 1$, report error (invalid final block length). If $R == 0$, break loop (end of input).
    c.  Read Potential Block/Signal: Read the next 5 characters from $S_{\text{in}}$ starting at $idx$. Let these be $c_1$...$c_5$. If $R < 5$, read only $R$ characters.
    d.  Convert to Digits: Attempt to convert $c_1$...$c_{\min(R, 5)}$ to their corresponding numerical values $d_1$...$d_{\min(R, 5)}$ using `CharToValue`. If any character is invalid, report error.
    e.  Process Based on Length:
        i.  If $R \ge 5$:
            * Convert the 5 digits $d_1$...$d_5$ to an integer $X$ using `Base85DigitsToValue`($[d_1..d_5]$, 5). (See Section 8).
            * Define signal range boundaries:
                * $X_{\text{min\_std}} = 2^{32} + L_{\text{min}}$
                * $X_{\text{max\_std}} = 2^{32} + L_{\text{max}}$
                * $X_{\text{start\_dyn}} = X_{\text{max\_std}} + 1$
                * $L_{\text{range\_dyn}} = L_{\text{max\_dyn}} - L_{\text{min\_dyn}} + 1$ (4077)
                * $X_{\text{max\_dyn}} = X_{\text{start\_dyn}} + (4 \times L_{\text{range\_dyn}}) + (L_{\text{max\_dyn}} - L_{\text{min\_dyn}})$

            * **Check for Standard Passthrough Signal:** If $X_{\text{min\_std}} \le X \le X_{\text{max\_std}}$:
                * Calculate passthrough length in blocks $L = X - 2^{32}$.
                * Let $L_{\text{bytes}} = 4 \times L$.
                * Check if $idx + 5 + L_{\text{bytes}} \le \text{len}(S_{\text{in}})$. If not, report error (unexpected EOF during raw data read).
                * Append the $L_{\text{bytes}}$ directly following the 5 signal characters ($S_{\text{in}}[idx+5 : idx+5+L_{\text{bytes}}]$) to $B_{\text{out}}$.
                * Increment $idx$ by $5 + L_{\text{bytes}}$. Continue loop.
            * **Check for Dynamic Passthrough Signal:** If $X_{\text{start\_dyn}} \le X \le X_{\text{max\_dyn}}$:
                * Calculate the offset: $Offset = X - X_{\text{start\_dyn}}$.
                * Decode map number: $n = Offset \ // \ L_{\text{range\_dyn}}$.
                * Decode length index: $L_{\text{index}} = Offset \pmod{L_{\text{range\_dyn}}}$.
                * Calculate final length: $L_{\text{dyn}} = L_{\text{index}} + L_{\text{min\_dyn}}$.
                * Perform sanity checks: Ensure $0 \le n \le 4$ and $L_{\text{min\_dyn}} \le L_{\text{dyn}} \le L_{\text{max\_dyn}}$. If checks fail, report error: Invalid Dynamic Passthrough signal value.
                * Check if $idx + 5 + L_{\text{dyn}} \le \text{len}(S_{\text{in}})$. If not, report error (unexpected EOF reading dynamic data).
                * Read the next $L_{\text{dyn}}$ characters from $S_{\text{in}}$ into $S_{\text{encoded\_dyn}} = S_{\text{in}}[idx+5 : idx+5+L_{\text{dyn}}]$.
                * **Decode Data:** Create the reverse mapping for map $n$. Build $RevMap_n = \{ \text{ValueToChar}( (r\_idx + n) \pmod{85} ) : r\_char \text{ for } r\_char, r\_idx \text{ in } Alphabet_R.items() \}$. (Assumes `ValueToChar` uses N alphabet).
                * Iterate through characters $c_{\text{enc}}$ in $S_{\text{encoded\_dyn}}$:
                    * If $c_{\text{enc}}$ is a key in $RevMap_n$: Append the byte value of $RevMap_n[c_{\text{enc}}]$ to $B_{\text{out}}$.
                    * Else ($c_{\text{enc}}$ should be a direct passthrough character from `Alphabet-N`): Append the byte value of $c_{\text{enc}}$ to $B_{\text{out}}$.
                * Increment $idx$ by $5 + L_{\text{dyn}}$. Continue loop.
            * **Check for Reserved Signal:** If $2^{32} \le X < X_{\text{min\_std}}$: Report error (reserved signal value encountered).
            * **Standard Block or Undefined:** Otherwise:
                * If $X < 2^{32}$: Decode as Standard Block (Convert $X$ to 4 bytes Big-Endian, append to $B_{\text{out}}$). Increment $idx$ by 5. Continue loop.
                * If $X > X_{\text{max\_dyn}}$ (and $X < 85^5$): Report error (Undefined signal value encountered).

        ii. If $R < 5$ (Final Partial Block):
            * Let $k = R - 1$. ($R$ can be 2, 3, or 4, so $k$ is 1, 2, or 3).
            * Convert the $R$ digits $d_1$...$d_R$ to an integer $N_k$ using `Base85DigitsToValue`($[d_1..d_R]$, $R$).
            * Check for Overflow / Invalid Encoding: Convert $N_k$ to 5 Base85 digits ($temp\_d1$..$temp\_d5$) using `ValueToBase85Digits`($N_k$, 5). Calculate the full potential value $N_{\text{full}} = \text{Base85DigitsToValue}([temp\_d1..temp\_d5], 5)$. Convert $N_{\text{full}}$ into 4 bytes using Big-Endian order. Check if the last $4-k$ bytes of this 4-byte sequence are zero. If not, report error (invalid encoding for partial block - value implies non-zero padding bytes).
            * Convert $N_{\text{full}}$ (or $N_k$ padded appropriately) to 4 bytes using Big-Endian order.
            * Append the first $k$ bytes of this 4-byte sequence to $B_{\text{out}}$.
            * Set $idx = \text{len}(S_{\text{in}})$. Break loop (end of input).

## 8. Value/Digit Conversion

These functions define the core base conversion logic.

* **ValueToBase85Digits(Value, NumDigits):**
    * Purpose: Converts a non-negative integer `Value` into a fixed number (`NumDigits`) of Base85 digits (0-84). Used for encoding full blocks (`Value`=$N$, `NumDigits`=5), partial blocks (`Value`=$N_k$, `NumDigits`=5, then truncate), standard passthrough signals (`Value`=$X$, `NumDigits`=5), and dynamic passthrough signals (`Value`=$X_{\text{dyn}}$, `NumDigits`=5).
    * Input: Integer `Value`, Integer `NumDigits` (MUST be $> 0$).
    * Output: Array/List of `NumDigits` integers $d[0..\text{NumDigits}-1]$, where $d[0]$ is the most significant digit.
    * Process: Performs standard base conversion. `Value` MUST be less than $85^{\text{NumDigits}}$. Calculates digits via repeated modulo 85 and integer division by 85. Returns exactly `NumDigits` digits, padding with leading zeros if the calculated number of digits is less than `NumDigits`.


def ValueToBase85Digits(Value, NumDigits):
  # Check required by Section 10
  if Value >= 85**NumDigits:
    raise ValueError("Input value too large for specified number of digits")

  digits = [0] * NumDigits # Initialize with zeros
  tempValue = Value
  for i in range(NumDigits - 1, -1, -1):
    digits[i] = tempValue % 85
    tempValue //= 85
  return digits


* **Base85DigitsToValue(Digits, NumDigits):**
    * Purpose: Converts a sequence of `NumDigits` Base85 digits (0-84) back into a non-negative integer. Used for decoding all encoded block types (full, partial, standard signal, dynamic signal).
    * Input: Array/List of `NumDigits` integers $Digits[0..\text{NumDigits}-1]$, where $Digits[0]$ is most significant. Integer `NumDigits`. Digits MUST be in range $[0, 84]$.
    * Output: Integer `Value`.
    * Process: Performs standard base conversion. Calculates $Value = \sum_{i=0}^{\text{NumDigits}-1} \text{Digits}[i] \times 85^{\text{NumDigits}-1-i}$.

```
def Base85DigitsToValue(Digits, NumDigits):
  value = 0
  for i in range(NumDigits):
    # Assumes digits are already validated (0-84) by caller (CharToValue)
    value = value * 85 + Digits[i]
  return value

```

## 9. Reserved Sequences

The 5-character sequences corresponding to the numerical values $X$ where $2^{32} \le X < 2^{32} + L_{\text{min}}$ (i.e., $X$ from $4,294,967,296$ to $4,294,967,300$ inclusive) MUST NOT be generated by an encoder and MUST be treated as an error if encountered during decoding (Reserved Signal Value).

Sequences decoding to $X$ where $2^{32} + L_{\text{min}} \le X \le 2^{32} + L_{\text{max}}$ signal Standard Passthrough (Section 6.1).

Sequences decoding to $X$ where $(2^{32} + L_{\text{max}}) + 1 \le X \le X_{\text{max\_dyn}}$ (where $X_{\text{max\_dyn}} = (2^{32} + L_{\text{max}}) + 1 + (4 \times 4077) + 4076$) signal Dynamic Passthrough (Section 6.5). The specific value encodes the map $n$ and length $L_{\text{dyn}}$.

Any 5-character sequence decoding to a value $X$ greater than $X_{\text{max\_dyn}}$ and less than $85^5$ is currently undefined and MUST be treated as an error during decoding.

## 10. Error Handling

Implementations MUST detect and report errors including, but not limited to:

* **Invalid Characters During Decoding:** Characters encountered in the input character stream during decoding that are not part of either the Variant N or Variant Z alphabet.
* **Invalid Final Block Length:** An encoded stream ending with only 1 character when at least 2 are required for the smallest partial block ($k=1$ byte $\rightarrow R=2$ chars).
* **Unexpected End of Stream:** The input stream ending prematurely, such as when reading raw bytes indicated by a Standard Passthrough signal, reading characters for Dynamic Passthrough data, or when expecting more characters for a standard or partial block.
* **Reserved Signal Value Encountered:** A 5-character sequence decoding to $X$ where $2^{32} \le X < 2^{32} + L_{\text{min}}$.
* **Undefined Signal Value Encountered:** A 5-character sequence decoding to $X$ where $X > X_{\text{max\_dyn}}$ (as defined in Section 7 and 9) and $X < 85^5$.
* **Invalid Dynamic Passthrough Signal Value:** A 5-character sequence decodes to $X$ within the dynamic passthrough range ($X_{\text{start\_dyn}} \le X \le X_{\text{max\_dyn}}$), but the derived $n$ or $L_{\text{dyn}}$ values are inconsistent or out of bounds (e.g., $n > 4$, or $L_{\text{dyn}}$ outside $[L_{\text{min\_dyn}}, L_{\text{max\_dyn}}]$).
* **Invalid Partial Block Encoding / Overflow:** During decoding of a final partial block ($R$=2, 3, or 4 characters), the decoded value implies non-zero padding bytes would have been present in the original data (See Section 7, Step 2.e.ii check).
* **Value Too Large for Conversion:** The input `Value` provided to `ValueToBase85Digits` is greater than or equal to $85^{\text{NumDigits}}$.

