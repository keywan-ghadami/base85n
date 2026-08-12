/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/*
 * test_base85n.c - Self-contained test suite for the Base85N C library.
 *
 * Sections:
 *   (a) Golden test vectors from testvectors/vectors.tsv.
 *   (b) Randomized round-trip property tests (fixed seed).
 *   (c) Explicit boundary / edge-case tests.
 *   (d) Decode-error (malformed input) tests.
 *
 * No external test framework: a couple of small ASSERT macros track a
 * pass/fail count and the process exits non-zero if anything failed.
 */

#include "base85n.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/* Tiny test harness                                                    */
/* ------------------------------------------------------------------ */

static long g_tests_run = 0;
static long g_tests_failed = 0;

#define ASSERT_TRUE(cond, msg)                                              \
    do {                                                                    \
        g_tests_run++;                                                     \
        if (!(cond)) {                                                     \
            g_tests_failed++;                                              \
            fprintf(stderr, "FAIL (%s:%d): %s\n", __FILE__, __LINE__, (msg)); \
        }                                                                   \
    } while (0)

/* ------------------------------------------------------------------ */
/* Shared helpers                                                       */
/* ------------------------------------------------------------------ */

static const char TEST_ALPHABET[] =
    "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
    ".-:+=^!/*?`_~()[]{}@%$#";

static const uint8_t TEST_RSET_ASCII[13] = {
    32, 34, 39, 44, 59, 92, 124, 60, 62, 38, 9, 10, 13
};

/* The eight replacement alphabets (spec 4.2), as [alphabet][R-Set index j]
 * -> donor character, 0 where that alphabet does not carry R_Char[j]. This
 * is an independent copy of the table in src/base85n.c; the tests below
 * drive every entry through the public API, so a typo in either cannot
 * survive `make test`. */
#define TEST_NUM_ALPHABETS 8
static const char ALPHABET_SUB_EXPECTED[TEST_NUM_ALPHABETS][13] = {
    /* 0 none   */ {   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0 },
    /* 1 text   */ { '^',   0,   0,   0,   0,   0,   0,   0,   0,   0, '$', '@', '%' },
    /* 2 prose  */ { '^', '$', '?', '%', '!',   0,   0,   0,   0,   0,   0, '@',   0 },
    /* 3 markup */ { '^', '!', '~', '{',   0,   0,   0, '%', '$', '?',   0, '@',   0 },
    /* 4 json   */ { '^', '%',   0, '$',   0, '?',   0,   0,   0,   0,   0, '@', '!' },
    /* 5 code   */ { '^', '?', '!', '%', '$',   0,   0,   0, '`',   0, '~', '@',   0 },
    /* 6 shell  */ { '^', '?', '!',   0, '#', '$', '%',   0,   0, '~',   0, '@',   0 },
    /* 7 full   */ { '^', '~', '#', '?', '!', '`', '_', '*', '+', '=', '$', '@', '%' },
};

/* Local re-implementation of the spec's 5-digit Base85 encoder, used
 * only by the malformed-input tests below to hand-construct DP
 * signals (the public API intentionally does not expose this). */
static void test_value_to_5chars(uint64_t value, char out[5]) {
    uint8_t digits[5];
    for (int i = 4; i >= 0; i--) {
        digits[i] = (uint8_t)(value % 85);
        value /= 85;
    }
    for (int i = 0; i < 5; i++) out[i] = TEST_ALPHABET[digits[i]];
}

static int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static uint8_t *hex_decode(const char *hex, size_t hexlen, size_t *out_len) {
    if (hexlen % 2 != 0) return NULL;
    size_t n = hexlen / 2;
    uint8_t *buf = (uint8_t *)malloc(n > 0 ? n : 1);
    if (!buf) return NULL;
    for (size_t i = 0; i < n; i++) {
        int hi = hex_val(hex[2 * i]);
        int lo = hex_val(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) { free(buf); return NULL; }
        buf[i] = (uint8_t)((hi << 4) | lo);
    }
    *out_len = n;
    return buf;
}

/* Reads one line (without the trailing newline) from f into a freshly
 * malloc'd NUL-terminated buffer. Returns NULL at EOF with nothing
 * read. Handles arbitrarily long lines. */
static char *read_line(FILE *f) {
    size_t cap = 256, len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) return NULL;
    int c;
    int got_any = 0;
    while ((c = fgetc(f)) != EOF) {
        got_any = 1;
        if (c == '\n') break;
        if (len + 1 >= cap) {
            cap *= 2;
            char *p = (char *)realloc(buf, cap);
            if (!p) { free(buf); return NULL; }
            buf = p;
        }
        buf[len++] = (char)c;
    }
    if (!got_any) { free(buf); return NULL; }
    if (len > 0 && buf[len - 1] == '\r') len--;
    buf[len] = '\0';
    return buf;
}

static void roundtrip_check(const uint8_t *data, size_t len, const char *label) {
    char msg[320];
    char *enc = NULL;
    size_t enc_len = 0;
    base85n_status st = base85n_encode(data, len, &enc, &enc_len);
    snprintf(msg, sizeof msg, "encode status OK (%s, len=%zu): %s", label, len,
              base85n_strerror(st));
    ASSERT_TRUE(st == BASE85N_OK, msg);
    if (st != BASE85N_OK) return;

    uint8_t *dec = NULL;
    size_t dec_len = 0;
    base85n_status dst = base85n_decode(enc, enc_len, &dec, &dec_len);
    snprintf(msg, sizeof msg, "decode status OK (%s, len=%zu): %s", label, len,
              base85n_strerror(dst));
    ASSERT_TRUE(dst == BASE85N_OK, msg);
    if (dst == BASE85N_OK) {
        snprintf(msg, sizeof msg, "round-trip matches original (%s, len=%zu)", label, len);
        ASSERT_TRUE(dec_len == len && (len == 0 || memcmp(dec, data, len) == 0), msg);
        free(dec);
    }
    free(enc);
}

/* ------------------------------------------------------------------ */
/* Shared test-vector lookup                                            */
/* ------------------------------------------------------------------ */

/* Opens a file from the repository's testvectors/ directory.
 *
 * The build systems pass the directory in as BASE85N_TESTVECTOR_DIR, so the
 * test binary finds the vectors regardless of the working directory it is
 * started from -- `make test` runs it from c/, while ctest runs it from the
 * CMake build directory. The relative paths are kept as a fallback for a
 * hand-rolled compile that does not define the macro. */
