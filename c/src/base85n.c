/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/*
 * base85n.c - Implementation of the Base85N binary-to-text encoding
 * scheme, per the spec: section 4.2's donor profiles, section 6's
 * encoding procedure with its Fill and Dynamic Passthrough scans, and
 * section 7's decoder.
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

#define RSET_COUNT BASE85N_RSET_LEN

/* The R-Set (spec 4.1), by R-Set index j:
 *
 *   j     0    1    2    3    4    5    6    7    8    9   10   11   12
 *   R    ' '  '"'  '\'' ','  ';'  '\\' '|'  '<'  '>'  '&'  \t   \n   \r
 *
 * Which of them a DP segment carries is named by its 13-bit mask; which
 * Alphabet-N character each is written as follows from the segment's donor
 * profile (spec 4.3). tests/test_base85n.c holds an independent copy of the
 * profile table and checks every substitution through the public API. */

#define NUM_PROFILES BASE85N_NUM_PROFILES

/* Distinct characters spent as donors across all eight profiles, which is
 * what DONOR_INDEX numbers and RANK_PACKED is indexed by. */
#define DONOR_SLOTS 22

#define MAX_DP_ANALYSIS_BYTES BASE85N_MAX_DP_ANALYSIS_BYTES
#define MAX_DP_SEGMENT_CHARS BASE85N_MAX_DP_SEGMENT_CHARS
#define MIN_PASSTHROUGH_BYTES BASE85N_MIN_PASSTHROUGH_BYTES
#define MIN_FILL_BYTES BASE85N_MIN_FILL_BYTES
#define MIN_FILL_IN_SEGMENT_BYTES BASE85N_MIN_FILL_IN_SEGMENT_BYTES
#define MAX_FILL_BYTES BASE85N_MAX_FILL_BYTES

/* Section 9's signal ranges. Block mode occupies 0 .. 2^32; DP takes the
 * next 2^27 values (3 profile + 13 mask + 11 length bits); Solid Fill the
 * next 2^19 (8 byte-value + 11 length bits); everything above that is
 * FUTURE_SIGNAL_SPACE and must be rejected. */
#define POW2_32 ((uint64_t)1u << 32)
#define FILL_SIGNAL_BASE (POW2_32 + ((uint64_t)1u << 27))
#define FUTURE_SIGNAL_BASE (FILL_SIGNAL_BASE + ((uint64_t)1u << 19))

/* ------------------------------------------------------------------ */
/* Byte-indexed lookup tables                                          */
/* ------------------------------------------------------------------ */

/* Every inner loop in this file classifies one input byte per iteration,
 * so each of these tables answers, in a single load, every question its
 * loop needs to ask about that byte. They are `const` and built at compile
 * time rather than lazily, which keeps the library free of shared mutable
 * state and makes the thread-safety promise in base85n.h unconditional.
 *
 * tests/test_base85n.c drives every entry through the public API and checks
 * the result against ALPHABET_N_CHARS_STR, the R-Set and the profile table,
 * so a typo here cannot survive `make test`. */

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

/* ASCII byte -> R-Set index j (0-12), or -1. */
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

/* 1 for a byte a DP segment could carry -- Alphabet-N or R-Set -- and 0 for
 * one that ends any segment it appears in. This is the encoder's lookahead
 * table: it answers the only question the skip below has to ask per byte. */
static const uint8_t REPRESENTABLE[256] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 1, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

/* The eight donor profiles of spec 4.2, each an ordered sequence of 13
 * distinct Alphabet-N characters. A segment whose mask has k bits set
 * spends the profile's first k characters, in mask-bit order. */
static const char PROFILES[NUM_PROFILES][RSET_COUNT + 1] = {
    "~^?%@+`$#!*.-",
    "~^+[]`?@!%#*(",
    "^~$#?%!`@[]+_",
    "~+?%@!^[]:`()",
    "~%^`+?!$@(){}",
    "^~?@!+%*$()_#",
    "^~@%?$+!#[]=*",
    "^$~@?!%`[]:}{",
};

