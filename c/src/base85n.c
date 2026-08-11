/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/*
 * base85n.c - Implementation of the Base85N binary-to-text encoding
 * scheme, per the spec, including Section 6.1's two-pass ("Pass 1"
 * window/mask discovery, "Pass 2" boundary finalization) Dynamic
 * Passthrough procedure.
 */

#include "base85n.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Constants                                                           */
/* ------------------------------------------------------------------ */

#define ALPHABET_SIZE 85

static const char ALPHABET_N_CHARS_STR[ALPHABET_SIZE + 1] =
    "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
    ".-:+=^!/*?`_~()[]{}@%$#";

#define RSET_COUNT 13

/* The R-Set (spec 4.1) and its allowedPassthroughSafeReplacementCharacters
 * (spec 4.2), by R-Set index j:
 *
 *   j     0    1    2    3    4    5    6    7    8    9   10   11   12
 *   R    ' '  '"'  '\'' ','  ';'  '\\' '|'  '<'  '>'  '&'  \t   \n   \r
 *   ->   ':'  '+'  '='  '^'  '!'  '/'  '*'  '?'  '`'  '('  ')'  '['  ']'
 *
 * Both are folded into the byte-indexed tables below rather than kept as
 * arrays of their own, because nothing indexes them by j at run time any
 * more. tests/test_base85n.c holds an independent copy of this table and
 * checks every pair through the public API. */

#define ESCAPE_CHAR '~'

#define MAX_CONSECUTIVE_ESCAPES BASE85N_MAX_CONSECUTIVE_ESCAPES
#define MAX_DP_OUTPUT_CHARS_PER_SIGNAL BASE85N_MAX_DP_OUTPUT_CHARS_PER_SIGNAL
#define MIN_PASSTHROUGH_BYTES BASE85N_MIN_PASSTHROUGH_BYTES

/* Longest representable run whose Pass 2 scratch fits on the stack. Two
 * characters per byte for the transformed text, plus one segment offset
 * per 255 bytes and a spare, is about 1 KiB of frame. */
#define SCRATCH_STACK_WINDOW 512

#define POW2_32 ((uint64_t)1u << 32)
#define SIGNAL_PAYLOAD_MAX ((uint64_t)(1u << 22) - 1u) /* 2^22 - 1 */

/* ------------------------------------------------------------------ */
/* Byte-indexed lookup tables                                          */
/* ------------------------------------------------------------------ */

/* Every inner loop in this file classifies one input byte per iteration,
 * so each of these tables answers, in a single load, every question its
 * loop needs to ask about that byte. They are `const` and built at compile
 * time rather than lazily, which keeps the library free of shared mutable
 * state and makes the thread-safety promise in base85n.h unconditional.
 *
 * The fields are packed rather than kept in separate tables because the
 * loads, not the arithmetic, are what the loops are made of: one indexed
 * load and a mask beats two indexed loads and two branches.
 *
 * tests/test_base85n.c drives every entry of all four tables through the
 * public API and checks the result against ALPHABET_N_CHARS_STR,
 * RSET_ASCII and REPLACEMENT_CHARS, so a typo here cannot survive
 * `make test`. */

/* ASCII byte -> Alphabet-N digit value (0-84), or -1 if not in Alphabet-N.
 * Read as a uint8_t in the decoder's hot path, where "not in Alphabet-N"
 * becomes the single testable bit 0x80 (all real digit values are < 85). */
static const int8_t ALPHABET_VALUE[256] = {
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  68,  -1,  84,  83,  82,  -1,  -1,  75,  76,  70,  65,  -1,  63,  62,  69,
      0,   1,   2,   3,   4,   5,   6,   7,   8,   9,  64,  -1,  -1,  66,  -1,  71,
     81,  36,  37,  38,  39,  40,  41,  42,  43,  44,  45,  46,  47,  48,  49,  50,
     51,  52,  53,  54,  55,  56,  57,  58,  59,  60,  61,  77,  -1,  78,  67,  73,
     72,  10,  11,  12,  13,  14,  15,  16,  17,  18,  19,  20,  21,  22,  23,  24,
     25,  26,  27,  28,  29,  30,  31,  32,  33,  34,  35,  79,  -1,  80,  74,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1
};

/* Pass 1's view of a byte (spec 6.1, step 1.a):
 *
 *   bit 15        CLS_UNREPRESENTABLE -- the byte is neither in Alphabet-N
 *                 nor in the R-Set, so a representable run ends here.
 *   bits 0..12    CLS_RSET_BITS -- (1 << j) if the byte is R-Set character
 *                 j, zero otherwise. This is exactly one bit of the window
 *                 mask, which is why Pass 1 can accumulate the mask for a
 *                 whole block of bytes with a plain OR.
 *   bits 24..27   the same j as a small integer, for the one caller that
 *                 needs to index by it. OR-ing entries together garbles
 *                 this field, so it is only ever read from a single
 *                 unaccumulated lookup.
 */
#define CLS_UNREPRESENTABLE 0x00008000u
#define CLS_RSET_BITS       0x00001FFFu
#define CLS_INDEX_SHIFT     24

static const uint32_t ENC_CLASS[256] = {
    0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u,
    0x00008000u, 0x0A000400u, 0x0B000800u, 0x00008000u, 0x00008000u, 0x0C001000u, 0x00008000u, 0x00008000u,
    0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u,
    0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u,
    0x00000001u, 0x00000000u, 0x01000002u, 0x00000000u, 0x00000000u, 0x00000000u, 0x09000200u, 0x02000004u,
    0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x03000008u, 0x00000000u, 0x00000000u, 0x00000000u,
    0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
    0x00000000u, 0x00000000u, 0x00000000u, 0x04000010u, 0x07000080u, 0x00000000u, 0x08000100u, 0x00000000u,
    0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
    0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
    0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
    0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x05000020u, 0x00000000u, 0x00000000u, 0x00000000u,
    0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
    0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
    0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
    0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x06000040u, 0x00000000u, 0x00000000u, 0x00008000u,
    0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u,
    0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u,
    0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u,
    0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u,
    0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u,
    0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u,
    0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u,
    0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u,
    0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u,
    0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u,
    0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u,
    0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u,
    0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u,
    0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u,
    0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u,
    0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u, 0x00008000u
};

/* Pass 2's view of a byte (spec 6.1, step 1.b):
 *
 *   bits 0..7     the character the byte becomes in DP output: its
 *                 replacement character if it is an R-Set byte (Case i),
 *                 the byte itself otherwise (Case ii and Case iii).
 *   bits 16..31   the escape trigger: the byte needs a '~' prefix iff this
 *                 field intersects (window_mask | DP_ESCAPE_ALWAYS). It is
 *                 (1 << j) << 16 for replacement character j -- which only
 *                 triggers while R-Set character j is in the window -- and
 *                 DP_ESCAPE_ALWAYS << 16 for '~' itself, which is escaped
 *                 unconditionally.
 *
 * Note that no byte is both an R-Set character and a replacement character,
 * so the two fields never have to describe the same byte at once. */
#define DP_XLAT_CHAR(t)    ((uint8_t)(t))
#define DP_XLAT_TRIGGER(t) ((t) >> 16)
#define DP_ESCAPE_ALWAYS   0x8000u

