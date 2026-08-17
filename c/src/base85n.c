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
#define MIN_TAIL_ZEROS BASE85N_MIN_TAIL_ZEROS
#define MAX_TAIL_ZEROS BASE85N_MAX_TAIL_ZEROS

/* Section 9's signal ranges. Block mode occupies 0 .. 2^32; DP takes the
 * next 2^27 values (3 profile + 13 mask + 11 length bits); Fill the next
 * 2^19 + 2^22, split into its two variants; everything above that is
 * FUTURE_SIGNAL_SPACE and must be rejected.
 *
 * Fill's two variants are two adjacent sub-ranges rather than a payload bit,
 * because they are not the same width: a solid run needs 8 bits of byte
 * value and 11 of length, a run with a tail needs 16 bits of literal, 5 of
 * length and one of order. One comparison separates them. */
#define POW2_32 ((uint64_t)1u << 32)
#define FILL_SIGNAL_BASE (POW2_32 + ((uint64_t)1u << 27))
#define TAIL_SIGNAL_BASE (FILL_SIGNAL_BASE + ((uint64_t)1u << 19))
#define FUTURE_SIGNAL_BASE (TAIL_SIGNAL_BASE + ((uint64_t)1u << 22))

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

/* What the prefix scan needs to know about one byte, in a single load.
 *
 * The scan used to ask three tables in sequence -- is this an R-Set
 * character, is it in Alphabet-N at all, is it a donor some profile spends --
 * which is three loads and three branches for the byte that answers "none of
 * the above", and that byte is most of every input. One table answers all
 * three questions at once, and numbers its answers into a single 0..63 space
 * so that the whole question the hot path asks is one bit test:
 *
 *   DP_PLAIN (0)          in Alphabet-N, no profile spends it: carries
 *                         nothing, constrains nothing.
 *   1 .. 13               R-Set character, index j = code - DP_RSET_BASE.
 *   14 .. 35              donor character, slot = code - DP_DONOR_BASE.
 *   DP_STOP (63)          not representable: ends the segment.
 *
 * One numbering, because the scan tracks all of them in one 64-bit "already
 * accounted for" set, and a code's bit in that set is exactly "this byte
 * changes nothing" -- true for a repeated R-Set character or donor, and true
 * from the start for DP_PLAIN. The two ends of the range are chosen to fall
 * out of the same test: DP_PLAIN is 0 so its bit can be set before the scan
 * begins, and DP_STOP is 63 so that it too is a well-defined shift, landing
 * on the one bit of the set that is never set. */
#define DP_PLAIN 0u
#define DP_RSET_BASE 1u  /* codes 1 .. 13 */
#define DP_DONOR_BASE 14u /* codes 14 .. 35 */
#define DP_STOP 63u