/* ASCII byte -> donor slot (0-21), or -1 for a byte no profile spends.
 * Only 22 of the 85 alphabet characters appear in any profile, so this is
 * also the encoder's fast "does this literal constrain anything?" test. */
static const int8_t DONOR_INDEX[256] = {
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,   0,  -1,   1,   2,   3,  -1,  -1,   4,   5,   6,   7,  -1,   8,   9,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  10,  -1,  -1,  11,  -1,  12,
     13,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  14,  -1,  15,  16,  17,
     18,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  19,  -1,  20,  21,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
};

/* The rank a donor character holds in each of the eight profiles, packed
 * one profile per byte lane so that the prefix scan can compare all eight
 * at once. 13 -- one past the last real rank -- means the profile does not
 * contain the character at all, so "absent" and "ranked below no possible
 * k" are the same value. */
#define PACK(r0, r1, r2, r3, r4, r5, r6, r7)                                   \
    ((uint64_t)(r0) | ((uint64_t)(r1) << 8) | ((uint64_t)(r2) << 16) |         \
     ((uint64_t)(r3) << 24) | ((uint64_t)(r4) << 32) | ((uint64_t)(r5) << 40) | \
     ((uint64_t)(r6) << 48) | ((uint64_t)(r7) << 56))

static const uint64_t RANK_PACKED[DONOR_SLOTS] = {
    /*  0 '!' */ PACK( 9,  8,  6,  5,  6,  4,  7,  5),
    /*  1 '#' */ PACK( 8, 10,  3, 13, 13, 12,  8, 13),
    /*  2 '$' */ PACK( 7, 13,  2, 13,  7,  8,  5,  1),
    /*  3 '%' */ PACK( 3,  9,  5,  3,  1,  6,  3,  6),
    /*  4 '(' */ PACK(13, 12, 13, 11,  9,  9, 13, 13),
    /*  5 ')' */ PACK(13, 13, 13, 12, 10, 10, 13, 13),
    /*  6 '*' */ PACK(10, 11, 13, 13, 13,  7, 12, 13),
    /*  7 '+' */ PACK( 5,  2, 11,  1,  4,  5,  6, 13),
    /*  8 '-' */ PACK(12, 13, 13, 13, 13, 13, 13, 13),
    /*  9 '.' */ PACK(11, 13, 13, 13, 13, 13, 13, 13),
    /* 10 ':' */ PACK(13, 13, 13,  9, 13, 13, 13, 10),
    /* 11 '=' */ PACK(13, 13, 13, 13, 13, 13, 11, 13),
    /* 12 '?' */ PACK( 2,  6,  4,  2,  5,  2,  4,  4),
    /* 13 '@' */ PACK( 4,  7,  8,  4,  8,  3,  2,  3),
    /* 14 '[' */ PACK(13,  3,  9,  7, 13, 13,  9,  8),
    /* 15 ']' */ PACK(13,  4, 10,  8, 13, 13, 10,  9),
    /* 16 '^' */ PACK( 1,  1,  0,  6,  2,  0,  0,  0),
    /* 17 '_' */ PACK(13, 13, 12, 13, 13, 11, 13, 13),
    /* 18 '`' */ PACK( 6,  5,  7, 10,  3, 13, 13,  7),
    /* 19 '{' */ PACK(13, 13, 13, 13, 11, 13, 13, 12),
    /* 20 '}' */ PACK(13, 13, 13, 13, 12, 13, 13, 11),
    /* 21 '~' */ PACK( 0,  0,  1,  0,  0,  1,  1,  2),
};

/* Rank 13 in every lane: the scan's starting state, before any literal
 * character has constrained the choice of profile. */
#define RANK_ABSENT_ALL PACK(13, 13, 13, 13, 13, 13, 13, 13)

#define LANE_HI ((uint64_t)0x8080808080808080u)
#define LANE_ONES ((uint64_t)0x0101010101010101u)