static const uint32_t DP_XLAT[256] = {
    0x00000000u, 0x00000001u, 0x00000002u, 0x00000003u, 0x00000004u, 0x00000005u, 0x00000006u, 0x00000007u,
    0x00000008u, 0x00000029u, 0x0000005Bu, 0x0000000Bu, 0x0000000Cu, 0x0000005Du, 0x0000000Eu, 0x0000000Fu,
    0x00000010u, 0x00000011u, 0x00000012u, 0x00000013u, 0x00000014u, 0x00000015u, 0x00000016u, 0x00000017u,
    0x00000018u, 0x00000019u, 0x0000001Au, 0x0000001Bu, 0x0000001Cu, 0x0000001Du, 0x0000001Eu, 0x0000001Fu,
    0x0000003Au, 0x00100021u, 0x0000002Bu, 0x00000023u, 0x00000024u, 0x00000025u, 0x00000028u, 0x0000003Du,
    0x02000028u, 0x04000029u, 0x0040002Au, 0x0002002Bu, 0x0000005Eu, 0x0000002Du, 0x0000002Eu, 0x0020002Fu,
    0x00000030u, 0x00000031u, 0x00000032u, 0x00000033u, 0x00000034u, 0x00000035u, 0x00000036u, 0x00000037u,
    0x00000038u, 0x00000039u, 0x0001003Au, 0x00000021u, 0x0000003Fu, 0x0004003Du, 0x00000060u, 0x0080003Fu,
    0x00000040u, 0x00000041u, 0x00000042u, 0x00000043u, 0x00000044u, 0x00000045u, 0x00000046u, 0x00000047u,
    0x00000048u, 0x00000049u, 0x0000004Au, 0x0000004Bu, 0x0000004Cu, 0x0000004Du, 0x0000004Eu, 0x0000004Fu,
    0x00000050u, 0x00000051u, 0x00000052u, 0x00000053u, 0x00000054u, 0x00000055u, 0x00000056u, 0x00000057u,
    0x00000058u, 0x00000059u, 0x0000005Au, 0x0800005Bu, 0x0000002Fu, 0x1000005Du, 0x0008005Eu, 0x0000005Fu,
    0x01000060u, 0x00000061u, 0x00000062u, 0x00000063u, 0x00000064u, 0x00000065u, 0x00000066u, 0x00000067u,
    0x00000068u, 0x00000069u, 0x0000006Au, 0x0000006Bu, 0x0000006Cu, 0x0000006Du, 0x0000006Eu, 0x0000006Fu,
    0x00000070u, 0x00000071u, 0x00000072u, 0x00000073u, 0x00000074u, 0x00000075u, 0x00000076u, 0x00000077u,
    0x00000078u, 0x00000079u, 0x0000007Au, 0x0000007Bu, 0x0000002Au, 0x0000007Du, 0x8000007Eu, 0x0000007Fu,
    0x00000080u, 0x00000081u, 0x00000082u, 0x00000083u, 0x00000084u, 0x00000085u, 0x00000086u, 0x00000087u,
    0x00000088u, 0x00000089u, 0x0000008Au, 0x0000008Bu, 0x0000008Cu, 0x0000008Du, 0x0000008Eu, 0x0000008Fu,
    0x00000090u, 0x00000091u, 0x00000092u, 0x00000093u, 0x00000094u, 0x00000095u, 0x00000096u, 0x00000097u,
    0x00000098u, 0x00000099u, 0x0000009Au, 0x0000009Bu, 0x0000009Cu, 0x0000009Du, 0x0000009Eu, 0x0000009Fu,
    0x000000A0u, 0x000000A1u, 0x000000A2u, 0x000000A3u, 0x000000A4u, 0x000000A5u, 0x000000A6u, 0x000000A7u,
    0x000000A8u, 0x000000A9u, 0x000000AAu, 0x000000ABu, 0x000000ACu, 0x000000ADu, 0x000000AEu, 0x000000AFu,
    0x000000B0u, 0x000000B1u, 0x000000B2u, 0x000000B3u, 0x000000B4u, 0x000000B5u, 0x000000B6u, 0x000000B7u,
    0x000000B8u, 0x000000B9u, 0x000000BAu, 0x000000BBu, 0x000000BCu, 0x000000BDu, 0x000000BEu, 0x000000BFu,
    0x000000C0u, 0x000000C1u, 0x000000C2u, 0x000000C3u, 0x000000C4u, 0x000000C5u, 0x000000C6u, 0x000000C7u,
    0x000000C8u, 0x000000C9u, 0x000000CAu, 0x000000CBu, 0x000000CCu, 0x000000CDu, 0x000000CEu, 0x000000CFu,
    0x000000D0u, 0x000000D1u, 0x000000D2u, 0x000000D3u, 0x000000D4u, 0x000000D5u, 0x000000D6u, 0x000000D7u,
    0x000000D8u, 0x000000D9u, 0x000000DAu, 0x000000DBu, 0x000000DCu, 0x000000DDu, 0x000000DEu, 0x000000DFu,
    0x000000E0u, 0x000000E1u, 0x000000E2u, 0x000000E3u, 0x000000E4u, 0x000000E5u, 0x000000E6u, 0x000000E7u,
    0x000000E8u, 0x000000E9u, 0x000000EAu, 0x000000EBu, 0x000000ECu, 0x000000EDu, 0x000000EEu, 0x000000EFu,
    0x000000F0u, 0x000000F1u, 0x000000F2u, 0x000000F3u, 0x000000F4u, 0x000000F5u, 0x000000F6u, 0x000000F7u,
    0x000000F8u, 0x000000F9u, 0x000000FAu, 0x000000FBu, 0x000000FCu, 0x000000FDu, 0x000000FEu, 0x000000FFu
};

/* The decoder's view of a character inside a DP segment (spec 7.2):
 *
 *   bit 31        DEC_INVALID -- the character is not in Alphabet-N.
 *   bit 30        DEC_ESCAPE -- the character is '~'.
 *   bits 16..28   (1 << j) if the character is replacement character j.
 *                 Intersect with the signal's 13-bit mask to decide whether
 *                 this occurrence stands for an R-Set byte or for itself.
 *   bits 0..7     the byte to emit when it does: R-Set character j's ASCII
 *                 value, or the character itself when it is not a
 *                 replacement character at all.
 */
#define DEC_INVALID 0x80000000u
#define DEC_ESCAPE  0x40000000u