static FILE *open_vector_file(const char *name) {
    char path[1024];
    const char *prefixes[] = {
#ifdef BASE85N_TESTVECTOR_DIR
        BASE85N_TESTVECTOR_DIR "/",
#endif
        "../testvectors/",
        "testvectors/",
        "../../testvectors/"
    };
    for (size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); i++) {
        int n = snprintf(path, sizeof path, "%s%s", prefixes[i], name);
        if (n < 0 || (size_t)n >= sizeof path) continue;
        FILE *f = fopen(path, "r");
        if (f) return f;
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* (a) Golden test vectors                                              */
/* ------------------------------------------------------------------ */

static void test_golden_vectors(void) {
    FILE *f = open_vector_file("vectors.tsv");
    ASSERT_TRUE(f != NULL, "could not open testvectors/vectors.tsv from any known path");
    if (!f) return;

    char *line = read_line(f); /* header */
    ASSERT_TRUE(line != NULL, "vectors.tsv appears empty (no header line)");
    free(line);

    int count = 0;
    while ((line = read_line(f)) != NULL) {
        if (line[0] == '\0') { free(line); continue; }

        char *t1 = strchr(line, '\t');
        if (!t1) { ASSERT_TRUE(0, "vector line missing first tab"); free(line); continue; }
        *t1 = '\0';
        char *name = line;
        char *rest = t1 + 1;

        char *t2 = strchr(rest, '\t');
        if (!t2) { ASSERT_TRUE(0, "vector line missing second tab"); free(line); continue; }
        *t2 = '\0';
        char *input_hex = rest;
        char *output = t2 + 1;

        size_t hexlen = strlen(input_hex);
        uint8_t *data = NULL;
        size_t data_len = 0;
        if (hexlen > 0) {
            data = hex_decode(input_hex, hexlen, &data_len);
            char msg[128];
            snprintf(msg, sizeof msg, "hex decode ok for vector '%s'", name);
            ASSERT_TRUE(data != NULL, msg);
        }

        char msg[256];

        /* encode(data) == output */
        char *enc = NULL;
        size_t enc_len = 0;
        base85n_status est = base85n_encode(data, data_len, &enc, &enc_len);
        snprintf(msg, sizeof msg, "encode status OK for vector '%s'", name);
        ASSERT_TRUE(est == BASE85N_OK, msg);
        if (est == BASE85N_OK) {
            size_t out_len = strlen(output);
            snprintf(msg, sizeof msg, "encode output matches golden for vector '%s'", name);
            ASSERT_TRUE(enc_len == out_len && (out_len == 0 || memcmp(enc, output, out_len) == 0), msg);
        }

        /* decode(golden output) == data */
        uint8_t *dec = NULL;
        size_t dec_len = 0;
        base85n_status dst = base85n_decode(output, strlen(output), &dec, &dec_len);
        snprintf(msg, sizeof msg, "decode(golden output) status OK for vector '%s'", name);
        ASSERT_TRUE(dst == BASE85N_OK, msg);
        if (dst == BASE85N_OK) {
            snprintf(msg, sizeof msg, "decode(golden output) matches original for vector '%s'", name);
            ASSERT_TRUE(dec_len == data_len && (data_len == 0 || memcmp(dec, data, data_len) == 0), msg);
            free(dec);
        }

        /* Also round-trip through our own encoder's output, to make sure
         * base85n_decode(base85n_encode(x)) == x independently of whether
         * our encoder matches the golden bytes exactly. */
        if (est == BASE85N_OK) {
            uint8_t *dec2 = NULL;
            size_t dec2_len = 0;
            base85n_status dst2 = base85n_decode(enc, enc_len, &dec2, &dec2_len);
            snprintf(msg, sizeof msg, "decode(own encode) status OK for vector '%s'", name);
            ASSERT_TRUE(dst2 == BASE85N_OK, msg);
            if (dst2 == BASE85N_OK) {
                snprintf(msg, sizeof msg, "decode(own encode) matches original for vector '%s'", name);
                ASSERT_TRUE(dec2_len == data_len && (data_len == 0 || memcmp(dec2, data, data_len) == 0), msg);
                free(dec2);
            }
            free(enc);
        }

        free(data);
        free(line);
        count++;
    }
    fclose(f);
    printf("[golden vectors] processed %d vectors\n", count);
}

/* ------------------------------------------------------------------ */
/* (b) Randomized round-trip property tests                             */
/* ------------------------------------------------------------------ */

static uint8_t gen_pure_random(void) {
    return (uint8_t)(rand() % 256);
}

static uint8_t gen_alphabet_literal(void) {
    return (uint8_t)TEST_ALPHABET[rand() % 85];
}

/* Every character any replacement alphabet spends as a donor (spec 4.2):
 * the bytes whose meaning depends on the segment's alphabet. */
static const char TEST_DONORS[] = "^@%$?!~#*+=_`{";
#define TEST_DONOR_COUNT (sizeof TEST_DONORS - 1)

static uint8_t gen_rset_or_donor(void) {
    int r = rand() % 100;
    if (r < 60) return TEST_RSET_ASCII[rand() % 13];
    return (uint8_t)TEST_DONORS[rand() % TEST_DONOR_COUNT];
}

static uint8_t gen_mixed(void) {
    int r = rand() % 100;
    if (r < 25) return gen_pure_random();
    if (r < 60) return gen_alphabet_literal();
    if (r < 90) return TEST_RSET_ASCII[rand() % 13];
    return (uint8_t)TEST_DONORS[rand() % TEST_DONOR_COUNT];
}

typedef uint8_t (*byte_gen_fn)(void);

static void test_random_roundtrips(void) {
    srand(12345);

    struct { const char *name; byte_gen_fn gen; } generators[] = {
        {"pure_random", gen_pure_random},
        {"alphabet_literal", gen_alphabet_literal},
        {"rset_donor_heavy", gen_rset_or_donor},
        {"mixed", gen_mixed},
    };

    size_t lengths[] = {0, 1, 2, 3, 4, 5, 10, 17, 19, 20, 21, 50, 100,
                         255, 256, 511, 512, 513, 600, 1000, 2500};

    uint8_t *buf = (uint8_t *)malloc(4096);
    ASSERT_TRUE(buf != NULL, "test buffer allocation");
    if (!buf) return;

    int trials = 0;
    for (size_t g = 0; g < sizeof(generators) / sizeof(generators[0]); g++) {
        for (size_t li = 0; li < sizeof(lengths) / sizeof(lengths[0]); li++) {
            size_t len = lengths[li];
            for (int rep = 0; rep < 2; rep++) {
                for (size_t i = 0; i < len; i++) buf[i] = generators[g].gen();
                char label[64];
                snprintf(label, sizeof label, "%s#%d", generators[g].name, rep);
                roundtrip_check(len == 0 ? NULL : buf, len, label);
                trials++;
            }
        }
    }
    free(buf);
    printf("[random round-trips] ran %d trials\n", trials);
}

/* ------------------------------------------------------------------ */
/* (c) Explicit edge cases                                              */
/* ------------------------------------------------------------------ */

static void test_edge_cases(void) {
    /* Empty input. */
    roundtrip_check(NULL, 0, "empty");

    /* Lengths 1..4 (block-mode partial boundaries). */
    for (size_t len = 1; len <= 4; len++) {
        uint8_t data[4];
        for (size_t i = 0; i < len; i++) data[i] = (uint8_t)(0x30 + i);
        char label[32];
        snprintf(label, sizeof label, "len_%zu_boundary", len);
        roundtrip_check(data, len, label);
    }

    /* MIN_PASSTHROUGH_BYTES boundary: 19 (below), 20 (exactly), 21 (above),
     * using DP-friendly literal content so the boundary actually matters
     * for mode selection. */
    {
        uint8_t data[21];
        for (int i = 0; i < 21; i++) data[i] = (uint8_t)('a' + (i % 26));
        roundtrip_check(data, BASE85N_MIN_PASSTHROUGH_BYTES - 1, "one_below_min_passthrough");
        roundtrip_check(data, BASE85N_MIN_PASSTHROUGH_BYTES, "exactly_min_passthrough");
        roundtrip_check(data, BASE85N_MIN_PASSTHROUGH_BYTES + 1, "one_above_min_passthrough");
    }

    /* Long enough to require multiple DP signal segments (a candidate
     * prefix is capped at MAX_DP_ANALYSIS_BYTES == 1024). Pure alphabet
     * literals, so one output character per input byte. */
    {
        size_t len = (size_t)BASE85N_MAX_DP_OUTPUT_CHARS_PER_SIGNAL * 2 + 37;
        uint8_t *data = (uint8_t *)malloc(len);
        ASSERT_TRUE(data != NULL, "multi-DP-segment buffer allocation");
        if (data) {
            for (size_t i = 0; i < len; i++) data[i] = (uint8_t)TEST_ALPHABET[i % 85];
            roundtrip_check(data, len, "multi_dp_segment");
            free(data);
        }
    }

    /* The MAX_DP_ANALYSIS_BYTES boundary: exactly 1024 representable bytes
     * are one segment, and one more needs a second. */
    {
        size_t w = BASE85N_MAX_DP_ANALYSIS_BYTES;
        uint8_t *data = (uint8_t *)malloc(w + 1);
        ASSERT_TRUE(data != NULL, "analysis-window buffer allocation");
        if (data) {
            memset(data, 'x', w + 1);
            char *enc = NULL;
            size_t enc_len = 0;
            if (base85n_encode(data, w, &enc, &enc_len) == BASE85N_OK) {
                ASSERT_TRUE(enc_len == w + 5,
                            "exactly MAX_DP_ANALYSIS_BYTES literals are one DP segment");
                free(enc);
            }
            roundtrip_check(data, w, "analysis_window_exact");
            roundtrip_check(data, w + 1, "analysis_window_plus_one");
            free(data);
        }
    }

    /* A literal donor character is representable under any alphabet that
     * does not spend it. With a space in the run, the alphabets that could
     * carry the space all spend '^' on it, so the run breaks at the '^'. */
    {
        for (int a = 0; a < TEST_NUM_ALPHABETS; a++) {
            for (int j = 0; j < 13; j++) {
                char donor = ALPHABET_SUB_EXPECTED[a][j];
                if (donor == 0) continue;
                uint8_t data[60];
                size_t idx = 0;
                for (int i = 0; i < 25; i++) data[idx++] = (uint8_t)'a';
                data[idx++] = (uint8_t)' ';
                data[idx++] = (uint8_t)donor;
                data[idx++] = (uint8_t)' ';
                for (int i = 0; i < 25; i++) data[idx++] = (uint8_t)'b';
                roundtrip_check(data, idx, "literal_donor_breaks_run");
            }
        }
    }

    /* Each alphabet has to carry the R-Set characters it substitutes. */
    {
        for (int a = 0; a < TEST_NUM_ALPHABETS; a++) {
            uint8_t data[256];
            size_t idx = 0;
            int progressed = 1;
            while (idx + 5 <= sizeof data && progressed) {
                progressed = 0;
                for (int j = 0; j < 13 && idx + 5 <= sizeof data; j++) {
                    if (ALPHABET_SUB_EXPECTED[a][j] == 0) continue;
                    data[idx++] = TEST_RSET_ASCII[j];
                    memcpy(data + idx, "word", 4);
                    idx += 4;
                    progressed = 1;
                }
            }
            if (idx >= BASE85N_MIN_PASSTHROUGH_BYTES) {
                roundtrip_check(data, idx, "alphabet_carries_its_rset_chars");
            }
        }
    }

    /* Every byte value 0-255. */
    {
        uint8_t data[256];
        for (int i = 0; i < 256; i++) data[i] = (uint8_t)i;
        roundtrip_check(data, 256, "all_byte_values");
    }
}

/* ------------------------------------------------------------------ */
/* (d) Decode-error tests                                               */
/* ------------------------------------------------------------------ */

static void expect_decode_error(const char *s, size_t s_len, base85n_status expected,
                                  const char *label) {
    uint8_t *out = NULL;
    size_t out_len = 0;
    base85n_status st = base85n_decode(s, s_len, &out, &out_len);
    char msg[256];
    snprintf(msg, sizeof msg, "decode('%s') returns non-OK error (%s), got: %s",
             label, label, base85n_strerror(st));
    ASSERT_TRUE(st != BASE85N_OK, msg);
    if (expected != BASE85N_OK) {
        snprintf(msg, sizeof msg, "decode('%s') returns expected status %s, got %s",
                 label, base85n_strerror(expected), base85n_strerror(st));
        ASSERT_TRUE(st == expected, msg);
    }
    if (st == BASE85N_OK) free(out); /* shouldn't happen, but don't leak if it does */
}

static void test_decode_errors(void) {
    /* Invalid character outside Alphabet-N, in a full 5-char group. */
    {
        const char *s = "abcd&"; /* '&' is an R-Set char, never valid in Alphabet-N */
        expect_decode_error(s, strlen(s), BASE85N_ERR_INVALID_CHAR, "invalid_char_in_group");
    }

    /* Invalid character inside DP transformed data. */
    {
        char sig[5];
        test_value_to_5chars(((uint64_t)1 << 32) | 4 /* alphabet=0, len=5 */, sig);
        char buf[16];
        size_t n = 0;
        memcpy(buf + n, sig, 5); n += 5;
        memcpy(buf + n, "ab&de", 5); n += 5; /* '&' invalid at index 2 */
        expect_decode_error(buf, n, BASE85N_ERR_INVALID_CHAR, "invalid_char_in_dp_data");
    }

    /* DP signal whose declared length overruns available input. */
    {
        char sig[5];
        test_value_to_5chars(((uint64_t)1 << 32) | 49 /* alphabet=0, len=50 */, sig);
        char buf[32];
        size_t n = 0;
        memcpy(buf + n, sig, 5); n += 5;
        const char *data10 = "abcdefghij"; /* only 10 chars, but 50 declared */
        memcpy(buf + n, data10, 10); n += 10;
        expect_decode_error(buf, n, BASE85N_ERR_UNEXPECTED_EOF, "dp_length_overrun");
    }

    /* The length field is stored biased by one, so a signal naming one
     * character still needs that character to follow it. A decoder that
     * forgets the bias reads nothing here and accepts the stream. */
    {
        char sig[5];
        test_value_to_5chars(((uint64_t)1 << 32) | 0 /* alphabet=0, len=1 */, sig);
        expect_decode_error(sig, 5, BASE85N_ERR_UNEXPECTED_EOF, "length_bias_needs_its_char");

        char buf[8];
        memcpy(buf, sig, 5);
        buf[5] = 'a';
        uint8_t *out = NULL;
        size_t out_len = 0;
        base85n_status st = base85n_decode(buf, 6, &out, &out_len);
        ASSERT_TRUE(st == BASE85N_OK && out_len == 1 && out && out[0] == 'a',
                    "a signal with length field 0 names a one-character segment");
        free(out);
    }

    /* Signal payload in the reserved range above 2^13 - 1. */
    {
        char sig[5];
        uint64_t payload = ((uint64_t)1 << 13); /* one above max (2^13 - 1) */
        test_value_to_5chars(((uint64_t)1 << 32) | payload, sig);
        expect_decode_error(sig, 5, BASE85N_ERR_RESERVED_SIGNAL, "reserved_signal_payload");
    }

    /* Reserved signal via the maximal 5-char group ("#####" = 85^5 - 1). */
    {
        const char *s = "#####";
        expect_decode_error(s, strlen(s), BASE85N_ERR_RESERVED_SIGNAL, "reserved_signal_max_group");
    }

    /* Invalid single-character trailing group (after a valid full block). */
    {
        /* Encode 4 literal bytes -> exactly one full 5-char block-mode group
         * (too short to ever trigger DP mode), then append one stray
         * trailing character to create an invalid 1-char partial group. */
        uint8_t four[4] = {'T', 'e', 's', 't'};
        char *enc = NULL;
        size_t enc_len = 0;
        base85n_status est = base85n_encode(four, 4, &enc, &enc_len);
        ASSERT_TRUE(est == BASE85N_OK, "setup: encode 4 bytes for partial-block test");
        if (est == BASE85N_OK) {
            char *buf = (char *)malloc(enc_len + 1);
            ASSERT_TRUE(buf != NULL, "setup: buffer alloc for partial-block test");
            if (buf) {
                memcpy(buf, enc, enc_len);
                buf[enc_len] = 'a'; /* stray trailing 1-char group */
                expect_decode_error(buf, enc_len + 1, BASE85N_ERR_INVALID_PARTIAL_BLOCK,
                                     "lone_trailing_char");
                free(buf);
            }
            free(enc);
        }
    }

    /* Also a bare single character with nothing else in the stream. */
    {
        const char *s = "a";
        expect_decode_error(s, strlen(s), BASE85N_ERR_INVALID_PARTIAL_BLOCK, "bare_single_char");
    }

    /* Spec 7.1: a trailing group is padded with '#' and the result must be
     * below 2^32. "%nSb" pads to 2^32 - 2 and decodes; "%nSc" is the very next
     * group and pads to 2^32 + 83, so the pair pins the boundary rather than
     * just its far side. The 2- and 3-character forms take a different branch
     * of the padding. */
    {
        static const uint8_t expected[3] = {0xFF, 0xFF, 0xFF};
        uint8_t *out = NULL;
        size_t out_len = 0;
        base85n_status st = base85n_decode("%nSb", 4, &out, &out_len);
        ASSERT_TRUE(st == BASE85N_OK, "partial_block_below_limit: decodes");
        if (st == BASE85N_OK) {
            ASSERT_TRUE(out_len == 3 && memcmp(out, expected, 3) == 0,
                        "partial_block_below_limit: yields ff ff ff");
            free(out);
        }

        expect_decode_error("%nSc", 4, BASE85N_ERR_INVALID_PARTIAL_BLOCK,
                            "partial_block_over_limit");
        expect_decode_error("###", 3, BASE85N_ERR_INVALID_PARTIAL_BLOCK,
                            "partial_block_three_chars_over_limit");
        expect_decode_error("##", 2, BASE85N_ERR_INVALID_PARTIAL_BLOCK,
                            "partial_block_two_chars_over_limit");
    }

    /* Sanity: NULL/0-length inputs must not crash and must succeed
     * trivially (not an error case, but exercised here for safety). */
    {
        uint8_t *out = NULL;
        size_t out_len = 123;
        base85n_status st = base85n_decode(NULL, 0, &out, &out_len);
        ASSERT_TRUE(st == BASE85N_OK, "decode(NULL, 0) should succeed");
        ASSERT_TRUE(out != NULL && out_len == 0, "decode(NULL, 0) yields empty result");
        free(out);
    }
}

/* ------------------------------------------------------------------ */
/* (e) Adversarial decode vectors                                       */
/*                                                                      */
/* testvectors/adversarial_vectors.tsv: multi-byte Unicode input at     */
/* various positions (character-position vs. storage-unit discrepancy   */
/* -- moot in C since base85n_decode takes raw bytes/length directly,   */
/* but still exercised here for cross-language parity), 0-length DP     */
/* signals, invalid/reserved DP signals, and deliberately malformed     */
/* escaping. Columns: name, category, kind, input_hex, error_code,      */
/* expected_hex.                                                        */
/* ------------------------------------------------------------------ */

static base85n_status status_for_error_code(const char *code) {
    if (strcmp(code, "invalid_character") == 0) return BASE85N_ERR_INVALID_CHAR;
    if (strcmp(code, "unexpected_end_of_stream") == 0) return BASE85N_ERR_UNEXPECTED_EOF;
        if (strcmp(code, "reserved_signal_value") == 0) return BASE85N_ERR_RESERVED_SIGNAL;
    if (strcmp(code, "invalid_partial_block_length") == 0) return BASE85N_ERR_INVALID_PARTIAL_BLOCK;
    return BASE85N_OK; /* unknown code: caller treats this as a hard failure */
}

static void test_adversarial_vectors(void) {
    FILE *f = open_vector_file("adversarial_vectors.tsv");
    ASSERT_TRUE(f != NULL,
                "could not open testvectors/adversarial_vectors.tsv from any known path");
    if (!f) return;

    char *line = read_line(f); /* header */
    ASSERT_TRUE(line != NULL, "adversarial_vectors.tsv appears empty (no header line)");
    free(line);

    int count = 0;
    while ((line = read_line(f)) != NULL) {
        if (line[0] == '\0') { free(line); continue; }

        char *fields[6] = {0};
        char *cursor = line;
        int nfields = 0;
        for (; nfields < 6; nfields++) {
            fields[nfields] = cursor;
            char *tab = strchr(cursor, '\t');
            if (!tab) { nfields++; break; }
            *tab = '\0';
            cursor = tab + 1;
        }
        ASSERT_TRUE(nfields == 6, "adversarial vector line has 6 tab-separated fields");
        if (nfields != 6) { free(line); continue; }

        const char *name = fields[0];
        /* fields[1] = category, unused here */
        const char *kind = fields[2];
        const char *input_hex = fields[3];
        const char *error_code = fields[4];
        const char *expected_hex = fields[5];

        size_t hexlen = strlen(input_hex);
        uint8_t *data = NULL;
        size_t data_len = 0;
        if (hexlen > 0) {
            data = hex_decode(input_hex, hexlen, &data_len);
            char msg[160];
            snprintf(msg, sizeof msg, "hex decode ok for adversarial vector '%s'", name);
            ASSERT_TRUE(data != NULL, msg);
        }

        uint8_t *out = NULL;
        size_t out_len = 0;
        base85n_status st = base85n_decode((const char *)data, data_len, &out, &out_len);
        char msg[256];

        if (strcmp(kind, "must_fail") == 0) {
            base85n_status expected = status_for_error_code(error_code);
            snprintf(msg, sizeof msg, "adversarial vector '%s': decode should fail with %s, got %s",
                     name, base85n_strerror(expected), base85n_strerror(st));
            ASSERT_TRUE(st == expected && st != BASE85N_OK, msg);
            if (st == BASE85N_OK) free(out);
        } else if (strcmp(kind, "valid") == 0) {
            snprintf(msg, sizeof msg, "adversarial vector '%s': decode should succeed, got %s",
                     name, base85n_strerror(st));
            ASSERT_TRUE(st == BASE85N_OK, msg);
            if (st == BASE85N_OK) {
                size_t want_len = 0;
                uint8_t *want = hex_decode(expected_hex, strlen(expected_hex), &want_len);
                snprintf(msg, sizeof msg, "adversarial vector '%s': decoded bytes match expected_hex", name);
                ASSERT_TRUE(out_len == want_len && (want_len == 0 || memcmp(out, want, want_len) == 0), msg);
                free(want);
                free(out);
            }
        } else {
            ASSERT_TRUE(0, "adversarial vector line has unknown 'kind'");
        }

        free(data);
        free(line);
        count++;
    }
    fclose(f);
    ASSERT_TRUE(count >= 15, "expected a non-trivial adversarial vector set");
    printf("[adversarial vectors] processed %d vectors\n", count);
}

/* ------------------------------------------------------------------ */
/* Lookup-table consistency                                             */
/* ------------------------------------------------------------------ */

/* base85n.c carries its lookup tables written out as literals, because
 * deriving them at run time cost a linear scan per input byte. A literal
 * table can hold a typo that round-trip tests never notice -- a swapped
 * pair round-trips perfectly and still produces the wrong stream -- so
 * check the tables' observable effects through the public API instead; the
 * tables themselves are static to that translation unit.
 *
 * Alphabet-N membership is observable through decode: a character outside
 * the alphabet must be rejected with BASE85N_ERR_INVALID_CHAR, and one
 * inside it must not be. R-Set membership and the substitution tables are
 * observable through a DP-mode encode: a run of an R-Set byte comes back as
 * a run of the donor character its segment's alphabet spends on it. Which
 * alphabet that is is observable the same way, by building a run only some
 * alphabets can carry. The base-85 digit-pair table is observable through
 * block mode, against the spec's own conversion. */
/* Alphabet selection (spec 6.1, step 1) and the substitutions it implies.
 *
 * A table that swapped two donors would still round-trip, because the
 * decoder would mirror the mistake, but it would emit a stream no other
 * implementation agrees with. So each probe drives one alphabet through a
 * DP-eligible run and checks the exact characters that come back.
 *
 * The probe for alphabet `a` is built from the R-Set characters `a`
 * carries. Any alphabet carrying all of them reaches the end of the run, so
 * the smallest such identifier wins -- which is what this then asserts,
 * pinning the tie-break rule as well as the table. */
static void test_alphabet_selection(void) {
    for (int a = 0; a < TEST_NUM_ALPHABETS; a++) {
        uint8_t buf[256];
        char expected[256];
        size_t n = 0, e = 0;

        int progressed = 1;
        while (n + 5 <= sizeof buf && progressed) {
            progressed = 0;
            for (int j = 0; j < 13 && n + 5 <= sizeof buf; j++) {
                char donor = ALPHABET_SUB_EXPECTED[a][j];
                if (donor == 0) continue;
                buf[n++] = TEST_RSET_ASCII[j];
                expected[e++] = donor;
                memcpy(buf + n, "word", 4);
                memcpy(expected + e, "word", 4);
                n += 4;
                e += 4;
                progressed = 1;
            }
        }
        if (n < BASE85N_MIN_PASSTHROUGH_BYTES) continue;

        /* The alphabet the encoder should actually pick: the smallest one
         * that carries every R-Set character present in the probe. */
        int want_a = -1;
        for (int c = 0; c < TEST_NUM_ALPHABETS && want_a < 0; c++) {
            int ok = 1;
            for (int j = 0; j < 13 && ok; j++) {
                if (ALPHABET_SUB_EXPECTED[a][j] != 0 &&
                    ALPHABET_SUB_EXPECTED[c][j] == 0) {
                    ok = 0;
                }
            }
            if (ok) want_a = c;
        }

        /* Rebuild the expectation under the alphabet that actually wins. */
        e = 0;
        for (size_t i = 0; i < n; i++) {
            int j = -1;
            for (int k = 0; k < 13; k++) {
                if (TEST_RSET_ASCII[k] == buf[i]) { j = k; break; }
            }
            expected[e++] = j < 0 ? (char)buf[i] : ALPHABET_SUB_EXPECTED[want_a][j];
        }

        char *enc = NULL;
        size_t enc_len = 0;
        base85n_status st = base85n_encode(buf, n, &enc, &enc_len);
        char msg[160];
        snprintf(msg, sizeof msg,
                  "alphabet %d's R-Set characters encode under alphabet %d",
                  a, want_a);
        ASSERT_TRUE(st == BASE85N_OK && enc_len == 5 + e &&
                     memcmp(enc + 5, expected, e) == 0, msg);
        free(enc);
    }

    /* A donor character standing for itself: under alphabet 0 nothing is
     * substituted, so a run of Alphabet-N characters that includes donors
     * comes back unchanged. */
    {
        uint8_t buf[64];
        size_t n = 0;
        for (size_t i = 0; i < TEST_DONOR_COUNT; i++) buf[n++] = (uint8_t)TEST_DONORS[i];
        while (n < 40) buf[n++] = 'a';

        char *enc = NULL;
        size_t enc_len = 0;
        base85n_status st = base85n_encode(buf, n, &enc, &enc_len);
        ASSERT_TRUE(st == BASE85N_OK && enc_len == 5 + n &&
                     memcmp(enc + 5, buf, n) == 0,
                    "donor characters stand for themselves under alphabet 0");
        free(enc);
    }
}

/* Block mode's base-85 conversion (spec section 8), against the spec's own
 * five-divisions-by-85 formulation. base85n.c splits the value into a
 * two-digit head, a one-digit middle and a two-digit tail and reads the
 * digit pairs out of a table, so this walks values chosen to touch every
 * entry of that table in both the head and the tail position.
 *
 * A 4-byte input is always block mode -- a DP candidate needs at least
 * MIN_PASSTHROUGH_BYTES -- so the encoding of a value's 4 Big-Endian bytes
 * is exactly its 5 digits. */
static int block_digits_match(uint32_t v) {
    uint8_t bytes[4] = {
        (uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v
    };
    char expected[5];
    test_value_to_5chars(v, expected);

    char *enc = NULL;
    size_t enc_len = 0;
    base85n_status st = base85n_encode(bytes, 4, &enc, &enc_len);
    int ok = (st == BASE85N_OK && enc_len == 5 && memcmp(enc, expected, 5) == 0);
    free(enc);
    return ok;
}

static void test_block_mode_digits(void) {
    /* Every entry of the digit-pair table, in both of the positions it is
     * read from. head = tail = k covers both at once while such a head is
     * reachable; beyond that no 32-bit value has that head, and k is
     * covered as a tail. */
    int pairs_ok = 1;
    for (uint32_t k = 0; k <= 7224 && pairs_ok; k++) {
        pairs_ok = block_digits_match(k <= 6993 ? k * 614125u + k : k);
    }
    ASSERT_TRUE(pairs_ok, "block mode reproduces the spec's Base85 conversion "
                          "over the whole digit-pair table");

    /* All 85 values of the middle digit, which the sweep above leaves at 0. */
    int mids_ok = 1;
    for (uint32_t mid = 0; mid < 85 && mids_ok; mid++) {
        mids_ok = block_digits_match(mid * 7225u);
    }
    ASSERT_TRUE(mids_ok,
                "block mode reproduces the middle digit for all 85 values");

    /* Boundaries the sweeps step over, including the top of the range. */
    const uint32_t edges[] = {0u, 1u, 84u, 85u, 7224u, 7225u, 614124u, 614125u,
                              52200624u, 52200625u, 0x7FFFFFFFu, 0x80000000u,
                              0xFFFFFFFEu, 0xFFFFFFFFu};
    for (size_t i = 0; i < sizeof edges / sizeof edges[0]; i++) {
        char msg[128];
        snprintf(msg, sizeof msg, "block mode digits for edge value 0x%08lX",
                  (unsigned long)edges[i]);
        ASSERT_TRUE(block_digits_match(edges[i]), msg);
    }
}

static void test_lookup_tables(void) {
    /* ALPHABET_VALUE, all 256 byte values. A 5-character group of a single
     * repeated character decodes iff that character is in Alphabet-N.
     *
     * Space, tab, CR and LF are the exception and are checked separately:
     * they are not in Alphabet-N, but Section 7.1 has the decoder strip
     * inter-token whitespace before parsing, so they are ignored rather
     * than rejected. Folding them into the membership probe would test the
     * whitespace rule, not the table. */
    int alphabet_ok = 1;
    int whitespace_ok = 1;
    for (int i = 0; i < 256; i++) {
        char group[5];
        for (int k = 0; k < 5; k++) group[k] = (char)i;

        uint8_t *out = NULL;
        size_t out_len = 0;
        base85n_status st = base85n_decode(group, 5, &out, &out_len);
        free(out);

        if (i == ' ' || i == '\t' || i == '\r' || i == '\n') {
            if (st != BASE85N_OK || out_len != 0) {
                char msg[128];
                snprintf(msg, sizeof msg,
                          "byte %d is inter-token whitespace and must decode to "
                          "nothing, not be rejected", i);
                ASSERT_TRUE(0, msg);
                whitespace_ok = 0;
            }
            continue;
        }

        int expected_in_alphabet = (i != 0 && strchr(TEST_ALPHABET, i) != NULL);
        int accepted = (st != BASE85N_ERR_INVALID_CHAR);
        if (accepted != expected_in_alphabet) {
            char msg[128];
            snprintf(msg, sizeof msg,
                      "ALPHABET_VALUE[%d]: alphabet membership disagrees with "
                      "ALPHABET_N_CHARS_STR", i);
            ASSERT_TRUE(0, msg);
            alphabet_ok = 0;
        }
    }
    ASSERT_TRUE(alphabet_ok, "ALPHABET_VALUE matches ALPHABET_N_CHARS_STR for all 256 bytes");
    ASSERT_TRUE(whitespace_ok, "space, tab, CR and LF are stripped as inter-token whitespace");

    /* RSET_INDEX and ENC_SUB, all 13 R-Set characters. A buffer of one
     * R-Set byte is comfortably DP-eligible, and the alphabet the encoder
     * picks is the smallest identifier that carries that byte -- so every
     * one of those bytes must appear in the output as that alphabet's
     * donor for it. */
    for (int j = 0; j < 13; j++) {
        uint8_t buf[32];
        memset(buf, TEST_RSET_ASCII[j], sizeof buf);

        /* Smallest identifier that substitutes R_Char[j] wins the tie: every
         * alphabet carrying it reaches the same distance on this input. */
        int want_a = -1;
        for (int a = 0; a < TEST_NUM_ALPHABETS && want_a < 0; a++) {
            if (ALPHABET_SUB_EXPECTED[a][j] != 0) want_a = a;
        }
        ASSERT_TRUE(want_a >= 0, "every R-Set character is carried by some alphabet");
        if (want_a < 0) continue;
        char want = ALPHABET_SUB_EXPECTED[want_a][j];

        char *enc = NULL;
        size_t enc_len = 0;
        base85n_status st = base85n_encode(buf, sizeof buf, &enc, &enc_len);
        char msg[160];
        snprintf(msg, sizeof msg, "encode of R-Set byte %d succeeds", j);
        ASSERT_TRUE(st == BASE85N_OK, msg);
        if (st != BASE85N_OK) continue;

        /* Skip the 5-character DP signal; the rest is the substituted run. */
        int all_replaced = (enc_len == 5 + sizeof buf);
        for (size_t k = 5; k < enc_len && all_replaced; k++) {
            if (enc[k] != want) all_replaced = 0;
        }
        snprintf(msg, sizeof msg,
                  "R-Set byte %d (0x%02x) is substituted by '%c' under alphabet %d",
                  j, TEST_RSET_ASCII[j], want, want_a);
        ASSERT_TRUE(all_replaced, msg);
        free(enc);
    }

    test_alphabet_selection();
    test_block_mode_digits();
}

/* ------------------------------------------------------------------ */
/* Output buffer growth                                                  */
/* ------------------------------------------------------------------ */

/* The encoder sizes its output buffer for block mode's exact 1.25
 * characters per byte plus a small constant, and never grows it: a DP
 * segment spends one character per byte plus a 5-character signal, which is
 * exactly 1.25 characters per byte at MIN_PASSTHROUGH_BYTES and less above
 * it, and DP is only chosen from that length up. Version 0.2.0's escape
 * pairs are what could exceed the budget, and they are gone.
 *
 * That makes the growth branch in base85n_encode a guard rather than a path
 * any input takes -- so what is worth checking here is the property it
 * guards: that no input, however adversarial, encodes to more than the
 * initial sizing allows. A buffer that grew wrongly would be a heap
 * overflow rather than a wrong answer, and the sanitiser build turns this
 * into a real check of that. */
static void test_output_size_stays_within_capacity(void) {
    /* Shapes chosen to sit at the mode boundary, where the ratio is worst:
     * DP-eligible runs of exactly MIN_PASSTHROUGH_BYTES separated by
     * unrepresentable bytes, so the encoder alternates modes constantly. */
    static const uint8_t unit[25] = {
        ' ', 'a', 'a', 'a', 'a', 'a', 'a', 'a',
        'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a',
        0x80, 0x81, 0x82, 0x83, 0x84
    };
    const size_t reps = 4000;
    const size_t n = reps * sizeof unit;

    uint8_t *data = (uint8_t *)malloc(n);
    ASSERT_TRUE(data != NULL, "allocation for the capacity input");
    if (!data) return;
    for (size_t i = 0; i < reps; i++) memcpy(data + i * sizeof unit, unit, sizeof unit);

    char *enc = NULL;
    size_t enc_len = 0;
    base85n_status st = base85n_encode(data, n, &enc, &enc_len);
    ASSERT_TRUE(st == BASE85N_OK, "mode-alternating input encodes");
    if (st == BASE85N_OK) {
        char msg[160];
        size_t budget = n + n / 4 + 16;
        snprintf(msg, sizeof msg,
                  "encoded %zu bytes to %zu characters, within the %zu the initial "
                  "sizing allows", n, enc_len, budget);
        ASSERT_TRUE(enc_len + 1 <= budget, msg);

        uint8_t *back = NULL;
        size_t back_len = 0;
        ASSERT_TRUE(base85n_decode(enc, enc_len, &back, &back_len) == BASE85N_OK &&
                     back_len == n && memcmp(back, data, n) == 0,
                    "mode-alternating input round-trips");
        free(back);
        free(enc);
    }
    free(data);
}

/* ------------------------------------------------------------------ */
/* Inter-token whitespace retry (spec section 7.1)                       */
/* ------------------------------------------------------------------ */

/* The decoder's fast path assumes a stream with no whitespace in it and
 * only strips on failure -- and it strips in place, over the output
 * buffer it already holds, so the retry allocates nothing. In-place means
 * the decoder's writer is chasing its own reader through the same array,
 * which is exactly the kind of thing that is correct until a boundary
 * case says otherwise. So this drives the retry over a stream that mixes
 * both modes, with whitespace inserted at every position in turn: under
 * the sanitizers the run also covers the aliasing.
 *
 * The single trailing space is called out separately because it is the
 * worst case and the reachable-from-untrusted one -- the whole stream
 * decodes before the last character rejects it. */
static void test_whitespace_retry(void) {
    /* 21 representable bytes (DP, with an R-Set byte and escapes in it)
     * followed by 4 unrepresentable ones (block mode), so the stream a
     * retry has to re-walk contains signals, escape pairs, substituted
     * R-Set characters and plain groups alike. */
    static const uint8_t unit[25] = {
        ' ', ':', 'a', ':', 'a', ':', 'a', ':',
        'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a',
        0x80, 0x81, 0x82, 0x83
    };
    const size_t reps = 40;
    const size_t n = reps * sizeof unit;

    uint8_t *data = (uint8_t *)malloc(n);
    ASSERT_TRUE(data != NULL, "allocation for the whitespace-retry input");
    if (!data) return;
    for (size_t i = 0; i < reps; i++) memcpy(data + i * sizeof unit, unit, sizeof unit);

    char *enc = NULL;
    size_t enc_len = 0;
    ASSERT_TRUE(base85n_encode(data, n, &enc, &enc_len) == BASE85N_OK,
                "encode of the whitespace-retry input");
    if (!enc) { free(data); return; }

    static const char WS[4] = {' ', '\t', '\n', '\r'};
    char *spaced = (char *)malloc(enc_len + 1);
    ASSERT_TRUE(spaced != NULL, "allocation for the spaced copy");
    if (!spaced) { free(enc); free(data); return; }

    int all_ok = 1;
    for (size_t at = 0; at <= enc_len && all_ok; at++) {
        char ws = WS[at % 4];
        memcpy(spaced, enc, at);
        spaced[at] = ws;
        memcpy(spaced + at + 1, enc + at, enc_len - at);

        uint8_t *dec = NULL;
        size_t dec_len = 0;
        base85n_status st = base85n_decode(spaced, enc_len + 1, &dec, &dec_len);
        if (st != BASE85N_OK || dec_len != n || memcmp(dec, data, n) != 0) {
            char msg[160];
            snprintf(msg, sizeof msg,
                      "whitespace 0x%02x at offset %lu of %lu survives the "
                      "in-place retry", (unsigned)(unsigned char)ws,
                      (unsigned long)at, (unsigned long)enc_len);
            ASSERT_TRUE(0, msg);
            all_ok = 0;
        }
        free(dec);
    }
    ASSERT_TRUE(all_ok,
                "whitespace at every position of a mixed-mode stream is "
                "stripped and the stream decoded in place");

    /* The worst case on its own: everything decodes, then the last
     * character forces the whole scan to be redone. */
    memcpy(spaced, enc, enc_len);
    spaced[enc_len] = ' ';
    uint8_t *dec = NULL;
    size_t dec_len = 0;
    ASSERT_TRUE(base85n_decode(spaced, enc_len + 1, &dec, &dec_len) == BASE85N_OK &&
                    dec_len == n && memcmp(dec, data, n) == 0,
                "a single trailing space still round trips");
    free(dec);

    free(spaced);
    free(enc);
    free(data);
}

/* ------------------------------------------------------------------ */
/* Encoding complexity (spec section 6.6)                                */
/* ------------------------------------------------------------------ */

/* Step 1 scans up to MAX_DP_ANALYSIS_BYTES bytes for each of the eight
 * alphabets, while step 2.b may consume as few as 4 bytes, so an encoder
 * that redoes those scans every iteration performs 2048 byte inspections
 * per input byte. Bounded lookahead keeps that linear rather than quadratic
 * -- unlike version 0.2.0 -- but a constant factor of 2048 is still what
 * section 6.6 exists to prevent.
 *
 * Pseudorandom bytes are the worst case: no alphabet reaches
 * MIN_PASSTHROUGH_BYTES, so every iteration takes the block-mode branch and
 * advances 4 bytes, while a naive implementation rescans the full window
 * each time.
 *
 * Both checks below are timing-based, which on a shared CI runner means
 * they have to tolerate interference. Two things make them stable: every
 * duration is the *minimum* of several runs, since scheduling noise only
 * ever adds time and never removes it, and the thresholds sit far from the
 * values a healthy encoder produces. */

#define SCAN_DENSE_SIZE (128 * 1024)
#define ENCODE_TIME_LIMIT_SEC 20.0

/* Sizes for the growth check, and how many times each is measured. */
#define GROWTH_SMALL_SIZE (32 * 1024)
#define GROWTH_LARGE_SIZE (64 * 1024)
#define GROWTH_REPEATS 5

/* Below this, a measurement is too short for its ratio to mean anything. */
#define MEASURABLE_SEC 0.001

/* Linear predicts ~2.0, quadratic ~4.0. Halfway between is the decision point. */
#define MAX_GROWTH 3.0

static double encode_seconds(size_t n) {
    uint8_t *data = (uint8_t *)malloc(n);
    ASSERT_TRUE(data != NULL, "allocation for scan-dense input");
    if (!data) return 0.0;
    /* An xorshift fill rather than rand(), so the shape does not depend on
     * whichever seed an earlier test left behind. */
    uint32_t state = 0x5CA4DE45u ^ (uint32_t)n;
    for (size_t i = 0; i < n; i++) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        data[i] = (uint8_t)(state >> 24);
    }

    char *encoded = NULL;
    size_t encoded_len = 0;
    clock_t start = clock();
    base85n_status st = base85n_encode(data, n, &encoded, &encoded_len);
    double elapsed = (double)(clock() - start) / (double)CLOCKS_PER_SEC;
    ASSERT_TRUE(st == BASE85N_OK, "scan-dense encode succeeds");

    if (st == BASE85N_OK) {
        uint8_t *decoded = NULL;
        size_t decoded_len = 0;
        ASSERT_TRUE(base85n_decode(encoded, encoded_len, &decoded, &decoded_len) == BASE85N_OK,
                    "scan-dense decode succeeds");
        ASSERT_TRUE(decoded_len == n && memcmp(decoded, data, n) == 0,
                    "scan-dense round trip");
        free(decoded);
        free(encoded);
    }
    free(data);
    return elapsed;
}

/* Fastest of `repeats` encodes of `n` scan-dense bytes. */
static double best_encode_seconds(size_t n, int repeats) {
    double best = -1.0;
    for (int i = 0; i < repeats; i++) {
        double t = encode_seconds(n);
        if (best < 0.0 || t < best) best = t;
    }
    return best;
}

static void test_encoding_complexity(void) {
    double elapsed = encode_seconds(SCAN_DENSE_SIZE);
    ASSERT_TRUE(elapsed < ENCODE_TIME_LIMIT_SEC,
                "scan-dense input encodes in linear time (spec 6.6); a long "
                "runtime here is the signature of the per-iteration rescan");

    encode_seconds(4096); /* warm up */
    double small = best_encode_seconds(GROWTH_SMALL_SIZE, GROWTH_REPEATS);
    double large = best_encode_seconds(GROWTH_LARGE_SIZE, GROWTH_REPEATS);

    if (small > MEASURABLE_SEC) {
        ASSERT_TRUE(large < small * MAX_GROWTH,
                    "doubling scan-dense input roughly doubles encoding time");
    }
    printf("[complexity] 128 KiB of scan-dense input encoded in %.3f s\n", elapsed);
}

/* ------------------------------------------------------------------ */
/* main                                                                  */
/* ------------------------------------------------------------------ */

int main(void) {
    test_golden_vectors();
    test_random_roundtrips();
    test_edge_cases();
    test_decode_errors();
    test_adversarial_vectors();
    test_lookup_tables();
    test_output_size_stays_within_capacity();
    test_whitespace_retry();
    test_encoding_complexity();

    printf("\n%ld tests run, %ld failed.\n", g_tests_run, g_tests_failed);
    if (g_tests_failed > 0) {
        printf("RESULT: FAIL\n");
        return 1;
    }
    printf("RESULT: PASS\n");
    return 0;
}
