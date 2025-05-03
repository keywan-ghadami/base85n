
---

# Base85N Encoding Specification v1.0-Draft

## 1. Abstract

This document defines **Base85N**, a binary-to-text encoding scheme extending Base85 principles. 
It utilizes an 85-character alphabet selected for robustness, 
minimizing conflicts when embedded in contexts like JSON, HTTP Cookies, and HTML/XML attributes.

**Base85N features include:**

- Standard Base85-like encoding of 4 input bytes to 5 output characters.
- An adaptive mechanism allowing sequences of raw input bytes to be efficiently represented, bypassing character encoding for those sections.
- Unambiguous handling of final partial input blocks (1, 2, or 3 bytes) through variable-length output sequences without requiring padding markers or complex decoding rules.

---

## 2. Advantages of Base85N

- **High Efficiency via Passthrough**: For input data containing sequences
suitable for direct transmission (e.g., binary blobs, non-conflicting text),
the adaptive Base85N Passthrough mode significantly reduces overhead,
approaching a 1:1 input-to-output size ratio (plus minimal signalling overhead).
This is much better than standard Base64 or Base85 for such data. Passthrough transmission becomes beneficial for sequences longer than 20 bytes (L>5).
- **Unambiguous Padding:** Handles input not divisible by 4 bytes cleanly. Final 1–3 bytes are encoded into exactly 2–4 characters respectively. No padding added.

- **Alphabet Supporting N Protocols:** Excludes problematic characters
(`" & ' ; < = > \ |`) that require escaping in JSON-Values,
HTML/XML attributes, quoted cookie values, CSV etc.

- **Decoding Compatibility with z85** Every z85 encoded string can be read
by an Base85N decoder.
---

## 3. Conformance Requirements