static const uint32_t DEC_SUB[256] = {
    0x80000000u, 0x80000001u, 0x80000002u, 0x80000003u, 0x80000004u, 0x80000005u, 0x80000006u, 0x80000007u,
    0x80000008u, 0x80000009u, 0x8000000Au, 0x8000000Bu, 0x8000000Cu, 0x8000000Du, 0x8000000Eu, 0x8000000Fu,
    0x80000010u, 0x80000011u, 0x80000012u, 0x80000013u, 0x80000014u, 0x80000015u, 0x80000016u, 0x80000017u,
    0x80000018u, 0x80000019u, 0x8000001Au, 0x8000001Bu, 0x8000001Cu, 0x8000001Du, 0x8000001Eu, 0x8000001Fu,
    0x80000020u, 0x0010003Bu, 0x80000022u, 0x00000023u, 0x00000024u, 0x00000025u, 0x80000026u, 0x80000027u,
    0x02000026u, 0x04000009u, 0x0040007Cu, 0x00020022u, 0x8000002Cu, 0x0000002Du, 0x0000002Eu, 0x0020005Cu,
    0x00000030u, 0x00000031u, 0x00000032u, 0x00000033u, 0x00000034u, 0x00000035u, 0x00000036u, 0x00000037u,
    0x00000038u, 0x00000039u, 0x00010020u, 0x8000003Bu, 0x8000003Cu, 0x00040027u, 0x8000003Eu, 0x0080003Cu,
    0x00000040u, 0x00000041u, 0x00000042u, 0x00000043u, 0x00000044u, 0x00000045u, 0x00000046u, 0x00000047u,
    0x00000048u, 0x00000049u, 0x0000004Au, 0x0000004Bu, 0x0000004Cu, 0x0000004Du, 0x0000004Eu, 0x0000004Fu,
    0x00000050u, 0x00000051u, 0x00000052u, 0x00000053u, 0x00000054u, 0x00000055u, 0x00000056u, 0x00000057u,
    0x00000058u, 0x00000059u, 0x0000005Au, 0x0800000Au, 0x8000005Cu, 0x1000000Du, 0x0008002Cu, 0x0000005Fu,
    0x0100003Eu, 0x00000061u, 0x00000062u, 0x00000063u, 0x00000064u, 0x00000065u, 0x00000066u, 0x00000067u,
    0x00000068u, 0x00000069u, 0x0000006Au, 0x0000006Bu, 0x0000006Cu, 0x0000006Du, 0x0000006Eu, 0x0000006Fu,
    0x00000070u, 0x00000071u, 0x00000072u, 0x00000073u, 0x00000074u, 0x00000075u, 0x00000076u, 0x00000077u,
    0x00000078u, 0x00000079u, 0x0000007Au, 0x0000007Bu, 0x8000007Cu, 0x0000007Du, 0x4000007Eu, 0x8000007Fu,
    0x80000080u, 0x80000081u, 0x80000082u, 0x80000083u, 0x80000084u, 0x80000085u, 0x80000086u, 0x80000087u,
    0x80000088u, 0x80000089u, 0x8000008Au, 0x8000008Bu, 0x8000008Cu, 0x8000008Du, 0x8000008Eu, 0x8000008Fu,
    0x80000090u, 0x80000091u, 0x80000092u, 0x80000093u, 0x80000094u, 0x80000095u, 0x80000096u, 0x80000097u,
    0x80000098u, 0x80000099u, 0x8000009Au, 0x8000009Bu, 0x8000009Cu, 0x8000009Du, 0x8000009Eu, 0x8000009Fu,
    0x800000A0u, 0x800000A1u, 0x800000A2u, 0x800000A3u, 0x800000A4u, 0x800000A5u, 0x800000A6u, 0x800000A7u,
    0x800000A8u, 0x800000A9u, 0x800000AAu, 0x800000ABu, 0x800000ACu, 0x800000ADu, 0x800000AEu, 0x800000AFu,
    0x800000B0u, 0x800000B1u, 0x800000B2u, 0x800000B3u, 0x800000B4u, 0x800000B5u, 0x800000B6u, 0x800000B7u,
    0x800000B8u, 0x800000B9u, 0x800000BAu, 0x800000BBu, 0x800000BCu, 0x800000BDu, 0x800000BEu, 0x800000BFu,
    0x800000C0u, 0x800000C1u, 0x800000C2u, 0x800000C3u, 0x800000C4u, 0x800000C5u, 0x800000C6u, 0x800000C7u,
    0x800000C8u, 0x800000C9u, 0x800000CAu, 0x800000CBu, 0x800000CCu, 0x800000CDu, 0x800000CEu, 0x800000CFu,
    0x800000D0u, 0x800000D1u, 0x800000D2u, 0x800000D3u, 0x800000D4u, 0x800000D5u, 0x800000D6u, 0x800000D7u,
    0x800000D8u, 0x800000D9u, 0x800000DAu, 0x800000DBu, 0x800000DCu, 0x800000DDu, 0x800000DEu, 0x800000DFu,
    0x800000E0u, 0x800000E1u, 0x800000E2u, 0x800000E3u, 0x800000E4u, 0x800000E5u, 0x800000E6u, 0x800000E7u,
    0x800000E8u, 0x800000E9u, 0x800000EAu, 0x800000EBu, 0x800000ECu, 0x800000EDu, 0x800000EEu, 0x800000EFu,
    0x800000F0u, 0x800000F1u, 0x800000F2u, 0x800000F3u, 0x800000F4u, 0x800000F5u, 0x800000F6u, 0x800000F7u,
    0x800000F8u, 0x800000F9u, 0x800000FAu, 0x800000FBu, 0x800000FCu, 0x800000FDu, 0x800000FEu, 0x800000FFu
};