/* Bit 7 of lane p set iff x's lane p is >= y's. Setting each lane's high
 * bit before subtracting keeps the lane difference in 1..255 whenever both
 * operands are below 128, so no lane can borrow from the next and the high
 * bit is left holding the comparison. */
static uint64_t lane_ge(uint64_t x, uint64_t y) {
    return ((x | LANE_HI) - y) & LANE_HI;
}

/* Lane-wise minimum of two packings of values below 128. */
static uint64_t lane_min(uint64_t x, uint64_t y) {
    /* 0xFF in each lane where x is the larger, 0 elsewhere: the shifted
     * comparison bit sits at the lane's low bit, and multiplying by 0xFF
     * fills that lane and only that lane. */
    uint64_t m = (lane_ge(x, y) >> 7) * 0xFFu;
    return (x & ~m) | (y & m);
}

/* Lane index of the lowest set lane bit of a lane_ge result: the smallest
 * viable profile. A table keeps the function free of compiler builtins. */
static const uint8_t LANE_INDEX[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };

static unsigned lowest_lane(uint64_t lanes) {
    unsigned p = 0;
    while ((lanes & (uint64_t)0x80u) == 0) {
        lanes >>= 8;
        p++;
    }
    return LANE_INDEX[p];
}

/* The identity over ASCII, the base every per-segment translation table is
 * patched into. Every byte a DP segment can carry is ASCII. */
static const uint8_t IDENTITY_ASCII[128] = {
      0,   1,   2,   3,   4,   5,   6,   7,   8,   9,  10,  11,  12,  13,  14,  15,
     16,  17,  18,  19,  20,  21,  22,  23,  24,  25,  26,  27,  28,  29,  30,  31,
     32,  33,  34,  35,  36,  37,  38,  39,  40,  41,  42,  43,  44,  45,  46,  47,
     48,  49,  50,  51,  52,  53,  54,  55,  56,  57,  58,  59,  60,  61,  62,  63,
     64,  65,  66,  67,  68,  69,  70,  71,  72,  73,  74,  75,  76,  77,  78,  79,
     80,  81,  82,  83,  84,  85,  86,  87,  88,  89,  90,  91,  92,  93,  94,  95,
     96,  97,  98,  99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111,
    112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127,
};

/* The R-Set characters, by index j -- the other half of spec 4.3's
 * derivation, and what a decoder writes a donor back as. */
