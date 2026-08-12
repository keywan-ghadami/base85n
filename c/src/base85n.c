/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/*
 * base85n.c - Implementation of the Base85N binary-to-text encoding
 * scheme, per the spec, including Section 4.2's eight replacement
 * alphabets and Section 6.1's single-scan Dynamic Prefix Identification.
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

/* The R-Set (spec 4.1), by R-Set index j:
 *
 *   j     0    1    2    3    4    5    6    7    8    9   10   11   12
 *   R    ' '  '"'  '\'' ','  ';'  '\\' '|'  '<'  '>'  '&'  \t   \n   \r
 *
 * Which of them a DP segment carries, and which Alphabet-N character each
 * is written as, depends on the segment's replacement alphabet (spec 4.2);
 * see ENC_SUB and DEC_SUB below. tests/test_base85n.c holds an independent
 * copy of all eight alphabets and checks every substitution through the
 * public API. */

#define NUM_ALPHABETS BASE85N_NUM_ALPHABETS

/* Distinct characters spent as donors across all eight alphabets, which is
 * what DONOR_INDEX numbers and DEC_SUB is indexed by. */
#define DONOR_SLOTS 14

#define MAX_DP_ANALYSIS_BYTES BASE85N_MAX_DP_ANALYSIS_BYTES
#define MAX_DP_OUTPUT_CHARS_PER_SIGNAL BASE85N_MAX_DP_OUTPUT_CHARS_PER_SIGNAL
#define MIN_PASSTHROUGH_BYTES BASE85N_MIN_PASSTHROUGH_BYTES

#define POW2_32 ((uint64_t)1u << 32)
#define SIGNAL_PAYLOAD_MAX ((uint64_t)(1u << 13) - 1u) /* 2^13 - 1 */

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

/* The tables the three inner loops are made of.
 *
 * REPR is the encoder's hot lookup: bit a is set iff the byte is
 * representable under replacement alphabet a (spec 6.1, step 1). One load
 * per byte settles all eight scans at once -- the encoder walks forward
 * AND-ing this into a live set, and an alphabet's run ends exactly where
 * its bit leaves that set.
 *
 * The substitutions themselves are small and structured, so they are kept
 * that way rather than expanded into eight 256-entry translation tables:
 * ENC_SUB[a][j] is the donor character alphabet a writes R_Char[j] as (0
 * if it does not carry that R-Set character), and DEC_SUB[a][slot] is the
 * R-Set byte alphabet a reads that donor back as (0 if it does not spend
 * that donor). RSET_INDEX and DONOR_INDEX map a byte to the j or the slot
 * to index them with, or -1.
 */

static const uint8_t REPR[256] = {
    0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0xA2u, 0xFEu, 0x00u, 0x00u, 0x92u, 0x00u, 0x00u,
    0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
    0xFEu, 0x03u, 0xFCu, 0x3Fu, 0x01u, 0x01u, 0xC8u, 0xECu, 0xFFu, 0xFFu, 0x7Fu, 0x7Fu, 0xBCu, 0xFFu, 0xFFu, 0xFFu,
    0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xE4u, 0x88u, 0x7Fu, 0xA8u, 0x03u,
    0x01u, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu,
    0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xD0u, 0xFFu, 0x01u, 0x7Fu,
    0x5Fu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu,
    0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xF7u, 0xC0u, 0xFFu, 0x17u, 0x00u,
    0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
    0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
    0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
    0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
    0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
    0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
    0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
    0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
};

static const int8_t RSET_INDEX[256] = {
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  10,  11,  -1,  -1,  12,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
      0,  -1,   1,  -1,  -1,  -1,   9,   2,  -1,  -1,  -1,  -1,   3,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,   4,   7,  -1,   8,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,   5,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,   6,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
};

static const int8_t DONOR_INDEX[256] = {
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,   0,  -1,   1,   2,   3,  -1,  -1,  -1,  -1,   4,   5,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,   6,  -1,   7,
      8,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,   9,  10,
     11,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  12,  -1,  -1,  13,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
};