The key words **"MUST", "MUST NOT", "REQUIRED", "SHALL", "SHALL NOT", "SHOULD", "SHOULD NOT", "RECOMMENDED", "MAY", and "OPTIONAL"** are to be interpreted as described in [RFC 2119](https://www.rfc-editor.org/rfc/rfc2119).

---

## 4. Alphabet

4. Alphabet
Base85N MUST support the following the following
85-character alphabets, Variant N and Z.
The table shows the character,
and its corresponding Base85N numerical value (0-84).

| N | Z | Wert |
|---|---|---|
| 0 | 0 | 0 |
| 1 | 1 | 1 |
| 2 | 2 | 2 |
| 3 | 3 | 3 |
| 4 | 4 | 4 |
| 5 | 5 | 5 |
| 6 | 6 | 6 |
| 7 | 7 | 7 |
| 8 | 8 | 8 |
| 9 | 9 | 9 |
| a | a | 10 |
| b | b | 11 |
| c | c | 12 |
| d | d | 13 |
| e | e | 14 |
| f | f | 15 |
| g | g | 16 |
| h | h | 17 |
| i | i | 18 |
| j | j | 19 |
| k | k | 20 |
| l | l | 21 |
| m | m | 22 |
| n | n | 23 |
| o | o | 24 |
| p | p | 25 |
| q | q | 26 |
| r | r | 27 |
| s | s | 28 |
| t | t | 29 |
| u | u | 30 |
| v | v | 31 |
| w | w | 32 |
| x | x | 33 |
| y | y | 34 |
| z | z | 35 |
| A | A | 36 |
| B | B | 37 |
| C | C | 38 |
| D | D | 39 |
| E | E | 40 |
| F | F | 41 |
| G | G | 42 |
| H | H | 43 |
| I | I | 44 |
| J | J | 45 |
| K | K | 46 |
| L | L | 47 |
| M | M | 48 |
| N | N | 49 |
| O | O | 50 |
| P | P | 51 |
| Q | Q | 52 |
| R | R | 53 |
| S | S | 54 |
| T | T | 55 |
| U | U | 56 |
| V | V | 57 |
| W | W | 58 |
| X | X | 59 |
| Y | Y | 60 |
| Z | Z | 61 |
| . | . | 62 |
| - | - | 63 |
| : | : | 64 |
| + | + | 65 |
| = | = | 66 |
| ^ | ^ | 67 |
| ! | ! | 68 |
| / | / | 69 |
| * | * | 70 |
| ? | ? | 71 |
| ` | & | 72 |
| _ | < | 73 |
| ~ | > | 74 |
| ( | ( | 75 |
| ) | ) | 76 |
| [ | [ | 77 |
| ] | ] | 78 |
| { | { | 79 |
| } | } | 80 |
| @ | @ | 81 |
| % | % | 82 |
| $ | $ | 83 |
| # | # | 84 |



---

## 5. Endianness

All conversions between multi-byte integers
and byte sequences MUST use **Big-Endian** byte order.

---

## 6. Encoding Algorithm


A function ValueToChar(value) MUST return the 
corresponding character from the N variant for a valid input
value v (0-84).

**Input:** Byte sequence `B_in`  
**Output:** Encoded stream `S_out`

**Processing Steps:**

1. Initialize `S_out` = empty, `idx` = 0.
2. Processing Loop: While `idx` < length(`B_in`):
   a. Check for Passthrough Suitability: The encoder MUST evaluate if the sequence of bytes starting at `idx` is suitable and long enough for Base85N Passthrough transmission (see 6.1). The specific heuristic for determining suitability is implementation-dependent, but the minimum length requirement (`L >= 4`) MUST be met. If the encoder decides to use passthrough mode for `L` 4-byte blocks, proceed to Step 6.1. Otherwise, proceed to Step 6.2.  
   b. Standard/Partial Block Encoding: Proceed to Step 6.2.

3. **6.1 Base85N Passthrough Signal and Output:**

   a. Determine the number of 4-byte blocks `L` to send as passthrough bytes (`L >= 4`). Let `L_bytes = 4 * L`. Ensure `idx + L_bytes <= length(B_in)`. (See Implementation Note 6.3 regarding choosing `L`).  
   b. Calculate signal value `X = 2^32 + L`. (This ensures `X >= 2^32 + 4`). The encoder MUST ensure `X < 85^5`.  
   c. Convert `X` to 5 Base85 digits (`s_1`..`s_5`) using `ValueToBase85Digits(X, 5)`. (See Section 8).  
   d. Append the 5 signal characters `ValueToChar(s_1)`...`ValueToChar(s_5)` to `S_out`.  
   e. Append the next `L_bytes` from `B_in` (starting at `idx`) directly to `S_out`. (Note: `S_out` now contains a mix of Base85N characters and raw bytes).  
   f. Increment `idx` by `L_bytes`. Continue loop at Step 2.

5. **Implementation Note on Passthrough Length `L` and Buffering:**
   * To determine an optimal length `L` for passthrough, an encoder will typically need to buffer input data and look ahead.
   * Encoders MAY choose to emit a passthrough sequence with a length `L` that is less than the maximum possible length of contiguous suitable data, for reasons such as internal buffer limits, memory constraints, or parallel processing strategies (chunking).
   * However, for consistency and maximum efficiency, encoders SHOULD use the largest possible value for `L` (`L >= 4`) whenever feasible. Using the largest possible `L` helps ensure that different encoder implementations produce identical output for the same input.
---

## 7. Decoding Algorithm

A mapping function CharToValue(c) MUST return 
the integer value (0-84) or an error indicator 
for invalid input characters c.
Any Character from any variant is valid. 

**Input:** Encoded stream `S_in`  
**Output:** Byte sequence `B_out`

**Processing Steps:**

1. Initialize `B_out` = empty, `idx` = 0.
2. While `idx < len(S_in)`:
   - Read next 5 characters:
     - If less than 5:
       - If not 2–4 characters, fail.
       - Decode partial block.
     - If exactly 5:
       - Convert to integer X.
       - If X ≥ 2³²+4: raw mode → read L= X-2³² → next 4*L bytes
         are raw.
       - If X between(including) 2³² and 2³²+3 invalid (reserved).
       - Else: decode as 4-byte normal block.

---

## 8. Value/Digit Conversion
These functions define the core base conversion logic.
 * ValueToBase85Digits(Value, NumDigits):
   * Purpose: Converts a non-negative integer Value into a fixed number (NumDigits) of Base85 digits (0-84). Used for encoding full blocks (Value=N, NumDigits=5), partial blocks (Value=Nk, NumDigits=k+1), and signal values (Value=X, NumDigits=5).
   * Input: Integer Value, Integer NumDigits (MUST be > 0).
   * Output: Array/List of NumDigits integers d[0..NumDigits-1], where d[0] is the most significant digit.
   * Process: Performs standard base conversion. Value MUST be less than 85^{NumDigits}. Calculates digits via repeated modulo 85 and integer division by 85. Returns exactly NumDigits digits, padding with leading zeros if the calculated number of digits is less than NumDigits.
```python

ValueToBase85Digits(Value, NumDigits):

digits = array[NumDigits]
tempValue = Value
for i from NumDigits-1 down to 0:
  digits[i] = tempValue % 85
  tempValue = tempValue // 85
return digits
```

Base85DigitsToValue(Digits, NumDigits):
   * Purpose: Converts a sequence of NumDigits Base85 digits (0-84) back into a non-negative integer. Used for decoding all encoded block types (full, partial, signal).
   * Input: Array/List of NumDigits integers Digits[0..NumDigits-1], where Digits[0] is most significant. Integer NumDigits. Digits MUST be in range [0, 84].
   * Output: Integer Value.`
   * Process: Performs standard base conversion. Calculates
   *  Value = $\sum_{i=0}^{NumDigits-1}$ Digits[i] * $85^{NumDigits-1-i}$.

```python
Base85DigitsToValue(Digits, NumDigits):

value = 0
for i from 0 to NumDigits-1:
  value = value * 85 + Digits[i]
return value
```

---

## 9. Reserved Sequences

The following 5-character sequences correspond to 
the numerical values `X = 2^32` through `X = 2^32 + 4`. 
These values MUST NOT be generated by an encoder 
(as they correspond to passthrough lengths `L=0` to 
`L=4`, which are disallowed) and MUST be treated as an
error if encountered during decoding.

- `X = 2^32` (`L = 0`)
- `X = 2^32 + 1` (`L = 1`)
- `X = 2^32 + 2` (`L = 2`)
- `X = 2^32 + 3` (`L = 3`)
- `X = 2^32 + 4` (`L = 4`)



## 10. Error Handling

Implementations MUST detect and report:

 * Invalid characters during decoding
   Invalid characters encountered in the input character stream during decoding (characters not in the Base85N alphabet).

 * Invalid final block (1 char only).
   Invalid length of the final sequence of characters during decoding (e.g., 1 character left when expecting at least 2).

 * Unexpected EOF during raw mode.
   Unexpected end of stream/file, especially when reading raw bytes indicated by a signal block.

 * Reserved signal value (X = 2³²).
   Encountering the reserved signal value X = 2^{32}.

 * Overflow when decoding partial block.
   Decoding a partial block resulting in a value N_k outside the valid range for k bytes (i.e., N_k \ge 2^{8k}).

 * Input value to ValueToBase85Digits being too large for the specified NumDigits.

 
---





