
---

# Base85N Encoding Specification v1.0

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

- **High Efficiency via Passthrough**: For input data containing sequences suitable for direct transmission (e.g., binary blobs, non-conflicting text), the adaptive Base85N Passthrough mode significantly reduces overhead, approaching a 1:1 input-to-output size ratio (plus minimal signalling overhead). This is much better than standard Base64 or Base85 for such data. Passthrough transmission becomes beneficial for sequences longer than 20 bytes (L>5).
- **Unambiguous Padding:** Handles input not divisible by 4 bytes cleanly. Final 1–3 bytes are encoded into exactly 2–4 characters respectively. No padding added.

- **Robust Alphabet:** Excludes problematic characters (`" % & ' ; < = > \`) that require escaping in JSON, HTML/XML attributes, etc.

---

## 3. Conformance Requirements

The key words **"MUST", "MUST NOT", "REQUIRED", "SHALL", "SHALL NOT", "SHOULD", "SHOULD NOT", "RECOMMENDED", "MAY", and "OPTIONAL"** are to be interpreted as described in [RFC 2119](https://www.rfc-editor.org/rfc/rfc2119).

---

## 4. Alphabet

4. Alphabet
Base85N MUST use the following 85-character alphabet. The table shows the character, its ASCII code, and its corresponding Base85N numerical value (0-84).

| Char | ASCII | Value | Char | ASCII | Value |
|---|---|---|---|---|---|
| ! | 33 | 0 | # | 35 | 1 |
| $ | 36 | 2 | ( | 40 | 3 |
| ) | 41 | 4 | * | 42 | 5 |
| + | 43 | 6 | , | 44 | 7 |
| - | 45 | 8 | . | 46 | 9 |
| / | 47 | 10 | 0 | 48 | 11 |
| 1 | 49 | 12 | 2 | 50 | 13 |
| 3 | 51 | 14 | 4 | 52 | 15 |
| 5 | 53 | 16 | 6 | 54 | 17 |
| 7 | 55 | 18 | 8 | 56 | 19 |
| 9 | 57 | 20 | : | 58 | 21 |
| ? | 63 | 22 | @ | 64 | 23 |
| A | 65 | 24 | B | 66 | 25 |
| C | 67 | 26 | D | 68 | 27 |
| E | 69 | 28 | F | 70 | 29 |
| G | 71 | 30 | H | 72 | 31 |
| I | 73 | 32 | J | 74 | 33 |
| K | 75 | 34 | L | 76 | 35 |
| M | 77 | 36 | N | 78 | 37 |
| O | 79 | 38 | P | 80 | 39 |
| Q | 81 | 40 | R | 82 | 41 |
| S | 83 | 42 | T | 84 | 43 |
| U | 85 | 44 | V | 86 | 45 |
| W | 87 | 46 | X | 88 | 47 |
| Y | 89 | 48 | Z | 90 | 49 |
| [ | 91 | 50 | ] | 93 | 51 |
| ^ | 94 | 52 | _ | 95 | 53 |
| ` | 96 | 54 | a | 97 | 55 |
| b | 98 | 56 | c | 99 | 57 |
| d | 100 | 58 | e | 101 | 59 |
| f | 102 | 60 | g | 103 | 61 |
| h | 104 | 62 | i | 105 | 63 |
| j | 106 | 64 | k | 107 | 65 |
| l | 108 | 66 | m | 109 | 67 |
| n | 110 | 68 | o | 111 | 69 |
| p | 112 | 70 | q | 113 | 71 |
| r | 114 | 72 | s | 115 | 73 |
| t | 116 | 74 | u | 117 | 75 |
| v | 118 | 76 | w | 119 | 77 |
| x | 120 | 78 | y | 121 | 79 |
| z | 122 | 80 | { | 123 | 81 |
| \| | 124 | 82 | } | 125 | 83 |
| ~ | 126 | 84 |  |  |  |

A mapping function CharToValue(c) MUST return the integer value (0-84) or an error indicator for invalid input characters c. A function ValueToChar(v) MUST return the corresponding character for a valid input value v (0-84).

---

## 5. Endianness

All conversions between multi-byte integers and byte sequences MUST use **Big-Endian** byte order.

---

## 6. Encoding Algorithm

**Input:** Byte sequence `B_in`  
**Output:** Encoded stream `S_out`

**Processing Steps:**

1. Initialize `S_out` = empty, `idx` = 0.
2. Processing Loop: While `idx` < length(`B_in`):
   a.  Check for Passthrough Suitability: The encoder MUST evaluate if the sequence of bytes starting at idx is suitable and long enough for Base85N Passthrough transmission (See 6.1). The specific heuristic for determining suitability is implementation-dependent, but the minimum length requirement (L \ge 4) MUST be met. If the encoder decides to use passthrough mode for L 4-byte blocks, proceed to Step 6.1. Otherwise, proceed to Step 6.2.
   b.  Standard/Partial Block Encoding: Proceed to Step 6.2.
 * 6.1 Base85N Passthrough Signal and Output:
   a.  Determine the number of 4-byte blocks L to send as passthrough bytes (L \ge 4). Let L_bytes = 4L. Ensure idx + L_bytes <= length(`B_in`). (See Implementation Note 6.3 regarding choosing L).
   b.  Calculate signal value X = 2^{32} + L. (This ensures X \ge 2^{32}+4). The encoder MUST ensure X < 85^5.
   c.  Convert X to 5 Base85 digits (s_1..s_5) using ValueToBase85Digits(X, 5). (See Section 8).
   d.  Append the 5 signal characters ValueToChar(s_1)...ValueToChar(s_5) to S_out.
   e.  Append the next L_bytes from `B_in` (starting at idx) directly to S_out. (Note: S_out now contains a mix of Base85N characters and raw bytes).
   f.  Increment idx by L_bytes. Continue loop at Step 2.
3. Implementation Note on Passthrough Length L and Buffering:
   * To determine an optimal length L for passthrough, an encoder will typically need to buffer input data and look ahead.
   * Encoders MAY choose to emit a passthrough sequence with a length L that is less than the maximum possible length of contiguous suitable data, for reasons such as internal buffer limits, memory constraints, or parallel processing strategies (chunking).
   * However, for consistency and maximum efficiency, encoders SHOULD use the largest possible value for L (L \ge 4) whenever feasible. Using the largest possible L helps ensure that different encoder implementations produce identical output for the same input.

---

## 7. Decoding Algorithm

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
       - If X ≥ 2³²+1: raw mode → read L= X-2³² → next 4L bytes are raw.
       - If X = 2³²: invalid (reserved).
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
   * Output: Integer Value.
   * Process: Performs standard base conversion. Calculates Value = \sum_{i=0}^{NumDigits-1} Digits[i] \times 85^{NumDigits-1-i}.

```python
Base85DigitsToValue(Digits, NumDigits):

value = 0
for i from 0 to NumDigits-1:
  value = value * 85 + Digits[i]
return value
```

---

## 9. Error Handling

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