static int is_ignorable_ws(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

/* ------------------------------------------------------------------ */
/* Base85 digit <-> value conversion (spec section 8)             */
/* ------------------------------------------------------------------ */

#define POW85_2 7225u     /* 85^2 */
#define POW85_3 614125u   /* 85^3 */
#define POW85_4 52200625u /* 85^4 */

/* Alphabet-N characters for every two-digit base-85 value 0 .. 85^2-1,
 * most significant digit first: PAIR_CHARS[2*v] and PAIR_CHARS[2*v + 1].
 *
 * Written as one row of 85 pairs per high digit, in Alphabet-N order, so
 * the table is visibly the same alphabet the encoder is defined against
 * rather than an opaque blob. It is 14450 bytes -- larger than the
 * byte-indexed tables above, but block mode touches it twice per 4-byte
 * group and only ever over ~226 cache lines, so it settles into L1 and
 * stays there for the duration of an encode. */
#define PAIR_ROW(c) \
    c,'0', c,'1', c,'2', c,'3', c,'4', \
    c,'5', c,'6', c,'7', c,'8', c,'9', \
    c,'a', c,'b', c,'c', c,'d', c,'e', \
    c,'f', c,'g', c,'h', c,'i', c,'j', \
    c,'k', c,'l', c,'m', c,'n', c,'o', \
    c,'p', c,'q', c,'r', c,'s', c,'t', \
    c,'u', c,'v', c,'w', c,'x', c,'y', \
    c,'z', c,'A', c,'B', c,'C', c,'D', \
    c,'E', c,'F', c,'G', c,'H', c,'I', \
    c,'J', c,'K', c,'L', c,'M', c,'N', \
    c,'O', c,'P', c,'Q', c,'R', c,'S', \
    c,'T', c,'U', c,'V', c,'W', c,'X', \
    c,'Y', c,'Z', c,'.', c,'-', c,':', \
    c,'+', c,'=', c,'^', c,'!', c,'/', \
    c,'*', c,'?', c,'`', c,'_', c,'~', \
    c,'(', c,')', c,'[', c,']', c,'{', \
    c,'}', c,'@', c,'%', c,'$', c,'#',

static const char PAIR_CHARS[POW85_2 * 2] = {
    PAIR_ROW('0') PAIR_ROW('1') PAIR_ROW('2') PAIR_ROW('3')
    PAIR_ROW('4') PAIR_ROW('5') PAIR_ROW('6') PAIR_ROW('7')
    PAIR_ROW('8') PAIR_ROW('9') PAIR_ROW('a') PAIR_ROW('b')
    PAIR_ROW('c') PAIR_ROW('d') PAIR_ROW('e') PAIR_ROW('f')
    PAIR_ROW('g') PAIR_ROW('h') PAIR_ROW('i') PAIR_ROW('j')
    PAIR_ROW('k') PAIR_ROW('l') PAIR_ROW('m') PAIR_ROW('n')
    PAIR_ROW('o') PAIR_ROW('p') PAIR_ROW('q') PAIR_ROW('r')
    PAIR_ROW('s') PAIR_ROW('t') PAIR_ROW('u') PAIR_ROW('v')
    PAIR_ROW('w') PAIR_ROW('x') PAIR_ROW('y') PAIR_ROW('z')
    PAIR_ROW('A') PAIR_ROW('B') PAIR_ROW('C') PAIR_ROW('D')
    PAIR_ROW('E') PAIR_ROW('F') PAIR_ROW('G') PAIR_ROW('H')
    PAIR_ROW('I') PAIR_ROW('J') PAIR_ROW('K') PAIR_ROW('L')
    PAIR_ROW('M') PAIR_ROW('N') PAIR_ROW('O') PAIR_ROW('P')
    PAIR_ROW('Q') PAIR_ROW('R') PAIR_ROW('S') PAIR_ROW('T')
    PAIR_ROW('U') PAIR_ROW('V') PAIR_ROW('W') PAIR_ROW('X')
    PAIR_ROW('Y') PAIR_ROW('Z') PAIR_ROW('.') PAIR_ROW('-')
    PAIR_ROW(':') PAIR_ROW('+') PAIR_ROW('=') PAIR_ROW('^')
    PAIR_ROW('!') PAIR_ROW('/') PAIR_ROW('*') PAIR_ROW('?')
    PAIR_ROW('`') PAIR_ROW('_') PAIR_ROW('~') PAIR_ROW('(')
    PAIR_ROW(')') PAIR_ROW('[') PAIR_ROW(']') PAIR_ROW('{')
    PAIR_ROW('}') PAIR_ROW('@') PAIR_ROW('%') PAIR_ROW('$')
    PAIR_ROW('#')
};

/* Converts a value (0 .. 85^5 - 1, which covers every uint32_t) into 5
 * Alphabet-N characters, Big-Endian digit order.
 *
 * The obvious loop -- five rounds of "digit = value % 85; value /= 85" --
 * costs five divisions by a constant, i.e. five multiply/shift/multiply/
 * subtract chains, each depending on the one before it. Reading the digits
 * out in pairs needs two divisions and three loads instead, and this is
 * the whole of block mode's arithmetic.
 *
 * The two divisions are by 85^2 and 85^3 rather than the more obvious 85^3
 * and then 85^2 of the remainder, so that neither waits for the other:
 * value / 85^2 is head*85 + mid, because 85^3 = 85 * 85^2, which recovers
 * the middle digit from a quotient that was computed in parallel with the
 * head. */
static void value_to_5chars_32(uint32_t value, char *out) {
    uint32_t q    = value / POW85_2;     /* = head * 85 + mid       */
    uint32_t head = value / POW85_3;     /* digits 0,1: 0 .. 85^2-1 */
    uint32_t tail = value - q * POW85_2; /* digits 3,4: 0 .. 85^2-1 */
    uint32_t mid  = q - head * 85u;      /* digit 2:    0 .. 84     */

    memcpy(out, PAIR_CHARS + 2 * head, 2);
    out[2] = ALPHABET_N_CHARS_STR[mid];
    memcpy(out + 3, PAIR_CHARS + 2 * tail, 2);
}

/* Same, for the one range of values that does not fit in 32 bits: a
 * Dynamic Passthrough signal is 2^32 + payload (spec section 9). Signals
 * are emitted once per segment of up to 511 characters, so this path is
 * cold and stays the straightforward loop. */
static void value_to_5chars_64(uint64_t value, char *out) {
    uint8_t digits[5];
    for (int i = 4; i >= 0; i--) {
        digits[i] = (uint8_t)(value % 85);
        value /= 85;
    }
    for (int i = 0; i < 5; i++) {
        out[i] = ALPHABET_N_CHARS_STR[digits[i]];
    }
}

/* ------------------------------------------------------------------ */
/* Encoding                                                             */
/* ------------------------------------------------------------------ */

/* Section 6.2: ProcessWithBlockMode. Encodes `n` bytes of `data`
 * starting at full 4-byte blocks; if n is not a multiple of 4, the
 * trailing 1-3 bytes are encoded as a padded partial group per the
 * spec. Writes at most (n/4)*5 + 4 characters at `w`, and returns the
 * cursor past the last one. */
static uint8_t *process_block_mode(const uint8_t *data, size_t n, uint8_t *w) {
    size_t full_blocks = n / 4;
    size_t k = 0;

    /* Two groups per iteration. Each group's digit extraction is a short
     * dependency chain with very little to fill it, and neighbouring
     * groups are entirely independent, so interleaving two of them keeps
     * the multipliers busy. gcc does not unroll this loop on its own at
     * -O2, which is the optimisation level the Makefile ships. */
    for (; k + 2 <= full_blocks; k += 2) {
        const uint8_t *p = data + 4 * k;
        uint32_t v0 = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
                      ((uint32_t)p[2] << 8) | (uint32_t)p[3];
        uint32_t v1 = ((uint32_t)p[4] << 24) | ((uint32_t)p[5] << 16) |
                      ((uint32_t)p[6] << 8) | (uint32_t)p[7];
        value_to_5chars_32(v0, (char *)w);
        value_to_5chars_32(v1, (char *)w + 5);
        w += 10;
    }
    for (; k < full_blocks; k++) {
        const uint8_t *p = data + 4 * k;
        uint32_t val = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
                       ((uint32_t)p[2] << 8) | (uint32_t)p[3];
        value_to_5chars_32(val, (char *)w);
        w += 5;
    }
    size_t rem = n % 4;
    if (rem > 0) {
        uint8_t block[4] = {0, 0, 0, 0};
        memcpy(block, data + 4 * full_blocks, rem);
        uint32_t val = ((uint32_t)block[0] << 24) | ((uint32_t)block[1] << 16) |
                       ((uint32_t)block[2] << 8) | (uint32_t)block[3];
        char chars[5];
        value_to_5chars_32(val, chars);
        /* Take the first rem+1 characters. */
        memcpy(w, chars, rem + 1);
        w += rem + 1;
    }
    return w;
}

/* Pass 1 -- Window and Mask Discovery (spec 6.1, step 1.a), scanned once
 * per representable run rather than once per iteration of the main loop.
 *
 * Bounded *only* by representability: a byte belongs to the run if it is
 * an R-Set character, or chr(byte) is in ALPHABET_N_CHARS_STR (which
 * includes the escape char and all replacement chars unconditionally,
 * regardless of escaping cost). Never terminates on account of escaping
 * cost or the consecutive-escape count.
 *
 * The main loop can consume as little as 4 bytes of a run at a time, so
 * Pass 1's window for the next position is a suffix of the one scanned
 * here. Rescanning it -- what a literal reading of 6.1 does -- is what
 * makes an encoder quadratic (spec 6.6), so the run is scanned once and
 * the window mask for any suffix is answered from what that scan found.
 *
 * The answer is a comparison, not a recount: R_Char[j] occurs in
 * [off, end) exactly when its *last* occurrence in the run is at or after
 * off. `last[j]` holds that position, found by a backward walk that stops
 * as soon as every character the forward scan saw has been placed -- which
 * for the common case, a run whose characters recur near its end, is a
 * walk of a few bytes. It is done lazily, on the first suffix asked for,
 * because most runs are never asked: a run consumed whole by one DP
 * segment has no suffix, and a run with no R-Set character in it has
 * nothing to narrow. */
typedef struct {
    size_t last[RSET_COUNT]; /* last offset of R_Char[j] within the run */
    size_t start;            /* offset the forward scan started from */
    size_t end;              /* offset into data, exclusive, where the run ends */
    size_t hold;             /* suffix_mask is the answer for every off <= hold */
    uint16_t mask;           /* bit j set iff R_Char[j] occurs in [start, end) */
    uint16_t suffix_mask;    /* the last answer given */
    int located;             /* last[] has been filled in */
} run_state;

static void run_scan(const uint8_t *data, size_t data_len, size_t pos,
                     run_state *st) {
    uint32_t acc = 0;
    size_t i = pos;

    /* Eight bytes at a time. A block is entirely representable iff the OR
     * of its eight class words has no CLS_UNREPRESENTABLE bit, and that
     * same OR carries every R-Set bit the block contributes to the mask --
     * so the common case, a long representable run, costs one load and one
     * OR per byte with no per-byte branch. */
    while (data_len - i >= 8) {
        uint32_t a = ENC_CLASS[data[i]]     | ENC_CLASS[data[i + 1]] |
                     ENC_CLASS[data[i + 2]] | ENC_CLASS[data[i + 3]];
        uint32_t b = ENC_CLASS[data[i + 4]] | ENC_CLASS[data[i + 5]] |
                     ENC_CLASS[data[i + 6]] | ENC_CLASS[data[i + 7]];
        uint32_t both = a | b;
        if (both & CLS_UNREPRESENTABLE) break;
        acc |= both;
        i += 8;
    }
    for (; i < data_len; i++) {
        uint32_t c = ENC_CLASS[data[i]];
        if (c & CLS_UNREPRESENTABLE) break;
        acc |= c;
    }

    /* acc's index field (bits 24..27) is meaningless once entries have been
     * OR-ed together; only the R-Set bits are read out. */
    st->mask = (uint16_t)(acc & CLS_RSET_BITS);
    st->located = 0;
    st->start = pos;
    st->end = i;
}

/* The window mask for the suffix of the run that starts at `off` (spec
 * 6.1, step 1.a, applied to a position deeper inside an already scanned
 * run). `off` lies in [st->start, st->end). */
static uint16_t run_mask_from(const uint8_t *data, size_t off, run_state *st) {
    if (st->mask == 0 || off == st->start) return st->mask;

    if (!st->located) {
        /* Walking backward, the first occurrence met of a character is its
         * last occurrence, so one bit of `pending` is retired per hit and
         * the walk ends with the earliest of the last occurrences. */
        uint16_t pending = st->mask;
        size_t i = st->end;
        while (i > st->start && pending) {
            i--;
            uint32_t cls = ENC_CLASS[data[i]];
            uint32_t bit = cls & CLS_RSET_BITS;
            if (bit & pending) {
                st->last[(cls >> CLS_INDEX_SHIFT) & 0xFu] = i;
                pending = (uint16_t)(pending & ~bit);
            }
        }
        st->located = 1;
        st->hold = 0; /* no answer cached yet; off > 0 forces the rebuild */
    } else if (off <= st->hold) {
        return st->suffix_mask;
    }

    /* The answer cannot change until off passes the earliest of the last
     * occurrences still in it, so record that position and rebuild only
     * when it is reached -- at most RSET_COUNT times over a whole run,
     * rather than once per iteration of the encoder's main loop. */
    uint16_t m = 0;
    size_t hold = SIZE_MAX;
    for (int j = 0; j < RSET_COUNT; j++) {
        if (((st->mask >> j) & 1u) != 0 && st->last[j] >= off) {
            m = (uint16_t)(m | (1u << j));
            if (st->last[j] < hold) hold = st->last[j];
        }
    }
    st->suffix_mask = m;
    st->hold = hold;
    return m;
}

/* What Pass 2 found. */
typedef struct {
    size_t candidate_len; /* bytes of the window that form dp_candidate_prefix */
    size_t chars;         /* transformed characters written */
    size_t segments;      /* segments the greedy packing splits them into */
} dp_scan;

/* Pass 2 -- Boundary Finalization with Fixed Mask (spec 6.1, step 1.b),
 * with DP Output Segmentation (step 1.d) folded into the same pass.
 *
 * Re-scans `window` (buf[0..window_len)) byte-by-byte against the *fixed*
 * final_mask (== window_mask from Pass 1, never modified here), applying
 * Case i/ii/iii and the consecutive-escape limit to determine how many
 * leading bytes of window form dp_candidate_prefix, and writes the
 * transformed text to `xf`.
 *
 * Segmentation is greedy over the same byte sequence -- close the current
 * segment *before* adding a piece that would push it past
 * MAX_DP_OUTPUT_CHARS_PER_SIGNAL, so a boundary never falls inside a Case
 * ii escape pair -- which makes it a prefix computation like everything
 * else here, and lets it run in this loop instead of two more passes over
 * the window. `seg_ends[k]` receives the end offset of segment k within
 * `xf`; segment k is xf[seg_ends[k-1] .. seg_ends[k]).
 *
 * The caller guarantees room for 2*window_len characters in `xf` (no byte
 * produces more than an escape pair) and window_len/255 + 2 entries in
 * `seg_ends` (a closed segment always holds at least 510 characters). */
static void dp_pass2(const uint8_t *buf, size_t window_len, uint16_t final_mask,
                     uint8_t *xf, size_t *seg_ends, dp_scan *out) {
    /* A byte is escaped iff its trigger field meets this: its own bit for
     * a replacement character whose R-Set partner is in the window, or
     * DP_ESCAPE_ALWAYS for '~'. */
    uint32_t trigger = (uint32_t)final_mask | DP_ESCAPE_ALWAYS;
    int consecutive_escape_trigger_count = 0;
    size_t seg_len = 0, nseg = 0, i = 0;
    size_t byte_at_a_time_until = 0;
    uint8_t *w = xf;

    while (i < window_len) {
        /* Eight bytes at a time while none of them needs escaping. Escapes
         * are what make Pass 2 branch: without one, every byte contributes
         * exactly its translated character and exactly one to the segment
         * length, so eight of them are one OR-reduction, eight stores and a
         * single segment-length test. The OR of the eight trigger fields
         * answers "does any of these need escaping" in one comparison.
         *
         * A group that does need escaping is handed to the byte path in
         * full, rather than one byte at a time with a fresh eight-byte
         * probe in between, so escape-dense input pays for the failed probe
         * once per eight bytes instead of once per byte. */
        if (i >= byte_at_a_time_until && window_len - i >= 8 &&
            seg_len + 8 <= MAX_DP_OUTPUT_CHARS_PER_SIGNAL) {
            const uint8_t *p = buf + i;
            uint32_t t0 = DP_XLAT[p[0]], t1 = DP_XLAT[p[1]];
            uint32_t t2 = DP_XLAT[p[2]], t3 = DP_XLAT[p[3]];
            uint32_t t4 = DP_XLAT[p[4]], t5 = DP_XLAT[p[5]];
            uint32_t t6 = DP_XLAT[p[6]], t7 = DP_XLAT[p[7]];
            uint32_t any = t0 | t1 | t2 | t3 | t4 | t5 | t6 | t7;

            if ((DP_XLAT_TRIGGER(any) & trigger) == 0) {
                w[0] = DP_XLAT_CHAR(t0); w[1] = DP_XLAT_CHAR(t1);
                w[2] = DP_XLAT_CHAR(t2); w[3] = DP_XLAT_CHAR(t3);
                w[4] = DP_XLAT_CHAR(t4); w[5] = DP_XLAT_CHAR(t5);
                w[6] = DP_XLAT_CHAR(t6); w[7] = DP_XLAT_CHAR(t7);
                w += 8;
                i += 8;
                seg_len += 8;
                consecutive_escape_trigger_count = 0;
                continue;
            }
            byte_at_a_time_until = i + 8;
        }

        uint8_t b = buf[i];
        uint32_t t = DP_XLAT[b];
        i++;

        if (DP_XLAT_TRIGGER(t) & trigger) {
            /* Case ii: requires escaping, against the fixed final_mask. */
            if (++consecutive_escape_trigger_count > MAX_CONSECUTIVE_ESCAPES) {
                i--;   /* b is excluded along with the rest of the window */
                break; /* terminate scan */
            }
            if (seg_len + 2 > MAX_DP_OUTPUT_CHARS_PER_SIGNAL) {
                seg_ends[nseg++] = (size_t)(w - xf);
                seg_len = 0;
            }
            w[0] = (uint8_t)ESCAPE_CHAR;
            w[1] = b;
            w += 2;
            seg_len += 2;
            continue;
        }

        /* Case i (R-Set character, substituted) or Case iii (plain literal);
         * DP_XLAT holds the right character for both. */
        consecutive_escape_trigger_count = 0;
        if (seg_len + 1 > MAX_DP_OUTPUT_CHARS_PER_SIGNAL) {
            seg_ends[nseg++] = (size_t)(w - xf);
            seg_len = 0;
        }
        *w++ = DP_XLAT_CHAR(t);
        seg_len += 1;
    }
    if (seg_len > 0) seg_ends[nseg++] = (size_t)(w - xf);

    out->candidate_len = i;
    out->chars = (size_t)(w - xf);
    out->segments = nseg;
}

/* Emits each segment Pass 2 marked out as a 5-character signal (spec
 * section 9) followed by that segment's characters. */
static uint8_t *emit_dp_segments(uint8_t *w, const uint8_t *xf,
                                 const size_t *seg_ends, size_t nseg,
                                 uint16_t final_mask) {
    size_t start = 0;
    for (size_t k = 0; k < nseg; k++) {
        size_t end = seg_ends[k];
        size_t seg_len = end - start;
        uint64_t payload = ((uint64_t)final_mask << 9) | (uint64_t)seg_len;
        value_to_5chars_64(POW2_32 + payload, (char *)w);
        w += 5;
        memcpy(w, xf + start, seg_len);
        w += seg_len;
        start = end;
    }
    return w;
}

/* Offset of the first position at or after `from` where a Dynamic Passthrough
 * candidate could begin -- that is, the first position whose representable run
 * reaches MIN_PASSTHROUGH_BYTES -- or data_len if there is none.
 *
 * Why the caller wants it: a DP candidate is never longer than the
 * representable run it starts in, so before this offset the encoder is certain
 * to take the block-mode branch. Block mode over a whole number of 4-byte
 * groups is exactly the concatenation of the per-group results, so that whole
 * stretch can be encoded in one call instead of re-entering the mode decision
 * every 4 bytes. The output is unchanged.
 *
 * Why it can afford to look ahead: any MIN_PASSTHROUGH_BYTES consecutive
 * positions contain exactly one multiple of MIN_PASSTHROUGH_BYTES, so a run
 * that long cannot avoid a sampling lattice of that stride. Sampling instead of
 * scanning turns the lookahead from one table lookup per byte into one per 20
 * bytes on the input where it matters -- high-entropy data, where nearly every
 * sample lands on an unrepresentable byte and is rejected immediately. A sample
 * that does land in a run costs a walk to that run's bounds, and the walk
 * forward stops as soon as the threshold is reached. */
static size_t first_dp_capable_run(const uint8_t *data, size_t data_len,
                                   size_t from) {
    size_t p = from;
    while (p < data_len) {
        if (ENC_CLASS[data[p]] & CLS_UNREPRESENTABLE) {
            p += MIN_PASSTHROUGH_BYTES;
            continue;
        }

        /* Back to this run's start, but never before `from`: positions before
         * it are not the caller's concern. */
        size_t start = p;
        while (start > from &&
               !(ENC_CLASS[data[start - 1]] & CLS_UNREPRESENTABLE)) {
            start--;
        }

        /* Forward only until the threshold is settled either way. */
        size_t end = p;
        while (end < data_len) {
            if (ENC_CLASS[data[end]] & CLS_UNREPRESENTABLE) break;
            end++;
            if (end - start >= MIN_PASSTHROUGH_BYTES) return start;
        }

        /* Too short. Resume the lattice at this run's end; a later run of the
         * required length still cannot dodge it. */
        p = end;
        if (p == from) p++; /* defensive: always make progress */
    }
    return data_len;
}

/* What the output buffer is sized at up front, excluding growth.
 *
 * Block mode emits exactly 1.25 characters per byte (plus at most 2 for a
 * partial final group), so this is the exact size an input with no Dynamic
 * Passthrough in it needs -- which is every high-entropy input, the case
 * where the buffer is large enough for its size to matter. A DP segment
 * can exceed that budget, by at most 0.1875 characters per byte, so the
 * main loop checks the room it needs before each emit and grows on the
 * rare occasion that it has to.
 *
 * Sizing for DP's worst case instead -- 1.5n, which needs no check at all
 * -- measured the same. This is the smaller allocation of the two, and the
 * bound it rests on is one a reader can check in a line rather than an
 * argument about DP's worst margin, so it is the one kept. */
static size_t encode_capacity(size_t n) {
    return n + n / 4 + 16;
}

base85n_status base85n_encode(const uint8_t *data, size_t data_len,
                               char **out_str, size_t *out_len) {
    if (!out_str || !out_len) return BASE85N_ERR_INVALID_ARGUMENT;
    if (!data && data_len != 0) return BASE85N_ERR_INVALID_ARGUMENT;
    if (data_len > (SIZE_MAX - 16) / 2) return BASE85N_ERR_ALLOC;

    size_t cap = encode_capacity(data_len);
    uint8_t *out = (uint8_t *)malloc(cap);
    if (!out) return BASE85N_ERR_ALLOC;
    uint8_t *w = out;

    size_t off = 0; /* current front of intermediate_buffer within data */
    base85n_status status = BASE85N_OK;

    /* Pass 2's scratch, reused across iterations and sized by the largest
     * window seen so far. Allocating per iteration cost a malloc/free pair
     * for every 4 bytes of block-mode input.
     *
     * It starts on the stack, because the payloads this format exists for
     * are short -- a UUID, a header line, a log record -- and at that size
     * a malloc/free pair is a large share of the whole call. Anything whose
     * representable runs stay within SCRATCH_STACK_WINDOW bytes never
     * reaches the heap for scratch at all. */
    uint8_t xf_stack[SCRATCH_STACK_WINDOW * 2];
    size_t seg_stack[SCRATCH_STACK_WINDOW / 255 + 2];
    uint8_t *xf = xf_stack;      /* transformed characters */
    size_t *seg_ends = seg_stack; /* segment end offsets within xf */
    uint8_t *xf_heap = NULL;     /* non-NULL once the stack was outgrown */
    size_t *seg_heap = NULL;
    size_t scratch_window = SCRATCH_STACK_WINDOW;

    /* State of the representable run currently being consumed. run.end == 0
     * with off == 0 forces the first scan. */
    run_state run;
    run.end = 0;
    run.start = 0;
    run.hold = 0;
    run.mask = 0;
    run.suffix_mask = 0;
    run.located = 0;

    while (off < data_len) {
        const uint8_t *buf = data + off;
        size_t buf_len = data_len - off;

        if (off >= run.end) {
            /* Entering a run that has not been scanned yet. The final
             * block-mode branch below ignores representability and can step
             * past run.end, in which case we land in a later run and scan
             * that one; runs handled this way are disjoint, so total
             * scanning work stays O(data_len). */
            run_scan(data, data_len, off, &run);
        }
        size_t window_len = run.end - off;
        uint16_t final_mask = run_mask_from(data, off, &run);

        dp_scan dp;
        dp.candidate_len = 0;
        dp.chars = 0;
        dp.segments = 0;
        int use_dp_mode = 0;

        if (window_len > 0) {
            if (window_len > scratch_window) {
                /* Double, but never past a value whose `grown * 2` would
                 * wrap. window_len itself always fits, since data_len is
                 * capped at (SIZE_MAX - 16) / 2 on entry; only the doubling
                 * can run away. Reaching a scratch_window large enough to
                 * wrap would already require an earlier realloc of half the
                 * address space to have succeeded, so the clamp is belt and
                 * braces -- but it is one comparison on a path taken a
                 * handful of times per call, and it makes the bound local
                 * instead of an argument about what allocations can't
                 * happen. */
                size_t grown = scratch_window <= SIZE_MAX / 4
                                   ? scratch_window * 2
                                   : SIZE_MAX / 2;
                if (grown < window_len) grown = window_len;
                /* realloc of the stack buffers is not a thing, so the first
                 * growth allocates; xf holds nothing that has to survive it. */
                uint8_t *nxf = (uint8_t *)realloc(xf_heap, grown * 2);
                if (!nxf) { status = BASE85N_ERR_ALLOC; break; }
                xf_heap = xf = nxf;
                size_t *nseg = (size_t *)realloc(seg_heap,
                                                 (grown / 255 + 2) * sizeof *seg_ends);
                if (!nseg) { status = BASE85N_ERR_ALLOC; break; }
                seg_heap = seg_ends = nseg;
                scratch_window = grown;
            }
            dp_pass2(buf, window_len, final_mask, xf, seg_ends, &dp);

            if (dp.candidate_len >= MIN_PASSTHROUGH_BYTES) {
                size_t conceptual_dp_output_length = dp.segments * 5 + dp.chars;
                size_t block_mode_output_length = ((dp.candidate_len + 3) / 4) * 5;
                if (conceptual_dp_output_length <= block_mode_output_length) {
                    use_dp_mode = 1;
                }
            }
        }

        size_t consumed, need;
        if (use_dp_mode) {
            consumed = dp.candidate_len;
            need = dp.segments * 5 + dp.chars;
        } else {
            /* spec section 6.1, step 2.b: block-encode only the exact
             * multiple-of-4 leading portion of dp_candidate_prefix now; any
             * 0-3 trailing bytes are deferred, unpadded, to the next loop
             * iteration rather than treated as a premature partial block. */
            if (dp.candidate_len >= 4) {
                consumed = (dp.candidate_len / 4) * 4;
            } else {
                /* Fewer than 4 candidate bytes. This is the branch that can
                 * consume past the end of the current run. */
                consumed = buf_len < 4 ? buf_len : 4;
            }
            /* Extend the block-mode run as far as DP provably cannot apply.
             * Only worth trying when the current window is itself too short
             * for DP -- inside a long representable run the scan below would
             * return immediately and cost a rescan for nothing. */
            if (window_len < MIN_PASSTHROUGH_BYTES) {
                size_t limit = first_dp_capable_run(data, data_len, off);
                size_t batch = ((limit - off) / 4) * 4;
                if (batch > consumed) {
                    consumed = batch;
                    /* The batch may end mid-run, so the cached run state no
                     * longer describes the new position. */
                    run.end = 0;
                }
            }
            need = (consumed / 4) * 5 + (consumed % 4 ? consumed % 4 + 1 : 0);
        }

        /* The only capacity test in the encoder, once per emit rather than
         * once per character, and taken only when Dynamic Passthrough has
         * spent more than block mode's 1.25 characters per byte. */
        size_t used = (size_t)(w - out);
        if (need + 1 > cap - used) {
            size_t want = used + need + 1;
            if (want < used) { status = BASE85N_ERR_ALLOC; break; }
            /* A quarter of headroom, so repeated growth stays amortised
             * without overshooting far past what the input needs. */
            size_t newcap = want <= SIZE_MAX - want / 4 ? want + want / 4 : SIZE_MAX;
            uint8_t *grown = (uint8_t *)realloc(out, newcap);
            if (!grown) { status = BASE85N_ERR_ALLOC; break; }
            out = grown;
            cap = newcap;
            w = out + used;
        }

        if (use_dp_mode) {
            w = emit_dp_segments(w, xf, seg_ends, dp.segments, final_mask);
        } else {
            w = process_block_mode(buf, consumed, w);
        }

        off += consumed;
    }

    free(xf_heap);
    free(seg_heap);

    if (status != BASE85N_OK) {
        free(out);
        return status;
    }

    size_t produced = (size_t)(w - out);
    out[produced] = 0; /* NUL-terminate */

    /* Hand back the slack the initial sizing reserved but the input did
     * not need. A shrinking realloc cannot fail usefully, so its failure
     * just means the caller keeps the roomier buffer. */
    uint8_t *trimmed = (uint8_t *)realloc(out, produced + 1);
    if (trimmed) out = trimmed;

    *out_len = produced;
    *out_str = (char *)out;
    return BASE85N_OK;
}

/* ------------------------------------------------------------------ */
/* Decoding                                                             */
/* ------------------------------------------------------------------ */

/* Decodes `n` characters of `in`, which must already be free of the
 * inter-token whitespace section 7.1 allows, into `out`. `out` must have
 * room for `n` bytes: a 5-character group yields 4 bytes, a DP segment at
 * most 1 byte per character, so no input character ever yields more than
 * one byte. Returns the number of bytes produced through `produced`.
 *
 * `out` may alias `in` exactly (out == in); the whitespace retry in
 * base85n_decode below decodes in place on the strength of it. The writer
 * never catches the reader: a 5-character group is loaded into registers
 * before any of its 4 bytes are stored, a partial final group likewise,
 * and inside a DP segment the writer trails the reader by at least the
 * segment's own 5-character signal, which produced no output of its own.
 * Any other overlap is undefined, as usual. */
static base85n_status decode_scan(const uint8_t *in, size_t n, uint8_t *out,
                                  size_t *produced) {
    uint8_t *w = out;
    size_t pos = 0;

    while (pos < n) {
        size_t remaining = n - pos;

        if (remaining >= 5) {
            const uint8_t *p = in + pos;
            /* ALPHABET_VALUE's -1 reads back as 0xFF, and every real digit
             * value is below 0x80, so one test covers all five characters. */
            uint32_t v0 = (uint8_t)ALPHABET_VALUE[p[0]];
            uint32_t v1 = (uint8_t)ALPHABET_VALUE[p[1]];
            uint32_t v2 = (uint8_t)ALPHABET_VALUE[p[2]];
            uint32_t v3 = (uint8_t)ALPHABET_VALUE[p[3]];
            uint32_t v4 = (uint8_t)ALPHABET_VALUE[p[4]];
            if ((v0 | v1 | v2 | v3 | v4) & 0x80u) return BASE85N_ERR_INVALID_CHAR;

            /* Horner's rule would chain five multiplies end to end; weighing
             * the digits directly leaves them independent. Only the top term
             * can leave 32 bits. */
            uint64_t decoded_value =
                (uint64_t)v0 * POW85_4 +
                (v1 * POW85_3 + v2 * POW85_2 + v3 * 85u + v4);
            pos += 5;

            if (decoded_value < POW2_32) {
                /* Standard Base85N block: 4 bytes, Big-Endian. */
                uint32_t v32 = (uint32_t)decoded_value;
                w[0] = (uint8_t)(v32 >> 24);
                w[1] = (uint8_t)(v32 >> 16);
                w[2] = (uint8_t)(v32 >> 8);
                w[3] = (uint8_t)v32;
                w += 4;
                continue;
            }

            uint64_t signal_payload = decoded_value - POW2_32;
            if (signal_payload > SIGNAL_PAYLOAD_MAX) {
                return BASE85N_ERR_RESERVED_SIGNAL;
            }
            uint32_t mask13 = (uint32_t)((signal_payload >> 9) & 0x1FFFu) << 16;
            size_t l_enc = (size_t)(signal_payload & 0x1FFu);

            if (n - pos < l_enc) return BASE85N_ERR_UNEXPECTED_EOF;

            const uint8_t *q = in + pos;
            const uint8_t *qend = q + l_enc;
            while (q < qend) {
                uint8_t c = *q++;
                uint32_t t = DEC_SUB[c];
                if (t & (DEC_INVALID | DEC_ESCAPE)) {
                    if (t & DEC_INVALID) return BASE85N_ERR_INVALID_CHAR;
                    /* '~': the next character stands for itself. */
                    if (q >= qend) return BASE85N_ERR_DANGLING_ESCAPE;
                    uint8_t c2 = *q++;
                    if (DEC_SUB[c2] & DEC_INVALID) return BASE85N_ERR_INVALID_CHAR;
                    *w++ = c2;
                    continue;
                }
                /* A replacement character stands for its R-Set byte exactly
                 * while the signal's mask says the window contained it. */
                *w++ = (t & mask13) ? (uint8_t)t : c;
            }
            pos += l_enc;
        } else if (remaining == 1) {
            /* A lone trailing Alphabet-N character cannot be a valid
             * partial block (minimum 2 chars needed to represent 1
             * original byte). */
            return BASE85N_ERR_INVALID_PARTIAL_BLOCK;
        } else {
            /* remaining is 2, 3, or 4: final partial block. */
            int vals[5];
            for (size_t k = 0; k < remaining; k++) {
                int v = ALPHABET_VALUE[in[pos + k]];
                if (v < 0) return BASE85N_ERR_INVALID_CHAR;
                vals[k] = v;
            }
            for (size_t k = remaining; k < 5; k++) vals[k] = 84; /* pad with '#' */

            uint64_t decoded_value = 0;
            for (int k = 0; k < 5; k++) {
                decoded_value = decoded_value * 85 + (uint64_t)vals[k];
            }
            uint32_t v32 = (uint32_t)(decoded_value & 0xFFFFFFFFu);
            uint8_t bytes[4] = {
                (uint8_t)(v32 >> 24), (uint8_t)(v32 >> 16),
                (uint8_t)(v32 >> 8), (uint8_t)v32
            };
            size_t nbytes = remaining - 1; /* 1, 2, or 3 */
            memcpy(w, bytes, nbytes);
            w += nbytes;
            pos += remaining;
        }
    }

    *produced = (size_t)(w - out);
    return BASE85N_OK;
}

base85n_status base85n_decode(const char *s, size_t s_len,
                               uint8_t **out_data, size_t *out_len) {
    if (!out_data || !out_len) return BASE85N_ERR_INVALID_ARGUMENT;
    if (!s && s_len != 0) return BASE85N_ERR_INVALID_ARGUMENT;

    const uint8_t *in = (const uint8_t *)s;

    /* One allocation, sized by the bound in decode_scan, so the decode
     * loop never has to test capacity. Zero-length input still gets a
     * valid, free()-able pointer. */
    uint8_t *out = (uint8_t *)malloc(s_len ? s_len : 1);
    if (!out) return BASE85N_ERR_ALLOC;

    size_t produced = 0;
    base85n_status status = decode_scan(in, s_len, out, &produced);

    /* Section 7.1 has the decoder ignore inter-token whitespace. Rather
     * than copy every input to strip characters that a valid stream from
     * this library's own encoder never contains, take the rejection as the
     * signal: none of the four whitespace bytes is in Alphabet-N, and
     * decode_scan validates every character it consumes, so a stream with
     * whitespace in it can never decode successfully. Only once it has
     * failed is it worth building the filtered copy and decoding again.
     *
     * The retry is on any failure, not just BASE85N_ERR_INVALID_CHAR:
     * whitespace also shifts the group boundaries after it, so it can
     * equally well surface as a truncated final group or a short DP
     * segment.
     *
     * A single global strip is equivalent to stripping only "between
     * tokens" as literally worded: whitespace never appears as meaningful
     * content in a valid stream, since R-Set occurrences are always
     * substituted away in DP output.
     *
     * The filtered copy goes into `out` and is decoded in place, rather
     * than into a second s_len buffer. `out` is already big enough --
     * stripping only ever shortens -- and the first attempt's partial
     * output is being discarded anyway, so the retry costs no allocation
     * at all. That matters because the retry is reachable from untrusted
     * input: a caller handed a long, otherwise valid stream with one
     * trailing space pays for the failed first scan either way, but it
     * should not also double the decoder's peak footprint. */
    if (status != BASE85N_OK) {
        size_t i;
        for (i = 0; i < s_len; i++) {
            if (is_ignorable_ws(in[i])) break;
        }
        if (i < s_len) {
            /* Everything before the first whitespace copies wholesale. */
            memcpy(out, in, i);
            size_t n = i;
            for (size_t k = i; k < s_len; k++) {
                if (!is_ignorable_ws(in[k])) out[n++] = in[k];
            }
            status = decode_scan(out, n, out, &produced);
        }
    }

    if (status != BASE85N_OK) {
        free(out);
        return status;
    }

    uint8_t *trimmed = (uint8_t *)realloc(out, produced ? produced : 1);
    if (trimmed) out = trimmed;

    *out_data = out;
    *out_len = produced;
    return BASE85N_OK;
}

/* ------------------------------------------------------------------ */
/* Misc                                                                 */
/* ------------------------------------------------------------------ */

const char *base85n_strerror(base85n_status status) {
    switch (status) {
        case BASE85N_OK: return "ok";
        case BASE85N_ERR_INVALID_CHAR: return "invalid character (not in Alphabet-N)";
        case BASE85N_ERR_UNEXPECTED_EOF: return "unexpected end of stream";
        case BASE85N_ERR_DANGLING_ESCAPE: return "dangling escape character";
        case BASE85N_ERR_RESERVED_SIGNAL: return "reserved/undefined DP signal value";
        case BASE85N_ERR_INVALID_PARTIAL_BLOCK: return "invalid partial final block";
        case BASE85N_ERR_ALLOC: return "memory allocation failure";
        case BASE85N_ERR_INVALID_ARGUMENT: return "invalid argument";
        default: return "unknown error";
    }
}