static const uint8_t RSET_ASCII[RSET_COUNT] = {
    32, 34, 39, 44, 59, 92, 124, 60, 62, 38, 9, 10, 13
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

/* Same, for the range of values that does not fit in 32 bits: every signal
 * is 2^32 + payload or above (spec section 9). One signal covers a whole
 * segment, so this path is cold and stays the straightforward loop. */
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
/* Section 4.3: deriving a segment's substitution                      */
/* ------------------------------------------------------------------ */

/* Fills `xlat` with the identity over ASCII, then patches in the donors a
 * segment with this profile and mask spends: the set bits of the mask
 * consume the profile's first k characters, the lowest bit taking rank 0.
 * `encode_direction` selects which way the substitution is written. */
static void build_substitution(unsigned profile, uint16_t mask, uint8_t *xlat,
                               int encode_direction) {
    memcpy(xlat, IDENTITY_ASCII, sizeof IDENTITY_ASCII);
    unsigned rank = 0;
    for (unsigned j = 0; j < RSET_COUNT; j++) {
        if (mask & (uint16_t)(1u << j)) {
            uint8_t donor = (uint8_t)PROFILES[profile][rank];
            if (encode_direction) {
                xlat[RSET_ASCII[j]] = donor;
            } else {
                xlat[donor] = RSET_ASCII[j];
            }
            rank++;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Encoding                                                             */
/* ------------------------------------------------------------------ */

/* Section 6.3: ProcessWithBlockMode. Encodes `n` bytes of `data`
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

/* Step 1 (spec 6.1): the length of the run of identical bytes starting at
 * buf[0], capped at MAX_FILL_BYTES. */
static size_t fill_run(const uint8_t *buf, size_t buf_len) {
    size_t limit = buf_len < MAX_FILL_BYTES ? buf_len : MAX_FILL_BYTES;
    uint8_t b = buf[0];
    size_t i = 1;
    while (i < limit && buf[i] == b) i++;
    return i;
}

/* The next position at or after `from` where the main loop could take a
 * branch other than block mode, given that it is inside a block-mode run and
 * therefore only ever *visits* positions `from`, `from + 4`, `from + 8`, ...
 *
 * Only those positions have to be tested, and at each of them the two tests
 * are exact rather than heuristic: a Fill segment starts there iff
 * MIN_FILL_BYTES equal bytes do, and a DP segment can only start there if
 * MIN_PASSTHROUGH_BYTES representable bytes do. Both bail out on their first
 * counterexample, which on high-entropy input is the second byte they read
 * -- so the whole test costs a handful of loads per 4 bytes consumed, where
 * running the two real scans costs an order of magnitude more.
 *
 * The caller may jump straight to the returned position: every position it
 * passes over would have taken step 4 and consumed exactly 4 bytes, and block
 * mode over a whole number of groups is the concatenation of the per-group
 * results, so the output is unchanged. */
static size_t next_decision_point(const uint8_t *data, size_t n, size_t from) {
    for (size_t q = from; q < n; q += 4) {
        if (q + 1 < n && data[q + 1] == data[q]) {
            size_t e = q + 1;
            while (e < n && e - q < MIN_FILL_BYTES && data[e] == data[q]) e++;
            if (e - q >= MIN_FILL_BYTES) return q;
        }
        if (REPRESENTABLE[data[q]]) {
            size_t e = q;
            while (e < n && e - q < MIN_PASSTHROUGH_BYTES && REPRESENTABLE[data[e]]) e++;
            if (e - q >= MIN_PASSTHROUGH_BYTES) return q;
        }
    }
    return n;
}

/* The Dynamic Passthrough prefix scan (spec 6.2): the longest prefix of
 * `buf` that one profile can carry, with the mask and profile in effect
 * for it. The state written out is the one in effect *before* the byte
 * that ended the scan, and the length is capped at MAX_DP_ANALYSIS_BYTES.
 *
 * The scan tracks, per profile, the lowest rank any literal Alphabet-N
 * character has held in it -- eight numbers, one per byte lane of a
 * uint64_t. A profile stays viable exactly while that number is at least
 * k, the count of R-Set characters the mask names, so the per-byte update
 * is a couple of loads and a handful of arithmetic whatever the state of
 * the eight. Most bytes do not even reach it: only 22 characters appear in
 * any profile, which DONOR_INDEX settles in one load.
 *
 * It also stops where a run of MIN_FILL_IN_SEGMENT_BYTES identical bytes
 * begins, so that Fill can reach runs inside passthrough text (spec 6.5,
 * rule 1). The rolled-back state is what that costs: a run's first byte
 * may have widened the mask or narrowed the profile choice, and the bytes
 * after it cannot have changed anything, being equal to a byte already
 * accounted for. */
static void scan_dp(const uint8_t *buf, size_t buf_len, size_t *out_len,
                    uint16_t *out_mask, unsigned *out_profile) {
    size_t limit = buf_len < MAX_DP_ANALYSIS_BYTES ? buf_len : MAX_DP_ANALYSIS_BYTES;

    uint16_t mask = 0;
    uint64_t k = 0;
    uint64_t min_donor = RANK_ABSENT_ALL;
    unsigned profile = 0;

    /* The state as it stood before the most recent change, and where that
     * change happened. At most 26 changes can occur in a segment, so this
     * costs nothing per byte. */
    uint16_t prev_mask = 0;
    unsigned prev_profile = 0;
    size_t prev_pos = (size_t)-1;

    /* Length of the run of identical bytes ending just before i. */
    size_t run = 0;
    size_t i = 0;

    while (i < limit) {
        uint8_t b = buf[i];

        if (i > 0 && b == buf[i - 1]) {
            run++;
            if (run + 1 >= MIN_FILL_IN_SEGMENT_BYTES) {
                size_t start = i - run;
                *out_len = start;
                if (prev_pos == start) {
                    *out_mask = prev_mask;
                    *out_profile = prev_profile;
                } else {
                    *out_mask = mask;
                    *out_profile = profile;
                }
                return;
            }
        } else {
            run = 0;
        }

        int8_t j = RSET_INDEX[b];
        if (j >= 0) {
            uint16_t bit = (uint16_t)(1u << j);
            if (mask & bit) {
                i++; /* already named by the mask; nothing changes */
                continue;
            }
            /* One more donor to spend: every profile whose lowest literal
             * rank has been reached now drops out. */
            uint64_t viable = lane_ge(min_donor, (k + 1) * LANE_ONES);
            if (viable == 0) break;
            prev_mask = mask;
            prev_profile = profile;
            prev_pos = i;
            profile = lowest_lane(viable);
            mask |= bit;
            k++;
        } else {
            if (ALPHABET_VALUE[b] < 0) break; /* not representable at all */
            int8_t slot = DONOR_INDEX[b];
            if (slot < 0) {
                i++; /* no profile spends it, so it constrains nothing */
                continue;
            }
            uint64_t new_min = lane_min(min_donor, RANK_PACKED[slot]);
            if (new_min == min_donor) {
                i++; /* ranks below nothing already seen */
                continue;
            }
            uint64_t viable = lane_ge(new_min, k * LANE_ONES);
            if (viable == 0) break;
            prev_mask = mask;
            prev_profile = profile;
            prev_pos = i;
            profile = lowest_lane(viable);
            min_donor = new_min;
        }
        i++;
    }

    *out_len = i;
    *out_mask = mask;
    *out_profile = profile;
}

/* Emit one DP segment: its 5-character signal (spec section 9, with the
 * length field biased by one) followed by the transformed bytes. */
static uint8_t *emit_dp_segment(uint8_t *w, const uint8_t *buf, size_t len,
                                uint16_t mask, unsigned profile) {
    uint64_t payload = ((uint64_t)profile << 24) | ((uint64_t)mask << 11) |
                       (uint64_t)(len - 1);
    value_to_5chars_64(POW2_32 + payload, (char *)w);
    w += 5;

    uint8_t xlat[128];
    build_substitution(profile, mask, xlat, 1);
    for (size_t i = 0; i < len; i++) {
        *w++ = xlat[buf[i] & 0x7fu];
    }
    return w;
}

/* Emit one Solid Fill signal (spec section 9). */
static uint8_t *emit_fill_signal(uint8_t *w, uint8_t byte, size_t len) {
    uint64_t payload = ((uint64_t)byte << 11) | (uint64_t)(len - 1);
    value_to_5chars_64(FILL_SIGNAL_BASE + payload, (char *)w);
    return w + 5;
}

/* What the output buffer is sized at up front, excluding growth.
 *
 * Block mode emits exactly 1.25 characters per byte (plus at most 2 for a
 * partial final group), so this is the exact size an input with neither
 * other mode in it needs -- which is every high-entropy input, the case
 * where the buffer is large enough for its size to matter. Neither other
 * mode exceeds it: DP spends one character per byte plus a signal per 2048,
 * and Fill five characters per five bytes or more. The main loop still
 * checks the room it needs before each emit, which is what makes that
 * argument a guard rather than a comment. */
static size_t encode_capacity(size_t n) {
    return n + n / 4 + 16;
}

/* Grow the output buffer so that `need` more characters, plus the NUL,
 * fit at *w. Returns 0 on allocation failure. */
static int ensure_capacity(uint8_t **out, size_t *cap, uint8_t **w, size_t need) {
    size_t used = (size_t)(*w - *out);
    if (need + 1 <= *cap - used) return 1;
    size_t want = used + need + 1;
    if (want < used) return 0;
    /* A quarter of headroom, so repeated growth stays amortised without
     * overshooting far past what the input needs. */
    size_t newcap = want <= SIZE_MAX - want / 4 ? want + want / 4 : SIZE_MAX;
    uint8_t *grown = (uint8_t *)realloc(*out, newcap);
    if (!grown) return 0;
    *out = grown;
    *cap = newcap;
    *w = grown + used;
    return 1;
}

/* Characters block mode spends on `n` pending bytes. */
static size_t block_mode_chars(size_t n) {
    return (n / 4) * 5 + (n % 4 ? n % 4 + 1 : 0);
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

    size_t off = 0;
    base85n_status status = BASE85N_OK;

    /* Start of the pending run of block-mode bytes, or SIZE_MAX for none.
     * Consecutive block-mode iterations are converted in one call instead of
     * four bytes at a time. This does not change which positions the loop
     * visits: every block-mode consumption is a whole number of 4-byte
     * groups, so the concatenation of the per-iteration results is exactly
     * the block-mode encoding of the accumulated range. */
    size_t block_start = SIZE_MAX;

    while (off < data_len) {
        const uint8_t *buf = data + off;
        size_t buf_len = data_len - off;
        size_t pending = block_start == SIZE_MAX ? 0 : off - block_start;

        /* Step 1: a run of identical bytes long enough to be worth a signal
         * of its own. Five characters for up to 2048 bytes. */
        size_t run = fill_run(buf, buf_len);
        if (run >= MIN_FILL_BYTES) {
            if (!ensure_capacity(&out, &cap, &w, block_mode_chars(pending) + 5)) {
                status = BASE85N_ERR_ALLOC;
                break;
            }
            if (block_start != SIZE_MAX) {
                w = process_block_mode(data + block_start, pending, w);
                block_start = SIZE_MAX;
            }
            w = emit_fill_signal(w, buf[0], run);
            off += run;
            continue;
        }

        /* Steps 2 and 3. At MIN_PASSTHROUGH_BYTES the two modes cost the
         * same 25 characters and Dynamic Passthrough only gains from there,
         * so the length test settles the size comparison too. */
        size_t best_len;
        uint16_t mask;
        unsigned profile;
        scan_dp(buf, buf_len, &best_len, &mask, &profile);

        if (best_len >= MIN_PASSTHROUGH_BYTES) {
            if (!ensure_capacity(&out, &cap, &w,
                                 block_mode_chars(pending) + 5 + best_len)) {
                status = BASE85N_ERR_ALLOC;
                break;
            }
            if (block_start != SIZE_MAX) {
                w = process_block_mode(data + block_start, pending, w);
                block_start = SIZE_MAX;
            }
            w = emit_dp_segment(w, buf, best_len, mask, profile);
            off += best_len;
            continue;
        }

        /* Step 4: exactly one 4-byte group, however long the failed
         * candidate was. Nothing but the end of the input can hand
         * process_block_mode a partial group this way. */
        if (block_start == SIZE_MAX) block_start = off;
        off += buf_len < 4 ? buf_len : 4;

        /* Every position up to the next decision point takes this same
         * branch, so jump to it rather than re-deciding every four bytes.
         *
         * The gate is what keeps the lookahead off the path it cannot help:
         * where the next byte is representable, a DP candidate starts right
         * here and the scan the loop is about to run is the cheaper way to
         * find out how far it reaches. Where it is not, the lookahead runs
         * over binary, which is exactly where it earns its keep. */
        if (off < data_len && !REPRESENTABLE[data[off]]) {
            size_t next = next_decision_point(data, data_len, off);
            off += ((next - off) / 4) * 4;
        }
    }

    if (status == BASE85N_OK && block_start != SIZE_MAX) {
        size_t pending = off - block_start;
        if (!ensure_capacity(&out, &cap, &w, block_mode_chars(pending))) {
            status = BASE85N_ERR_ALLOC;
        } else {
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

/* Grow the decoder's output buffer so that `need` more bytes fit at offset
 * `w`. Returns 0 on allocation failure.
 *
 * Only Solid Fill can make this necessary -- every other construct produces
 * at most one byte per input character -- so the callers below test the
 * bound inline and reach this only on the rare path where it fails. */
static int grow_output(uint8_t **out, size_t *cap, size_t w, size_t need) {
    size_t want = w + need;
    if (want < w) return 0;
    size_t newcap = want <= SIZE_MAX - want / 4 ? want + want / 4 : SIZE_MAX;
    uint8_t *grown = (uint8_t *)realloc(*out, newcap ? newcap : 1);
    if (!grown) return 0;
    *out = grown;
    *cap = newcap;
    return 1;
}

/* Room for `need` more bytes at `w`, growing only if the buffer is short.
 * `buf` is the caller's local copy of the pointer, refreshed on growth so
 * the hot loops can keep writing through a register. */
#define ENSURE_OUTPUT(need)                                                        do {                                                                               if ((need) > cap - w) {                                                            if (!grow_output(out, &cap, w, (need))) return BASE85N_ERR_ALLOC;               buf = *out;                                                                }                                                                          } while (0)

/* Decodes `n` characters of `in`, which must already be free of the
 * inter-token whitespace section 7.1 allows, into *out, growing it where a
 * Solid Fill signal needs more room than the input bounds. `*cap` is the
 * buffer's current size and `*produced` receives the number of bytes
 * written. `in` and `*out` must not overlap: a Fill signal produces bytes
 * without consuming characters, so the writer can outrun the reader. */
static base85n_status decode_scan(const uint8_t *in, size_t n, uint8_t **out,
                                  size_t *cap_io, size_t *produced) {
    size_t w = 0;
    size_t pos = 0;
    size_t cap = *cap_io;
    uint8_t *buf = *out;

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
                ENSURE_OUTPUT(4);
                uint32_t v32 = (uint32_t)decoded_value;
                buf[w + 0] = (uint8_t)(v32 >> 24);
                buf[w + 1] = (uint8_t)(v32 >> 16);
                buf[w + 2] = (uint8_t)(v32 >> 8);
                buf[w + 3] = (uint8_t)v32;
                w += 4;
                continue;
            }

            if (decoded_value >= FUTURE_SIGNAL_BASE) {
                return BASE85N_ERR_UNDEFINED_SIGNAL;
            }

            if (decoded_value >= FILL_SIGNAL_BASE) {
                /* Section 7.4: no characters are read to construct the data. */
                uint64_t payload = decoded_value - FILL_SIGNAL_BASE;
                size_t fill_len = (size_t)(payload & 0x7FFu) + 1;
                uint8_t byte = (uint8_t)((payload >> 11) & 0xFFu);
                ENSURE_OUTPUT(fill_len);
                memset(buf + w, byte, fill_len);
                w += fill_len;
                continue;
            }

            uint64_t payload = decoded_value - POW2_32;
            unsigned profile = (unsigned)((payload >> 24) & 0x7u);
            uint16_t mask = (uint16_t)((payload >> 11) & 0x1FFFu);
            /* Spec section 9: the length field is stored biased by one, so
             * the shortest segment a signal can name is 1 character and the
             * longest MAX_DP_SEGMENT_CHARS. */
            size_t seg_len = (size_t)(payload & 0x7FFu) + 1;

            if (n - pos < seg_len) return BASE85N_ERR_UNEXPECTED_EOF;
            ENSURE_OUTPUT(seg_len);

            /* Section 4.3: one character in, one byte out, with no state
             * carried between characters. */
            uint8_t xlat[128];
            build_substitution(profile, mask, xlat, 0);
            const uint8_t *q = in + pos;
            const uint8_t *qend = q + seg_len;
            while (q < qend) {
                uint8_t c = *q++;
                if (ALPHABET_VALUE[c] < 0) return BASE85N_ERR_INVALID_CHAR;
                buf[w++] = xlat[c];
            }
            pos += seg_len;
        } else if (remaining == 1) {
            /* A lone trailing Alphabet-N character cannot be a valid final
             * block (2 characters are the minimum for 1 byte). */
            return BASE85N_ERR_INVALID_FINAL_BLOCK;
        } else {
            /* remaining is 2, 3, or 4: the final block (section 7.5). */
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
            /* Section 7.5: the padded group's value must be below 2^32. */
            if (decoded_value >= POW2_32) {
                return BASE85N_ERR_INVALID_FINAL_BLOCK;
            }
            uint32_t v32 = (uint32_t)decoded_value;
            uint8_t bytes[4] = {
                (uint8_t)(v32 >> 24), (uint8_t)(v32 >> 16),
                (uint8_t)(v32 >> 8), (uint8_t)v32
            };
            size_t nbytes = remaining - 1; /* 1, 2, or 3 */

            /* Section 7.5, canonical enforcement: the characters must be
             * exactly what encoding those bytes zero-padded to four would
             * have produced. Without this, several character sequences
             * decode to the same bytes. */
            uint8_t padded[4] = {0, 0, 0, 0};
            memcpy(padded, bytes, nbytes);
            uint32_t canon_value = ((uint32_t)padded[0] << 24) |
                                   ((uint32_t)padded[1] << 16) |
                                   ((uint32_t)padded[2] << 8) | (uint32_t)padded[3];
            char canonical[5];
            value_to_5chars_32(canon_value, canonical);
            if (memcmp(canonical, in + pos, remaining) != 0) {
                return BASE85N_ERR_INVALID_FINAL_BLOCK;
            }

            ENSURE_OUTPUT(nbytes);
            memcpy(buf + w, bytes, nbytes);
            w += nbytes;
            pos += remaining;
        }
    }

    *produced = w;
    return BASE85N_OK;
}

base85n_status base85n_decode(const char *s, size_t s_len,
                               uint8_t **out_data, size_t *out_len) {
    if (!out_data || !out_len) return BASE85N_ERR_INVALID_ARGUMENT;
    if (!s && s_len != 0) return BASE85N_ERR_INVALID_ARGUMENT;

    const uint8_t *in = (const uint8_t *)s;

    /* One allocation, sized by the bound every construct but Solid Fill
     * obeys; the Fill branch grows it. Zero-length input still gets a
     * valid, free()-able pointer. */
    size_t cap = s_len ? s_len : 1;
    uint8_t *out = (uint8_t *)malloc(cap);
    if (!out) return BASE85N_ERR_ALLOC;

    size_t produced = 0;
    base85n_status status = decode_scan(in, s_len, &out, &cap, &produced);

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
     * The filtered copy is its own allocation. Version 0.3.0 could strip
     * into the output buffer and decode in place, because output could
     * never overtake input; a Fill signal produces up to 2048 bytes without
     * consuming a character, so that no longer holds. */
    if (status != BASE85N_OK) {
        size_t i;
        for (i = 0; i < s_len; i++) {
            if (is_ignorable_ws(in[i])) break;
        }
        if (i < s_len) {
            uint8_t *filtered = (uint8_t *)malloc(s_len);
            if (!filtered) {
                free(out);
                return BASE85N_ERR_ALLOC;
            }
            memcpy(filtered, in, i); /* everything before the first space */
            size_t n = i;
            for (size_t k = i; k < s_len; k++) {
                if (!is_ignorable_ws(in[k])) filtered[n++] = in[k];
            }
            status = decode_scan(filtered, n, &out, &cap, &produced);
            free(filtered);
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
        case BASE85N_ERR_UNDEFINED_SIGNAL: return "undefined signal value (FUTURE_SIGNAL_SPACE)";
        case BASE85N_ERR_INVALID_FINAL_BLOCK: return "invalid final block";
        case BASE85N_ERR_ALLOC: return "memory allocation failure";
        case BASE85N_ERR_INVALID_ARGUMENT: return "invalid argument";
        default: return "unknown error";
    }
}