static const uint8_t DP_CLASS[256] = {
     63,  63,  63,  63,  63,  63,  63,  63,  63,  11,  12,  63,  63,  13,  63,  63,
     63,  63,  63,  63,  63,  63,  63,  63,  63,  63,  63,  63,  63,  63,  63,  63,
      1,  14,   2,  15,  16,  17,  10,   3,  18,  19,  20,  21,   4,  22,  23,   0,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,  24,   5,   8,  25,   9,  26,
     27,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,  28,   6,  29,  30,  31,
     32,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,  33,   7,  34,  35,  63,
     63,  63,  63,  63,  63,  63,  63,  63,  63,  63,  63,  63,  63,  63,  63,  63,
     63,  63,  63,  63,  63,  63,  63,  63,  63,  63,  63,  63,  63,  63,  63,  63,
     63,  63,  63,  63,  63,  63,  63,  63,  63,  63,  63,  63,  63,  63,  63,  63,
     63,  63,  63,  63,  63,  63,  63,  63,  63,  63,  63,  63,  63,  63,  63,  63,
     63,  63,  63,  63,  63,  63,  63,  63,  63,  63,  63,  63,  63,  63,  63,  63,
     63,  63,  63,  63,  63,  63,  63,  63,  63,  63,  63,  63,  63,  63,  63,  63,
     63,  63,  63,  63,  63,  63,  63,  63,  63,  63,  63,  63,  63,  63,  63,  63,
     63,  63,  63,  63,  63,  63,  63,  63,  63,  63,  63,  63,  63,  63,  63,  63,
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

/* The identity over every byte, the base each per-segment translation table
 * is patched into. Every byte a DP segment can carry is ASCII, but the table
 * covers all 256 so that the loops indexing it need no masking step: one
 * instruction per character, on the loop that writes every DP byte. */
#define IDENTITY_ROW(h) \
    h+0, h+1, h+2, h+3, h+4, h+5, h+6, h+7, \
    h+8, h+9, h+10, h+11, h+12, h+13, h+14, h+15,

static const uint8_t IDENTITY_BYTES[256] = {
    IDENTITY_ROW(0)   IDENTITY_ROW(16)  IDENTITY_ROW(32)  IDENTITY_ROW(48)
    IDENTITY_ROW(64)  IDENTITY_ROW(80)  IDENTITY_ROW(96)  IDENTITY_ROW(112)
    IDENTITY_ROW(128) IDENTITY_ROW(144) IDENTITY_ROW(160) IDENTITY_ROW(176)
    IDENTITY_ROW(192) IDENTITY_ROW(208) IDENTITY_ROW(224) IDENTITY_ROW(240)
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

/* One Big-Endian 32-bit group. Written as shifts rather than a load and a
 * byte swap so that it is correct on any endianness and needs no intrinsic;
 * every compiler this library targets recognises the pattern and emits the
 * single byte-swapping load anyway. */
static inline uint32_t load_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

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
static inline void value_to_5chars_32(uint32_t value, char *out) {
    uint32_t q    = value / POW85_2;     /* = head * 85 + mid       */
    uint32_t head = value / POW85_3;     /* digits 0,1: 0 .. 85^2-1 */
    uint32_t tail = value - q * POW85_2; /* digits 3,4: 0 .. 85^2-1 */
    uint32_t mid  = q - head * 85u;      /* digit 2:    0 .. 84     */

    /* The three indices are widened before they are scaled, so that the
     * scaling is part of the address the load already forms rather than an
     * instruction of its own -- three of them, on the path that carries
     * every block-mode byte. */
    memcpy(out, PAIR_CHARS + 2 * (size_t)head, 2);
    out[2] = ALPHABET_N_CHARS_STR[(size_t)mid];
    memcpy(out + 3, PAIR_CHARS + 2 * (size_t)tail, 2);
}

/* Same, for the range of values that does not fit in 32 bits: every signal
 * is 2^32 + payload or above (spec section 9), and every one is below 85^5.
 *
 * A signal covers a whole segment, so this used to be the straightforward
 * five-division loop. That is the wrong shape for the inputs that emit a
 * signal every few bytes -- a zero-padded ELF spends one Fill-with-tail per
 * five bytes, and there the loop's five dependent divisions were as much
 * arithmetic as the block mode they replace. Splitting at 85^3 costs two
 * divisions and reads the outer digit pairs from the same table block mode
 * uses. */
static void value_to_5chars_64(uint64_t value, char *out) {
    uint32_t head = (uint32_t)(value / POW85_3);           /* digits 0,1 */
    uint32_t low  = (uint32_t)(value - (uint64_t)head * POW85_3);
    uint32_t mid  = low / POW85_2;                         /* digit 2    */
    uint32_t tail = low - mid * POW85_2;                   /* digits 3,4 */

    memcpy(out, PAIR_CHARS + 2 * (size_t)head, 2);
    out[2] = ALPHABET_N_CHARS_STR[(size_t)mid];
    memcpy(out + 3, PAIR_CHARS + 2 * (size_t)tail, 2);
}

/* ------------------------------------------------------------------ */
/* Section 4.3: deriving a segment's substitution                      */
/* ------------------------------------------------------------------ */

/* Fills `xlat` with the identity, then patches in the donors a segment with
 * this profile and mask spends: the set bits of the mask consume the
 * profile's first k characters, the lowest bit taking rank 0.
 * `encode_direction` selects which way the substitution is written. */
static void build_substitution(unsigned profile, uint16_t mask, uint8_t *xlat,
                               int encode_direction) {
    memcpy(xlat, IDENTITY_BYTES, sizeof IDENTITY_BYTES);
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

/* One built substitution together with the profile and mask it was built
 * for. Segments do not choose their profile and mask independently of one
 * another: a JSON document is a long sequence of segments that all carry
 * the same three R-Set characters, and hands the same pair to segment after
 * segment. Keeping the last one lets those segments skip the rebuild
 * entirely, which on pretty-printed JSON is thirteen thousand of them.
 *
 * A cache is only ever used in one direction -- encoders hold an encoding
 * one, decoders a decoding one -- so the direction is not part of the key.
 * It lives in its owner's stack frame, so the library keeps its promise of
 * no shared mutable state. */
typedef struct {
    uint8_t table[256];
    uint32_t key; /* profile << 16 | mask, or XLAT_NO_KEY when unbuilt */
} xlat_cache;

#define XLAT_NO_KEY 0xFFFFFFFFu

static const uint8_t *xlat_for(xlat_cache *cache, unsigned profile,
                               uint16_t mask, int encode_direction) {
    uint32_t key = ((uint32_t)profile << 16) | (uint32_t)mask;
    if (cache->key != key) {
        build_substitution(profile, mask, cache->table, encode_direction);
        cache->key = key;
    }
    return cache->table;
}

/* ------------------------------------------------------------------ */
/* Encoding                                                             */
/* ------------------------------------------------------------------ */

/* Which of the encoder's steps are in play.
 *
 * The shipped encoder is ENC_OPT_ALL and nothing else: spec 6.5 makes every
 * step mandatory at every decision point the loop reaches, so an encoder that
 * drops one is not a Base85N encoder producing smaller-or-larger output -- it
 * is a second, non-conforming dialect that happens to decode. These bits exist
 * so that bench/speed/bench_binary_flag.c can build those dialects and measure
 * what each step actually costs, which is the only way to attribute a speed
 * difference to a step rather than to a mode as a whole. They are deliberately
 * not in base85n.h; nothing outside the benchmark can reach them.
 *
 * `opts` is a compile-time constant at every call site, so each instantiation
 * of encode_with() folds its tests away and none of this reaches the shipped
 * object code as a branch. */
#define ENC_OPT_DP    1u  /* Dynamic Passthrough, spec steps 2 and 3 */
#define ENC_OPT_FILL  2u  /* Fill and its zero-run tail variant, spec step 1 */

/* How wide a lookahead the block-mode skip of spec 11.1 gates its Dynamic
 * Passthrough test on. These are not dialects: all three widths find the
 * same decision points, so all three produce the same output, character for
 * character. They differ only in how much work and how many mispredicted
 * branches finding them costs, which is why they are measurable separately
 * -- see bench/speed/bench_binary_flag.c. Both are on in the shipped
 * encoder; dropping them is what the benchmark does. */
#define ENC_OPT_WIDEGATE 4u  /* four bytes, from the table */
#define ENC_OPT_WORDGATE 8u  /* eight bytes, in one word, without the table */

/* Also not a dialect: the skip settles two 4-byte groups at a time behind one
 * word-level test, instead of testing each group in turn. Same decision
 * points, same output; see window_may_hold(). */
#define ENC_OPT_WINDOW 16u

/* And the same idea in the main loop: the passthrough scan is retired by a
 * cheap necessary condition before it is entered, rather than entered and
 * abandoned. See encode_with(). */
#define ENC_OPT_SCANGATE 32u

#define ENC_OPT_ALL (ENC_OPT_DP | ENC_OPT_FILL | ENC_OPT_WIDEGATE | \
                     ENC_OPT_WORDGATE | ENC_OPT_WINDOW | ENC_OPT_SCANGATE)

/* Whether a Dynamic Passthrough segment can begin at `p`, tested as cheaply
 * as it can be tested without being wrong.
 *
 * A DP segment needs MIN_PASSTHROUGH_BYTES representable bytes in a row, so
 * the first four being representable is a necessary condition: the wide gate
 * never turns away a position the narrow one would have accepted, and both
 * are exact. What differs is how often the branch below them is taken, and
 * that is the whole cost. Roughly a third of byte values are representable,
 * so on high-entropy binary the narrow gate is a coin flip resolved once per
 * four bytes of input -- a branch nothing can predict, on the loop that
 * carries every binary encode. Four in a row is taken about one time in
 * fifty on the same input, which predicts, while text and the scan-heavy
 * adversarial case clear it as immediately as they clear one byte.
 *
 * The four loads are unconditional and folded with `&` rather than `&&` so
 * that the test itself contributes no branch of its own.
 *
 * The word gate takes that one step further and asks for eight, which costs
 * less than four rather than more: eight bytes are one load, and the range
 * they have to fall in can be tested in the register instead of through the
 * table. It tests the wider range [0x09, 0x7E] -- every representable byte
 * lies in it, and 0x0B, 0x0C and 0x0E to 0x1F additionally do -- because
 * that range is what arithmetic on a whole word can settle. A superset is
 * all this position needs: it may only fail to rule a position out, never
 * rule one out that a DP segment could have started at, and the walk below
 * decides for real either way. On high-entropy binary eight bytes land in
 * that range about one time in five hundred.
 *
 * The caller guarantees MIN_PASSTHROUGH_BYTES bytes are inside the input,
 * which is more than any of the three reads. */
/* Nonzero iff every byte of the word at `p` lies in [lo, hi]. Both bounds
 * must be at most 127, which every bound used here is.
 *
 * A lane below `lo` borrows into its own high bit while its own top bit is
 * clear; a lane above `hi` either has its top bit set already or carries
 * into it when 127 - hi is added. Neither test needs to know which lane is
 * which, so neither assumes an endianness. */
static inline int lanes_within(const uint8_t *p, uint64_t lo, uint64_t hi) {
    uint64_t x;
    memcpy(&x, p, sizeof x);
    uint64_t below = (x - LANE_ONES * lo) & ~x;
    uint64_t above = (x + LANE_ONES * (127 - hi)) | x;
    return ((below | above) & (LANE_ONES * 0x80)) == 0;
}

/* How many of the bytes at `p` the gate has proved representable -- 0 when
 * it has proved none, and the gate's width when it has proved them all. The
 * walk in the caller starts there, and returns 0 when no DP segment can
 * begin at `p` at all. */
static inline size_t dp_possible(const uint8_t *p, const unsigned opts,
                                 int *out_possible) {
    *out_possible = 1;
    if (opts & ENC_OPT_WORDGATE) {
        /* Eight bytes in one load and half a dozen register operations,
         * against four table loads for half the lookahead -- so this goes
         * first, being the test that rejects. On high-entropy binary it
         * rejects about four hundred and ninety-nine times in five hundred,
         * which is what makes the branch under it predictable where the
         * shipped encoder's is a coin flip. */
        if (!lanes_within(p, 0x09, 0x7E)) {
            *out_possible = 0;
            return 0;
        }
        /* Past the word gate the four-byte gate decides, exactly as it does
         * on its own. The word gate cannot settle its own eight bytes: it
         * clears the wider range [0x09, 0x7E], which also admits 0x0B, 0x0C
         * and 0x0E to 0x1F, and a byte in those is not representable. So
         * what it contributes is the rejection, and the table settles the
         * four bytes the walk then starts past. */
    }
    if (opts & (ENC_OPT_WIDEGATE | ENC_OPT_WORDGATE)) {
        if (!(REPRESENTABLE[p[0]] & REPRESENTABLE[p[1]] &
              REPRESENTABLE[p[2]] & REPRESENTABLE[p[3]])) {
            *out_possible = 0;
            return 0;
        }
        return 4;
    }
    if (!REPRESENTABLE[p[0]]) {
        *out_possible = 0;
        return 0;
    }
    return 1;
}

/* Section 6.3: ProcessWithBlockMode. Encodes `n` bytes of `data`
 * starting at full 4-byte blocks; if n is not a multiple of 4, the
 * trailing 1-3 bytes are encoded as a padded partial group per the
 * spec. Writes at most (n/4)*5 + 4 characters at `w`, and returns the
 * cursor past the last one. */
static uint8_t *process_block_mode(const uint8_t *data, size_t n, uint8_t *w) {
    const uint8_t *p = data;
    const uint8_t *end = data + (n & ~(size_t)3);

    /* Four groups per iteration, walked by pointer. Each group's digit
     * extraction is a short dependency chain with very little to fill it,
     * and neighbouring groups are entirely independent, so issuing four of
     * them together keeps the multipliers busy and pays the loop's own
     * bookkeeping once per sixteen bytes rather than once per four. gcc
     * unrolls none of this on its own at -O2, which is the optimisation
     * level the Makefile ships. */
    while ((size_t)(end - p) >= 16) {
        uint32_t v0 = load_be32(p);
        uint32_t v1 = load_be32(p + 4);
        uint32_t v2 = load_be32(p + 8);
        uint32_t v3 = load_be32(p + 12);
        value_to_5chars_32(v0, (char *)w);
        value_to_5chars_32(v1, (char *)w + 5);
        value_to_5chars_32(v2, (char *)w + 10);
        value_to_5chars_32(v3, (char *)w + 15);
        p += 16;
        w += 20;
    }
    while (p < end) {
        value_to_5chars_32(load_be32(p), (char *)w);
        p += 4;
        w += 5;
    }

    size_t rem = n & 3;
    if (rem > 0) {
        uint32_t val = (uint32_t)p[0] << 24;
        if (rem > 1) val |= (uint32_t)p[1] << 16;
        if (rem > 2) val |= (uint32_t)p[2] << 8;
        char chars[5];
        value_to_5chars_32(val, chars);
        /* Take the first rem+1 characters. */
        memcpy(w, chars, rem + 1);
        w += rem + 1;
    }
    return w;
}

/* The first index at or after `i` and below `limit` at which `buf` stops
 * repeating the byte `b`.
 *
 * A Fill run reaches 2048 bytes, and counting one byte at a time is eight
 * times more work than the run deserves: a run of a single byte value is a
 * run of one 8-byte word, whatever that value is. Comparing against the byte
 * broadcast into all eight lanes needs no endianness assumption -- every lane
 * holds the same value, so which lane is which does not arise -- and the
 * word that fails the comparison is finished off by the byte loop below it,
 * which is also what runs when there is no word left to read. */
static size_t run_end(const uint8_t *buf, size_t limit, uint8_t b, size_t i) {
    uint64_t broadcast = (uint64_t)b * LANE_ONES;
    while (i + 8 <= limit) {
        uint64_t word;
        memcpy(&word, buf + i, sizeof word);
        if (word != broadcast) break;
        i += 8;
    }
    while (i < limit && buf[i] == b) i++;
    return i;
}

/* Step 1 (spec 6.1): the length of the run of identical bytes starting at
 * buf[0], capped at MAX_FILL_BYTES. */
static size_t fill_run(const uint8_t *buf, size_t buf_len) {
    size_t limit = buf_len < MAX_FILL_BYTES ? buf_len : MAX_FILL_BYTES;
    uint8_t b = buf[0];
    /* Most positions in a binary input begin no run at all, and this is the
     * test every one of them pays. Settling it before a word is loaded keeps
     * the wide scan on the inputs that have runs to find. */
    if (limit < 2 || buf[1] != b) return 1;
    return run_end(buf, limit, b, 2);
}

/* Step 1 (spec 6.1): the length of the run of zero bytes starting at
 * buf[0], capped where the tail variant's 5-bit length field saturates. */
static size_t zero_run(const uint8_t *buf, size_t buf_len) {
    size_t limit = buf_len < MAX_TAIL_ZEROS ? buf_len : MAX_TAIL_ZEROS;
    return run_end(buf, limit, 0, 0);
}

/* Whether any of steps 1 to 3 applies at `q`, which is what makes `q` a
 * decision point the skip may not pass over. Each test is exact and bails
 * out on its first counterexample, which on high-entropy input is the second
 * byte it reads.
 *
 * The caller guarantees MIN_PASSTHROUGH_BYTES bytes from `q` are inside the
 * input, which is more than any test here reads, so none of them needs a
 * bound test of its own. That is three comparisons saved per four bytes
 * skipped, on the loop that carries every high-entropy encode. */
static inline int decision_at(const uint8_t *data, size_t q,
                              const unsigned opts, int dp_maybe) {
    /* Each walk starts past the byte its gate has already settled: the gate
     * is the walk's first step, and repeating it is a step the common case
     * cannot afford, being most of what the walk does before the byte after
     * it ends the walk. */
    uint8_t b0 = data[q];
    if ((opts & ENC_OPT_FILL) && data[q + 2] == 0) {
        size_t e = q;
        while (e - q < MIN_TAIL_ZEROS && data[e] == 0) e++;
        if (e - q >= MIN_TAIL_ZEROS) return 1;
        e = q + 3; /* data[q + 2] is the zero the gate found */
        while (e - (q + 2) < MIN_TAIL_ZEROS && data[e] == 0) e++;
        if (e - (q + 2) >= MIN_TAIL_ZEROS) return 1;
    }
    if ((opts & ENC_OPT_FILL) && data[q + 1] == b0) {
        size_t e = q + 2; /* data[q] and data[q + 1] are the gate's pair */
        while (e - q < MIN_FILL_BYTES && data[e] == b0) e++;
        if (e - q >= MIN_FILL_BYTES) return 1;
    }
    /* `dp_maybe` is what the caller's window test already settled. When it is
     * clear, no passthrough segment can begin here and the whole step is
     * dropped -- which is most of what this function costs on zero-padded
     * binary, where a Fill gate wakes it up at nearly every position and the
     * passthrough test then fails at nearly every one. */
    if ((opts & ENC_OPT_DP) && dp_maybe) {
        int possible = 0;
        /* The walk starts past whatever the gate has already settled. */
        size_t e = q + dp_possible(data + q, opts, &possible);
        if (possible) {
            while (e - q < MIN_PASSTHROUGH_BYTES && REPRESENTABLE[data[e]]) e++;
            if (e - q >= MIN_PASSTHROUGH_BYTES) return 1;
        }
    }
    return 0;
}

/* Whether a decision point can begin at `p` or at `p + 4`. False means
 * neither can, and the caller may skip both groups without testing either.
 *
 * Widening the gate (see dp_possible) made the skip's branches predictable;
 * this is the same idea applied to the loop rather than to one test in it.
 * Each of the three steps has a gate that decides it, every one of those
 * gates reads inside the sixteen bytes from `p`, and each becomes a question
 * about a whole word rather than about a named byte:
 *
 *   - Fill's zero variants are gated on `data[q + 2]`, which for the two
 *     groups is `p[2]` and `p[6]`.
 *   - Solid Fill is gated on a pair of equal adjacent bytes, `(p[0], p[1])`
 *     and `(p[4], p[5])`.
 *   - Dynamic Passthrough needs MIN_PASSTHROUGH_BYTES representable bytes
 *     from its start, so a segment beginning at either group must carry
 *     `p[4]` through `p[11]` among them. One range test over that word rules
 *     out both.
 *
 * The two Fill gates are exact; the passthrough one is a *necessary*
 * condition and nothing more. None of the three may rule out a group a step
 * applies at -- they may only fail to rule one out. decision_at() then
 * decides for real.
 *
 * All three are computed unconditionally and folded with `|` so the test
 * contributes one branch rather than three, and on high-entropy input that
 * branch is not taken about 94 times in 100. The caller guarantees sixteen
 * bytes from `p` are inside the input. */
#define WIN_FILL 1  /* a Fill gate fires at one of the two groups */
#define WIN_DP   2  /* a passthrough segment may begin at one of them */

static inline int window_may_hold(const uint8_t *p, const unsigned opts) {
    int maybe = 0;
    if (opts & ENC_OPT_FILL) {
        /* Both Fill gates name a byte, so both are already one comparison per
         * group -- four in total, and exact. A word-level stand-in was tried
         * here and is worse: "some lane is zero" and "some adjacent pair is
         * equal" hold constantly on the zero-padded and run-heavy files that
         * Fill exists for, so the weaker test passed where the exact one
         * rejects, and the two decision_at() calls behind it were then paid
         * for nothing. It cost 24 % on WebAssembly and 13 % on TrueType. */
        maybe |= ((p[2] == 0) | (p[6] == 0)) * WIN_FILL;
        maybe |= ((p[0] == p[1]) | (p[4] == p[5])) * WIN_FILL;
    }
    if (opts & ENC_OPT_DP)
        maybe |= lanes_within(p + 4, 0x09, 0x7E) * WIN_DP;
    return maybe;
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
 * results, so the output is unchanged.
 *
 * `opts` drops the tests for the steps that are not in play; with neither
 * step left there is no decision point ahead at all, and the whole input is
 * one block-mode run. */
static inline size_t next_decision_point(const uint8_t *data, size_t n,
                                         size_t from, const unsigned opts) {
    size_t q = from;
    if (!opts) return n;
    /* Every test below reads at most MIN_PASSTHROUGH_BYTES bytes from q, so
     * while that window is inside the input none of them needs a bound test
     * of its own. That is three comparisons saved per four bytes skipped,
     * on the loop that carries every high-entropy encode. */
    size_t fast_end = n >= MIN_PASSTHROUGH_BYTES ? n - MIN_PASSTHROUGH_BYTES : 0;

    /* Each test below re-reads the window it needs from q rather than
     * carrying what the previous group learned forward. Carrying it forward
     * was tried and is slower: the walks these tests actually perform are
     * two or three bytes long, because the byte that stops them is usually
     * the second one they read, and the bookkeeping to resume a walk costs
     * more per group than repeating one that short. */
    /* Two groups at a time while there is room, one at a time after that.
     * Both loops ask the same question of the same positions; the wide one
     * just gets to ask it of eight bytes at once. */
    if (opts & ENC_OPT_WINDOW) {
        for (; q + 4 <= fast_end; q += 8) {
            int win = window_may_hold(data + q, opts);
            if (!win) continue;
            int dp = (win & WIN_DP) != 0;
            if (decision_at(data, q, opts, dp)) return q;
            if (decision_at(data, q + 4, opts, dp)) return q + 4;
        }
    }
    /* No window test ran for these, so nothing is settled and every step is
     * still in play. */
    for (; q < fast_end; q += 4) {
        if (decision_at(data, q, opts, 1)) return q;
    }
    for (; q < n; q += 4) {
        if ((opts & ENC_OPT_FILL) && q + 2 < n && data[q + 2] == 0) {
            size_t e = q;
            while (e < n && e - q < MIN_TAIL_ZEROS && data[e] == 0) e++;
            if (e - q >= MIN_TAIL_ZEROS && q + MIN_TAIL_ZEROS + 2 <= n) return q;
            e = q + 2;
            while (e < n && e - (q + 2) < MIN_TAIL_ZEROS && data[e] == 0) e++;
            if (e - (q + 2) >= MIN_TAIL_ZEROS) return q;
        }
        if ((opts & ENC_OPT_FILL) && q + 1 < n && data[q + 1] == data[q]) {
            size_t e = q + 1;
            while (e < n && e - q < MIN_FILL_BYTES && data[e] == data[q]) e++;
            if (e - q >= MIN_FILL_BYTES) return q;
        }
        if ((opts & ENC_OPT_DP) && REPRESENTABLE[data[q]]) {
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
 * the eight.
 *
 * Almost no byte reaches that update, though, and the loop is built around
 * which ones can. A byte changes the state only the *first* time its
 * character appears in the segment: a repeated R-Set character is already
 * named by the mask, and a repeated donor folds a rank vector into a
 * minimum that already contains it, which is the same minimum. So the scan
 * carries one 64-bit set of the R-Set indices and donor slots already
 * accounted for -- DP_CLASS numbers them into one space precisely so they
 * can share it -- and retires every repeat in a bit test. What is left is
 * bounded: 13 R-Set characters and 22 donors, so the arithmetic below runs
 * at most 35 times however long the segment is.
 *
 * It also stops where a run of MIN_FILL_IN_SEGMENT_BYTES identical bytes
 * begins, so that Fill can reach runs inside passthrough text (spec 6.5,
 * rule 1). The rolled-back state is what that costs: a run's first byte
 * may have widened the mask or narrowed the profile choice, and the bytes
 * after it cannot have changed anything, being equal to a byte already
 * accounted for. */

/* Everything the scan carries from one byte to the next. It is a struct so
 * that the loop below can hand it to one inlined step rather than repeat
 * that step; the whole of it lives in registers. */
typedef struct {
    uint16_t mask;
    unsigned profile;
    uint64_t k;         /* how many R-Set characters the mask names */
    uint64_t min_donor; /* per profile, the lowest rank a literal has held */
    /* The R-Set indices and donor slots already accounted for, as DP_CLASS
     * numbers them, plus DP_PLAIN, which is set before the scan starts
     * because it never needs accounting for at all. Bit DP_STOP is the one
     * that is never set, which is what lets a not-representable byte fall
     * through the same test. */
    uint64_t seen;

    /* The state as it stood before the most recent change, and where that
     * change happened. At most 35 changes can occur in a segment, so this
     * costs nothing per byte. */
    uint16_t prev_mask;
    unsigned prev_profile;
    size_t prev_pos;
} dp_scan;

/* Folds the byte at `pos` into the scan state. Returns 0 if no profile can
 * carry it, meaning the segment has to end before it, and 1 otherwise.
 *
 * One test at the top decides for nearly every byte, and it is one rather
 * than two on purpose. A byte leaves the state alone for either of two
 * reasons -- no profile spends its character, or its kind is already
 * accounted for -- and asking those separately means a branch that goes
 * both ways on ordinary text, where a third of the bytes are punctuation
 * some profile spends. Nothing predicts such a branch, and the scan was
 * spending more time on the ones it got wrong than on the work itself.
 *
 * So DP_PLAIN's bit is set in `seen` from the start and never cleared, and
 * the two reasons become one bit test that holds for every byte but the at
 * most 35 that change something -- a branch that is never taken twice for
 * the same character, and is right essentially always. */
static inline int dp_absorb(dp_scan *st, uint8_t b, size_t pos) {
    uint8_t cls = DP_CLASS[b];
    uint64_t cls_bit = (uint64_t)1u << cls;
    if (st->seen & cls_bit) return 1;
    if (cls == DP_STOP) return 0; /* not representable at all */
    st->seen |= cls_bit;

    if (cls < DP_DONOR_BASE) {
        /* One more donor to spend: every profile whose lowest literal rank
         * has been reached now drops out. */
        uint64_t viable = lane_ge(st->min_donor, (st->k + 1) * LANE_ONES);
        if (viable == 0) return 0;
        st->prev_mask = st->mask;
        st->prev_profile = st->profile;
        st->prev_pos = pos;
        st->profile = lowest_lane(viable);
        st->mask |= (uint16_t)(1u << (cls - DP_RSET_BASE));
        st->k++;
    } else {
        uint64_t new_min = lane_min(st->min_donor, RANK_PACKED[cls - DP_DONOR_BASE]);
        if (new_min == st->min_donor) return 1; /* ranks below nothing already seen */
        uint64_t viable = lane_ge(new_min, st->k * LANE_ONES);
        if (viable == 0) return 0;
        st->prev_mask = st->mask;
        st->prev_profile = st->profile;
        st->prev_pos = pos;
        st->profile = lowest_lane(viable);
        st->min_donor = new_min;
    }
    return 1;
}

static void scan_dp(const uint8_t *buf, size_t buf_len, size_t *out_len,
                    uint16_t *out_mask, unsigned *out_profile) {
    size_t limit = buf_len < MAX_DP_ANALYSIS_BYTES ? buf_len : MAX_DP_ANALYSIS_BYTES;

    dp_scan st;
    st.mask = 0;
    st.profile = 0;
    st.k = 0;
    st.min_donor = RANK_ABSENT_ALL;
    st.seen = (uint64_t)1u << DP_PLAIN;
    st.prev_mask = 0;
    st.prev_profile = 0;
    st.prev_pos = (size_t)-1;

    size_t i = 0;

    /* The loop below never steps into the middle of a run: when it meets a
     * byte equal to its predecessor it measures that run whole and jumps
     * past it. So a byte that equals its predecessor is always the *second*
     * byte of its run, and the run it belongs to always begins exactly one
     * byte back -- which is why nothing here counts a run length per byte.
     * That is what the previous byte comparison is: not bookkeeping, but the
     * one test that says whether this byte opens a run at all.
     *
     * The first byte has no predecessor to compare against, so it is folded
     * in before the loop rather than paying for a bounds test on every byte
     * that follows. */
    if (limit > 0 && dp_absorb(&st, buf[0], 0)) {
        i = 1;
        while (i < limit) {
            uint8_t b = buf[i];
            if (b == buf[i - 1]) {
                /* Only whether the run reaches MIN_FILL_IN_SEGMENT_BYTES
                 * matters, so the walk stops there -- and it walks bytes
                 * rather than words. Text is full of two-byte repeats, `ll`
                 * and `==` and a double space, and a word-wide scan reads
                 * eight bytes to answer a question the next byte settles.
                 * Measured: reading them a word at a time here costs prose
                 * and source about a fifth of the encoder's throughput,
                 * however much it flatters an instruction count. */
                size_t start = i - 1;
                size_t stop = limit - start > MIN_FILL_IN_SEGMENT_BYTES
                            ? start + MIN_FILL_IN_SEGMENT_BYTES
                            : limit;
                size_t end = i + 1;
                while (end < stop && buf[end] == b) end++;
                if (end - start >= MIN_FILL_IN_SEGMENT_BYTES) {
                    *out_len = start;
                    if (st.prev_pos == start) {
                        *out_mask = st.prev_mask;
                        *out_profile = st.prev_profile;
                    } else {
                        *out_mask = st.mask;
                        *out_profile = st.profile;
                    }
                    return;
                }
                /* Too short to hand to Fill, and every byte of it repeats one
                 * already accounted for, so the run changes nothing the scan
                 * tracks and can be stepped over whole. */
                i = end;
                continue;
            }
            if (!dp_absorb(&st, b, i)) break;
            i++;
        }
    }

    *out_len = i;
    *out_mask = st.mask;
    *out_profile = st.profile;
}

/* Emit one DP segment: its 5-character signal (spec section 9, with the
 * length field biased by one) followed by the transformed bytes. */
static uint8_t *emit_dp_segment(uint8_t *w, const uint8_t *buf, size_t len,
                                uint16_t mask, unsigned profile,
                                xlat_cache *cache) {
    uint64_t payload = ((uint64_t)profile << 24) | ((uint64_t)mask << 11) |
                       (uint64_t)(len - 1);
    value_to_5chars_64(POW2_32 + payload, (char *)w);
    w += 5;

    const uint8_t *xlat = xlat_for(cache, profile, mask, 1);

    /* Four at a time: the four lookups are independent of one another, and
     * writing them back as a group keeps the store unit fed. */
    size_t i = 0;
    for (; i + 4 <= len; i += 4) {
        uint8_t c0 = xlat[buf[i]];
        uint8_t c1 = xlat[buf[i + 1]];
        uint8_t c2 = xlat[buf[i + 2]];
        uint8_t c3 = xlat[buf[i + 3]];
        w[0] = c0;
        w[1] = c1;
        w[2] = c2;
        w[3] = c3;
        w += 4;
    }
    for (; i < len; i++) {
        *w++ = xlat[buf[i]];
    }
    return w;
}

/* Emit one solid Fill signal, variant A (spec section 9). */
static uint8_t *emit_fill_signal(uint8_t *w, uint8_t byte, size_t len) {
    uint64_t payload = ((uint64_t)byte << 11) | (uint64_t)(len - 1);
    value_to_5chars_64(FILL_SIGNAL_BASE + payload, (char *)w);
    return w + 5;
}

/* Emit one Fill signal with a tail, variant B: `zeros` zero bytes and two
 * literals, in the order `order` names. */
static uint8_t *emit_tail_signal(uint8_t *w, size_t zeros, unsigned order,
                                 uint8_t lit0, uint8_t lit1) {
    uint64_t payload = ((uint64_t)order << 21) | ((uint64_t)(zeros - 1) << 16) |
                       ((uint64_t)lit0 << 8) | (uint64_t)lit1;
    value_to_5chars_64(TAIL_SIGNAL_BASE + payload, (char *)w);
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

/* The encoder proper. `opts` is a compile-time constant at every call site
 * (see ENC_OPT_ALL above), so each instantiation keeps only the steps it is
 * built with and pays nothing for the ones it is not. */
static inline base85n_status encode_with(const uint8_t *data, size_t data_len,
                                          char **out_str, size_t *out_len,
                                          const unsigned opts) {
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

    xlat_cache xlat = {{0}, XLAT_NO_KEY};

    while (off < data_len) {
        const uint8_t *buf = data + off;
        size_t buf_len = data_len - off;
        size_t pending = block_start == SIZE_MAX ? 0 : off - block_start;

        /* Step 1: a run of identical bytes long enough to be worth a signal
         * of its own -- either variant of Fill. Both spend five characters,
         * so the one that covers more bytes wins, and a tie goes to the
         * solid variant (spec 6.5).
         *
         * The tail variant exists because a zero run's two neighbouring
         * bytes would otherwise cost a block group of their own: three
         * zeros and two literals are five bytes in five characters, where
         * block mode charges 1.25 characters for each of them. */
        size_t run = (opts & ENC_OPT_FILL) ? fill_run(buf, buf_len) : 0;
        size_t cover = run >= MIN_FILL_BYTES ? run : 0;
        size_t zeros = 0;
        unsigned order = 0;

        if ((opts & ENC_OPT_FILL) && buf[0] == 0) {
            /* The run just counted is the zero run, so the tail variant's
             * length is a comparison rather than a second scan. */
            size_t z = run > MAX_TAIL_ZEROS ? (size_t)MAX_TAIL_ZEROS : run;
            if (z >= MIN_TAIL_ZEROS && z + 2 <= buf_len && z + 2 > cover) {
                cover = z + 2;
                zeros = z;
            }
        }
        if ((opts & ENC_OPT_FILL) && buf_len >= 3 && buf[2] == 0) {
            size_t z = zero_run(buf + 2, buf_len - 2);
            if (z >= MIN_TAIL_ZEROS && z + 2 > cover) {
                cover = z + 2;
                zeros = z;
                order = 1;
            }
        }

        if (cover) {
            if (!ensure_capacity(&out, &cap, &w, block_mode_chars(pending) + 5)) {
                status = BASE85N_ERR_ALLOC;
                break;
            }
            if (block_start != SIZE_MAX) {
                w = process_block_mode(data + block_start, pending, w);
                block_start = SIZE_MAX;
            }
            if (zeros) {
                size_t lit = order ? 0 : zeros;
                w = emit_tail_signal(w, zeros, order, buf[lit], buf[lit + 1]);
            } else {
                w = emit_fill_signal(w, buf[0], run);
            }
            off += cover;
            continue;
        }

        /* Steps 2 and 3. At MIN_PASSTHROUGH_BYTES the two modes cost the
         * same 25 characters and Dynamic Passthrough only gains from there,
         * so the length test settles the size comparison too. */
        size_t best_len = 0;
        uint16_t mask = 0;
        unsigned profile = 0;
        if (opts & ENC_OPT_DP) {
            /* The scan's answer is used only when it reaches
             * MIN_PASSTHROUGH_BYTES, so anything that settles in advance that
             * it cannot retires the call outright. Two things do. A buffer
             * shorter than the threshold can never reach it. And a segment
             * that reached it would have MIN_PASSTHROUGH_BYTES representable
             * bytes at `buf`, so the same word test the skip uses rules the
             * rest out.
             *
             * This is worth its own test because of where the loop arrives
             * here from. A Fill segment ends by continuing straight back to
             * the top, so every one of them is followed by a scan at the next
             * position -- thousands of them on a zero-padded object file, each
             * initialising the scan's state and then failing on its second or
             * third byte. */
            int possible = 1;
            if (opts & ENC_OPT_SCANGATE) {
                possible = buf_len >= MIN_PASSTHROUGH_BYTES;
                if (possible) {
                    int gate = 0;
                    dp_possible(buf, opts, &gate);
                    possible = gate;
                }
            }
            if (possible)
                scan_dp(buf, buf_len, &best_len, &mask, &profile);
        }

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
            w = emit_dp_segment(w, buf, best_len, mask, profile, &xlat);
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
        if (off < data_len &&
            (!(opts & ENC_OPT_DP) || !REPRESENTABLE[data[off]])) {
            size_t next = next_decision_point(data, data_len, off, opts);
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

base85n_status base85n_encode(const uint8_t *data, size_t data_len,
                               char **out_str, size_t *out_len) {
    return encode_with(data, data_len, out_str, out_len, ENC_OPT_ALL);
}

#ifdef BASE85N_BENCH_ENCODERS
/* Non-conforming encoder dialects, built only for the attribution benchmark
 * in bench/speed/. Each drops a step spec 6.5 makes mandatory, so its output
 * is not canonical Base85N -- it decodes, because the decoder accepts every
 * construct wherever it appears, but no conforming encoder would emit it.
 * These are here to answer "where does a speed difference come from", and
 * they are compiled out of every ordinary build. */
base85n_status base85n_encode_bench_nodp(const uint8_t *data, size_t data_len,
                                          char **out_str, size_t *out_len) {
    return encode_with(data, data_len, out_str, out_len, ENC_OPT_FILL);
}

base85n_status base85n_encode_bench_nofill(const uint8_t *data, size_t data_len,
                                            char **out_str, size_t *out_len) {
    return encode_with(data, data_len, out_str, out_len,
                       ENC_OPT_ALL & ~ENC_OPT_FILL);
}

base85n_status base85n_encode_bench_block(const uint8_t *data, size_t data_len,
                                           char **out_str, size_t *out_len) {
    return encode_with(data, data_len, out_str, out_len, 0u);
}

/* Not dialects: every step is in play, only the skip's gate is narrower, so
 * both are conforming encoders whose output is base85n_encode()'s character
 * for character. `narrow` is the encoder as it stood before the gate was
 * widened, and it is what says how much of a `--binary` flag's apparent gain
 * was never about the flag. */
#define ENC_OPT_SKIP_LADDER (ENC_OPT_WIDEGATE | ENC_OPT_WORDGATE | \
                             ENC_OPT_WINDOW | ENC_OPT_SCANGATE)

base85n_status base85n_encode_bench_narrowgate(const uint8_t *data, size_t data_len,
                                                char **out_str, size_t *out_len) {
    return encode_with(data, data_len, out_str, out_len,
                       ENC_OPT_ALL & ~ENC_OPT_SKIP_LADDER);
}

base85n_status base85n_encode_bench_widegate(const uint8_t *data, size_t data_len,
                                              char **out_str, size_t *out_len) {
    return encode_with(data, data_len, out_str, out_len,
                       (ENC_OPT_ALL & ~ENC_OPT_SKIP_LADDER) | ENC_OPT_WIDEGATE);
}

base85n_status base85n_encode_bench_wordgate(const uint8_t *data, size_t data_len,
                                              char **out_str, size_t *out_len) {
    return encode_with(data, data_len, out_str, out_len,
                       ENC_OPT_ALL & ~(ENC_OPT_WINDOW | ENC_OPT_SCANGATE));
}
#endif /* BASE85N_BENCH_ENCODERS */

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
 * the hot loops can keep writing through a register.
 *
 * Only the two Fill signals reach this. Every other construct is covered by
 * the invariant the scan maintains, described where the loop below begins. */
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
    xlat_cache cache = {{0}, XLAT_NO_KEY};

    /* Loop invariant: the buffer has at least as many bytes left as the
     * input has characters left --
     *
     *     cap - w >= n - pos
     *
     * It holds on entry, where cap is the character count and nothing has
     * been written yet, and every construct but Fill preserves it by
     * producing no more bytes than it consumes characters: a block group is
     * four from five, a passthrough segment one per character after its
     * signal, a final group one fewer than it reads.
     *
     * So none of those has to test the buffer at all. A block group needs
     * four bytes and the invariant already promises it the five its
     * characters are worth, which is the check this loop used to make three
     * hundred thousand times per megabyte and never once fail.
     *
     * Fill is the exception -- 2048 bytes out of a five-character signal --
     * and is the only place that grows the buffer. It asks for what it is
     * about to write plus what the rest of the input could still need, which
     * is what puts the invariant back for everything after it. */
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
                /* Standard Base85N block: 4 bytes, Big-Endian. The invariant
                 * covers them -- five characters were left, so five bytes
                 * are. */
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

            if (decoded_value >= TAIL_SIGNAL_BASE) {
                /* Section 7.4, variant B: zeros and two literals, in the
                 * order the payload's top bit names. No characters are read
                 * to construct any of it. */
                uint64_t payload = decoded_value - TAIL_SIGNAL_BASE;
                size_t zeros = (size_t)((payload >> 16) & 0x1Fu) + 1;
                uint8_t lit0 = (uint8_t)((payload >> 8) & 0xFFu);
                uint8_t lit1 = (uint8_t)(payload & 0xFFu);
                ENSURE_OUTPUT(zeros + 2 + (n - pos));
                if (payload & ((uint64_t)1u << 21)) {
                    buf[w] = lit0;
                    buf[w + 1] = lit1;
                    memset(buf + w + 2, 0, zeros);
                } else {
                    memset(buf + w, 0, zeros);
                    buf[w + zeros] = lit0;
                    buf[w + zeros + 1] = lit1;
                }
                w += zeros + 2;
                continue;
            }

            if (decoded_value >= FILL_SIGNAL_BASE) {
                /* Section 7.4, variant A: no characters are read either. */
                uint64_t payload = decoded_value - FILL_SIGNAL_BASE;
                size_t fill_len = (size_t)(payload & 0x7FFu) + 1;
                uint8_t byte = (uint8_t)((payload >> 11) & 0xFFu);
                ENSURE_OUTPUT(fill_len + (n - pos));
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

            /* The invariant covers the segment's bytes: the characters it is
             * about to read are still ahead of pos, and it writes one byte
             * per character. */
            if (n - pos < seg_len) return BASE85N_ERR_UNEXPECTED_EOF;

            /* Section 4.3: one character in, one byte out, with no state
             * carried between characters. */
            /* The table covers all 256 byte values, so this indexes it with
             * the character as read. Carrying the rejection to the end of
             * the segment through an OR, instead of branching per character,
             * was tried and is slower: the branch is never taken on a valid
             * stream and the predictor has no trouble with it, while the
             * accumulator costs an operation the loop cannot hide. */
            const uint8_t *xlat = xlat_for(&cache, profile, mask, 0);
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
             * block (2 characters are the minimum for 1 byte).
             *
             * The character still has to be one first. Section 10 makes a
             * significant character outside Alphabet-N an INVALID_CHARACTER
             * unconditionally, and Section 8 gives no digit value to one, so
             * a character that has no value cannot be the trailing group
             * whose size is being complained about. Reporting the size
             * instead was a real divergence: it is what the C and Go
             * implementations did and the Rust and TypeScript ones did not,
             * and differential fuzzing is what found it. */
            if (ALPHABET_VALUE[in[pos]] < 0) return BASE85N_ERR_INVALID_CHAR;
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

            /* One byte fewer than the characters read, so the invariant
             * covers these too. */
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