static const uint8_t ENC_SUB[NUM_ALPHABETS][RSET_COUNT] = {
    /* 0 none   */ { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    /* 1 text   */ { '^', 0, 0, 0, 0, 0, 0, 0, 0, 0, '$', '@', '%' },
    /* 2 prose  */ { '^', '$', '?', '%', '!', 0, 0, 0, 0, 0, 0, '@', 0 },
    /* 3 markup */ { '^', '!', '~', '{', 0, 0, 0, '%', '$', '?', 0, '@', 0 },
    /* 4 json   */ { '^', '%', 0, '$', 0, '?', 0, 0, 0, 0, 0, '@', '!' },
    /* 5 code   */ { '^', '?', '!', '%', '$', 0, 0, 0, '`', 0, '~', '@', 0 },
    /* 6 shell  */ { '^', '?', '!', 0, '#', '$', '%', 0, 0, '~', 0, '@', 0 },
    /* 7 full   */ { '^', '~', '#', '?', '!', '`', '_', '*', '+', '=', '$', '@', '%' },
};

static const uint8_t DEC_SUB[NUM_ALPHABETS][DONOR_SLOTS] = {
    /* 0 none   */ {   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0 },
    /* 1 text   */ {   0,   0,   9,  13,   0,   0,   0,   0,  10,  32,   0,   0,   0,   0 },
    /* 2 prose  */ {  59,   0,  34,  44,   0,   0,   0,  39,  10,  32,   0,   0,   0,   0 },
    /* 3 markup */ {  34,   0,  62,  60,   0,   0,   0,  38,  10,  32,   0,   0,  44,  39 },
    /* 4 json   */ {  13,   0,  44,  34,   0,   0,   0,  92,  10,  32,   0,   0,   0,   0 },
    /* 5 code   */ {  39,   0,  59,  44,   0,   0,   0,  34,  10,  32,   0,  62,   0,   9 },
    /* 6 shell  */ {  39,  59,  92, 124,   0,   0,   0,  34,  10,  32,   0,   0,   0,  38 },
    /* 7 full   */ {  59,  39,   9,  13,  60,  62,  38,  44,  10,  32, 124,  92,   0,  34 },
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

/* Dynamic Prefix Identification (spec 6.1, step 1), resolved for all eight
 * replacement alphabets in a single walk from `pos`.
 *
 * Writes the length of the longest representable prefix to *best_len and the
 * identifier of the alphabet achieving it to *best_alphabet, the numerically
 * smallest such identifier winning a tie as the spec requires. The length is
 * capped at MAX_DP_ANALYSIS_BYTES.
 *
 * Asking the alphabets one at a time would walk the window eight times.
 * Instead `live` carries the alphabets that have represented every byte so
 * far; REPR gives that set for a byte in one load, so the walk is an AND per
 * byte and an alphabet's run ends exactly where its bit leaves the set.
 *
 * This is also what satisfies spec section 6.6 without carrying any state
 * between iterations of the encoding loop -- which is what the 0.2.0 encoder
 * needed its run_state for. The walk costs *best_len byte inspections, and the
 * caller then consumes *best_len bytes under Dynamic Passthrough or at least
 * *best_len - 3 under block mode, so the work per byte of input is bounded by
 * a small constant rather than by the window size. */
static void scan_alphabets(const uint8_t *buf, size_t buf_len,
                           size_t *best_len, unsigned *best_alphabet) {
    size_t limit = buf_len < MAX_DP_ANALYSIS_BYTES ? buf_len : MAX_DP_ANALYSIS_BYTES;
    size_t stop[NUM_ALPHABETS];
    for (unsigned a = 0; a < NUM_ALPHABETS; a++) stop[a] = 0;

    unsigned live = (1u << NUM_ALPHABETS) - 1u;
    size_t i = 0;
    while (i < limit) {
        unsigned next = live & REPR[buf[i]];
        if (next != live) {
            unsigned dropped = live & ~next;
            while (dropped) {
                unsigned bit = dropped & (unsigned)(-(int)dropped);
                unsigned a = 0;
                while ((bit >> a) != 1u) a++;
                stop[a] = i;
                dropped &= dropped - 1u;
            }
            live = next;
            if (live == 0) break;
        }
        i++;
    }
    /* Whatever is still live reaches the end of the window. */
    while (live) {
        unsigned bit = live & (unsigned)(-(int)live);
        unsigned a = 0;
        while ((bit >> a) != 1u) a++;
        stop[a] = i;
        live &= live - 1u;
    }

    size_t bl = 0;
    unsigned ba = 0;
    for (unsigned a = 0; a < NUM_ALPHABETS; a++) {
        /* Strictly greater keeps the smallest identifier on a tie. */
        if (stop[a] > bl) {
            bl = stop[a];
            ba = a;
        }
    }
    *best_len = bl;
    *best_alphabet = ba;
}

/* Emit one DP segment: its 5-character signal (spec section 9, with the
 * length field biased by one) followed by the transformed bytes. */
static uint8_t *emit_dp_segment(uint8_t *w, const uint8_t *buf, size_t len,
                                unsigned alphabet) {
    uint64_t payload = ((uint64_t)alphabet << 10) | (uint64_t)(len - 1);
    value_to_5chars_64(POW2_32 + payload, (char *)w);
    w += 5;

    const uint8_t *sub = ENC_SUB[alphabet];
    for (size_t i = 0; i < len; i++) {
        uint8_t b = buf[i];
        int8_t j = RSET_INDEX[b];
        *w++ = j < 0 ? b : sub[j];
    }
    return w;
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

    /* Start of the pending run of block-mode bytes, or SIZE_MAX for none.
     * Consecutive block-mode iterations are converted in one call instead of
     * four bytes at a time. This does not change which positions the loop
     * visits: every block-mode consumption is a whole number of 4-byte
     * groups, so the concatenation of the per-iteration results is exactly
     * the block-mode encoding of the accumulated range.
     *
     * Version 0.2.0 needed a reusable Pass 2 scratch buffer here, because a
     * candidate's transformed length was not known until it had been built.
     * A 0.3.0 segment is one character per byte, so it is written straight
     * into the output buffer and no scratch exists to size or grow. */
    size_t block_start = SIZE_MAX;

    while (off < data_len) {
        const uint8_t *buf = data + off;
        size_t buf_len = data_len - off;

        size_t best_len;
        unsigned best_alphabet;
        scan_alphabets(buf, buf_len, &best_len, &best_alphabet);

        /* Step 2.a. At MIN_PASSTHROUGH_BYTES the two modes cost the same 25
         * characters and Dynamic Passthrough only gains from there, so the
         * length test settles the size comparison too. */
        int use_dp_mode = best_len >= MIN_PASSTHROUGH_BYTES;

        size_t consumed, need, pending;
        if (use_dp_mode) {
            consumed = best_len;
            pending = block_start == SIZE_MAX ? 0 : off - block_start;
            need = (pending / 4) * 5 + (pending % 4 ? pending % 4 + 1 : 0)
                   + 5 + best_len;
        } else {
            /* spec section 6.1, step 2.b: block-encode only the exact
             * multiple-of-4 leading portion of the candidate now; any 1-3
             * trailing bytes are deferred, unpadded, to the next loop
             * iteration rather than treated as a premature partial block. */
            if (best_len >= 4) {
                consumed = (best_len / 4) * 4;
            } else {
                /* Fewer than 4 representable bytes under every alphabet.
                 * This is the branch that ignores representability. */
                consumed = buf_len < 4 ? buf_len : 4;
            }
            pending = 0;
            need = 0;
        }

        /* The only capacity test in the encoder, once per emit rather than
         * once per character. */
        if (need > 0) {
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
        }

        if (use_dp_mode) {
            if (block_start != SIZE_MAX) {
                w = process_block_mode(data + block_start, pending, w);
                block_start = SIZE_MAX;
            }
            w = emit_dp_segment(w, buf, best_len, best_alphabet);
        } else if (block_start == SIZE_MAX) {
            block_start = off;
        }

        off += consumed;
    }

    if (status == BASE85N_OK && block_start != SIZE_MAX) {
        size_t pending = off - block_start;
        size_t need = (pending / 4) * 5 + (pending % 4 ? pending % 4 + 1 : 0);
        size_t used = (size_t)(w - out);
        if (need + 1 > cap - used) {
            size_t want = used + need + 1;
            size_t newcap = want <= SIZE_MAX - want / 4 ? want + want / 4 : SIZE_MAX;
            uint8_t *grown = (uint8_t *)realloc(out, newcap);
            if (!grown) {
                status = BASE85N_ERR_ALLOC;
            } else {
                out = grown;
                cap = newcap;
                w = out + used;
            }
        }
        if (status == BASE85N_OK) {
            w = process_block_mode(data + block_start, pending, w);
        }
    }

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
 * room for `n` bytes: a 5-character group yields 4 bytes, a DP segment
 * exactly 1 byte per character, so no input character ever yields more than
 * one byte. Returns the number of bytes produced through `produced`.
 *
 * `out` may alias `in` exactly (out == in); the whitespace retry in
 * base85n_decode below decodes in place on the strength of it. The writer
 * never catches the reader: a 5-character group is loaded into registers
 * before any of its 4 bytes are stored, a partial final group likewise,
 * and inside a DP segment the writer trails the reader by the segment's own
 * 5-character signal, which produced no output of its own, and stays exactly
 * that far behind since each character yields one byte. Any other overlap is
 * undefined, as usual. */
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
            unsigned alphabet = (unsigned)((signal_payload >> 10) & 0x7u);
            /* Spec section 9: the length field is stored biased by one, so
             * the shortest segment a signal can name is 1 character and the
             * longest 1024. */
            size_t seg_len = (size_t)(signal_payload & 0x3FFu) + 1;

            if (n - pos < seg_len) return BASE85N_ERR_UNEXPECTED_EOF;

            /* Section 7.1.e: one character in, one byte out, with no state
             * carried between characters -- 0.3.0 has no construct that
             * spends two characters on one byte. */
            const uint8_t *dec = DEC_SUB[alphabet];
            const uint8_t *q = in + pos;
            const uint8_t *qend = q + seg_len;
            while (q < qend) {
                uint8_t c = *q++;
                if (ALPHABET_VALUE[c] < 0) return BASE85N_ERR_INVALID_CHAR;
                int8_t slot = DONOR_INDEX[c];
                uint8_t sub = slot < 0 ? 0 : dec[slot];
                *w++ = sub ? sub : c;
            }
            pos += seg_len;
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
            /* Spec 7.1: the padded group's value must be below 2^32. The
             * encoder truncates a group whose value already is, and re-padding
             * with '#' raises it by at most 614124, so a group that crosses
             * 2^32 cannot be this format's output. Reducing it modulo 2^32
             * instead would accept several character sequences as encodings of
             * the same bytes. */
            if (decoded_value >= POW2_32) {
                return BASE85N_ERR_INVALID_PARTIAL_BLOCK;
            }
            uint32_t v32 = (uint32_t)decoded_value;
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
        case BASE85N_ERR_RESERVED_SIGNAL: return "reserved/undefined DP signal value";
        case BASE85N_ERR_INVALID_PARTIAL_BLOCK: return "invalid partial final block";
        case BASE85N_ERR_ALLOC: return "memory allocation failure";
        case BASE85N_ERR_INVALID_ARGUMENT: return "invalid argument";
        default: return "unknown error";
    }
}
