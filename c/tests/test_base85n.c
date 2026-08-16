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

/* The eight donor profiles (spec 4.2). This is an independent copy of the
 * table in src/base85n.c; the tests below drive every entry through the
 * public API, so a typo in either cannot survive `make test`. */
#define TEST_NUM_PROFILES BASE85N_NUM_PROFILES
static const char TEST_PROFILES[TEST_NUM_PROFILES][14] = {
    "~^?%@+`$#!*.-",
    "~^+[]`?@!%#*(",
    "^~$#?%!`@[]+_",
    "~+?%@!^[]:`()",
    "~%^`+?!$@(){}",
    "^~?@!+%*$()_#",
    "^~@%?$+!#[]=*",
    "^$~@?!%`[]:}{",
};

/* Spec 4.3: donor character for R-Set index j under this profile and mask,
 * or 0 if the mask does not name j. The set bits consume the profile's
 * first k characters, the lowest bit taking rank 0. */
static char test_donor_for(unsigned profile, uint16_t mask, int j) {
    unsigned rank = 0;
    for (int b = 0; b < 13; b++) {
        if (!(mask & (uint16_t)(1u << b))) continue;
        if (b == j) return TEST_PROFILES[profile][rank];
        rank++;
    }
    return 0;
}

/* Local re-implementation of the spec's 5-digit Base85 encoder, used
 * only by the malformed-input tests below to hand-construct signals
 * (the public API intentionally does not expose this). */
static void test_value_to_5chars(uint64_t value, char out[5]) {
    uint8_t digits[5];
    for (int i = 4; i >= 0; i--) {
        digits[i] = (uint8_t)(value % 85);
        value /= 85;
    }
    for (int i = 0; i < 5; i++) out[i] = TEST_ALPHABET[digits[i]];
}

/* Section 9's three signal ranges, as the tests build them. */
#define TEST_DP_BASE ((uint64_t)1 << 32)
#define TEST_FILL_BASE (TEST_DP_BASE + ((uint64_t)1 << 27))
#define TEST_TAIL_BASE (TEST_FILL_BASE + ((uint64_t)1 << 19))
#define TEST_FUTURE_BASE (TEST_TAIL_BASE + ((uint64_t)1 << 22))

static void test_dp_signal(unsigned profile, uint16_t mask, size_t len, char out[5]) {
    test_value_to_5chars(TEST_DP_BASE + ((uint64_t)profile << 24) +
                             ((uint64_t)mask << 11) + (uint64_t)(len - 1),
                         out);
}

static void test_fill_signal(uint8_t byte, size_t len, char out[5]) {
    test_value_to_5chars(TEST_FILL_BASE + ((uint64_t)byte << 11) + (uint64_t)(len - 1),
                         out);
}

/* The value of a 5-character group, for reading back a signal the encoder
 * emitted. */
static uint64_t test_group_value(const char *chars) {
    uint64_t v = 0;
    for (int i = 0; i < 5; i++) {
        const char *at = strchr(TEST_ALPHABET, chars[i]);
        v = v * 85 + (uint64_t)(at - TEST_ALPHABET);
    }
    return v;
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
     * prefix is capped at MAX_DP_ANALYSIS_BYTES == 2048). Pure alphabet
     * literals, so one output character per input byte. */
    {
        size_t len = (size_t)BASE85N_MAX_DP_SEGMENT_CHARS * 2 + 37;
        uint8_t *data = (uint8_t *)malloc(len);
        ASSERT_TRUE(data != NULL, "multi-DP-segment buffer allocation");
        if (data) {
            for (size_t i = 0; i < len; i++) data[i] = (uint8_t)TEST_ALPHABET[i % 85];
            roundtrip_check(data, len, "multi_dp_segment");
            free(data);
        }
    }

    /* The MAX_DP_ANALYSIS_BYTES boundary: exactly 2048 representable bytes
     * are one segment, and one more needs a second. The bytes have to vary:
     * a run of identical ones is a Fill segment long before the window
     * fills up. */
    {
        size_t w = BASE85N_MAX_DP_ANALYSIS_BYTES;
        uint8_t *data = (uint8_t *)malloc(w + 1);
        ASSERT_TRUE(data != NULL, "analysis-window buffer allocation");
        if (data) {
            for (size_t i = 0; i <= w; i++) data[i] = (uint8_t)('a' + i % 26);
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

    /* A literal donor character is representable while the segment does not
     * spend it. With a space in the run every profile pays for the space
     * with its rank-0 donor, so the scan either moves to a profile that
     * ranks the literal beyond k, or breaks the segment. */
    {
        for (int a = 0; a < TEST_NUM_PROFILES; a++) {
            for (int r = 0; r < 13; r++) {
                char donor = TEST_PROFILES[a][r];
                uint8_t data[60];
                size_t idx = 0;
                for (int i = 0; i < 25; i++) data[idx++] = (uint8_t)('a' + i % 26);
                data[idx++] = (uint8_t)' ';
                data[idx++] = (uint8_t)donor;
                data[idx++] = (uint8_t)' ';
                for (int i = 0; i < 25; i++) data[idx++] = (uint8_t)('A' + i % 26);
                roundtrip_check(data, idx, "literal_donor_in_run");
            }
        }
    }

    /* Every R-Set character has to survive a segment that names it. */
    {
        for (int j = 0; j < 13; j++) {
            uint8_t data[256];
            size_t idx = 0;
            while (idx + 5 <= sizeof data) {
                memcpy(data + idx, "word", 4);
                idx += 4;
                data[idx++] = TEST_RSET_ASCII[j];
            }
            roundtrip_check(data, idx, "segment_carries_its_rset_char");
        }
    }

    /* Solid Fill: one below the threshold is block mode; at it and at the
     * cap, one signal carries the whole run; one past it needs a second. */
    {
        static const uint8_t bytes[] = {0x00, 0x20, 0x61, 0xFF};
        size_t cap = BASE85N_MAX_FILL_BYTES;
        uint8_t *data = (uint8_t *)malloc(cap + 1);
        ASSERT_TRUE(data != NULL, "fill buffer allocation");
        if (data) {
            for (size_t b = 0; b < sizeof bytes; b++) {
                memset(data, bytes[b], cap + 1);
                char *enc = NULL;
                size_t enc_len = 0;
                for (size_t n = BASE85N_MIN_FILL_BYTES; n <= cap; n = n == cap ? cap + 1 : (n + 1 == BASE85N_MIN_FILL_BYTES + 2 ? cap : n + 1)) {
                    if (base85n_encode(data, n, &enc, &enc_len) == BASE85N_OK) {
                        ASSERT_TRUE(enc_len == 5, "a run is one Fill signal");
                        free(enc);
                    }
                    roundtrip_check(data, n, "fill_run");
                    if (n == cap + 1) break;
                }
                if (base85n_encode(data, cap + 1, &enc, &enc_len) == BASE85N_OK) {
                    ASSERT_TRUE(enc_len == 7,
                                "one byte past the cap needs a second signal");
                    free(enc);
                }
                roundtrip_check(data, cap + 1, "fill_run_over_cap");
                roundtrip_check(data, BASE85N_MIN_FILL_BYTES - 1, "fill_run_below_threshold");
            }
            free(data);
        }
    }

    /* A long run inside passthrough text breaks the segment around it. */
    {
        uint8_t data[380];
        size_t idx = 0;
        for (int i = 0; i < 40; i++) data[idx++] = (uint8_t)('a' + i % 26);
        for (int i = 0; i < 300; i++) data[idx++] = (uint8_t)'=';
        for (int i = 0; i < 40; i++) data[idx++] = (uint8_t)('a' + i % 26);
        char *enc = NULL;
        size_t enc_len = 0;
        if (base85n_encode(data, idx, &enc, &enc_len) == BASE85N_OK) {
            ASSERT_TRUE(enc_len == 5 + 40 + 5 + 5 + 40,
                        "a run inside text costs one Fill signal and one restart");
            free(enc);
        }
        roundtrip_check(data, idx, "fill_inside_text");
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
        test_dp_signal(0, 0, 5, sig);
        char buf[16];
        size_t n = 0;
        memcpy(buf + n, sig, 5); n += 5;
        memcpy(buf + n, "ab&de", 5); n += 5; /* '&' invalid at index 2 */
        expect_decode_error(buf, n, BASE85N_ERR_INVALID_CHAR, "invalid_char_in_dp_data");
    }

    /* DP signal whose declared length overruns available input. */
    {
        char sig[5];
        test_dp_signal(0, 0, 50, sig);
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
        test_dp_signal(0, 0, 1, sig);
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

    /* A group value in FUTURE_SIGNAL_SPACE: the first one, and the last. */
    {
        char sig[5];
        test_value_to_5chars(TEST_FUTURE_BASE, sig);
        expect_decode_error(sig, 5, BASE85N_ERR_UNDEFINED_SIGNAL, "future_signal_first");
        const char *max_group = "#####"; /* 85^5 - 1, the top of the space */
        expect_decode_error(max_group, strlen(max_group), BASE85N_ERR_UNDEFINED_SIGNAL,
                            "future_signal_max_group");
    }

    /* The two ends of the Fill range, which read no characters at all. */
    {
        char sig[5];
        test_value_to_5chars(TEST_FILL_BASE, sig);
        uint8_t *out = NULL;
        size_t out_len = 0;
        base85n_status st = base85n_decode(sig, 5, &out, &out_len);
        ASSERT_TRUE(st == BASE85N_OK && out_len == 1 && out && out[0] == 0,
                    "the first Fill signal is one zero byte");
        free(out);

        test_fill_signal(0x5A, BASE85N_MAX_FILL_BYTES, sig);
        out = NULL;
        st = base85n_decode(sig, 5, &out, &out_len);
        ASSERT_TRUE(st == BASE85N_OK && out_len == BASE85N_MAX_FILL_BYTES,
                    "five characters expand to at most MAX_FILL_BYTES bytes");
        if (st == BASE85N_OK) {
            int all = 1;
            for (size_t i = 0; i < out_len; i++) if (out[i] != 0x5A) all = 0;
            ASSERT_TRUE(all, "a Fill signal repeats its byte");
            free(out);
        }

        /* The two ends of the tail variant, which read no characters either.
         * The first names one zero and two NUL literals; the last names two
         * 0xFF literals ahead of MAX_TAIL_ZEROS zeros. */
        test_value_to_5chars(TEST_TAIL_BASE, sig);
        out = NULL;
        st = base85n_decode(sig, 5, &out, &out_len);
        ASSERT_TRUE(st == BASE85N_OK && out_len == 3 && out &&
                        out[0] == 0 && out[1] == 0 && out[2] == 0,
                    "the first tail signal names one zero and two NUL literals");
        free(out);

        test_value_to_5chars(TEST_FUTURE_BASE - 1, sig);
        out = NULL;
        st = base85n_decode(sig, 5, &out, &out_len);
        ASSERT_TRUE(st == BASE85N_OK && out_len == BASE85N_MAX_TAIL_ZEROS + 2 &&
                        out && out[0] == 0xFF && out[1] == 0xFF && out[2] == 0,
                    "the last tail signal names two literals then 32 zeros");
        free(out);
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
                expect_decode_error(buf, enc_len + 1, BASE85N_ERR_INVALID_FINAL_BLOCK,
                                     "lone_trailing_char");
                free(buf);
            }
            free(enc);
        }
    }

    /* Also a bare single character with nothing else in the stream. */
    {
        const char *s = "a";
        expect_decode_error(s, strlen(s), BASE85N_ERR_INVALID_FINAL_BLOCK, "bare_single_char");
    }

    /* Section 7.5: a trailing group is padded with '#' and the result must
     * be below 2^32, and it must be the canonical encoding of the bytes it
     * yields. "%nSb" pads to 2^32 - 2 and would yield ff ff ff, but those
     * bytes encode as something else, so it is an alias and is rejected;
     * "%nSc" is the very next group and pads past 2^32 outright. */
    {
        static const uint8_t three_ff[3] = {0xFF, 0xFF, 0xFF};
        char *enc = NULL;
        size_t enc_len = 0;
        base85n_status st = base85n_encode(three_ff, 3, &enc, &enc_len);
        ASSERT_TRUE(st == BASE85N_OK && enc_len == 4, "setup: ff ff ff encodes");
        if (st == BASE85N_OK) {
            uint8_t *out = NULL;
            size_t out_len = 0;
            base85n_status dst = base85n_decode(enc, enc_len, &out, &out_len);
            ASSERT_TRUE(dst == BASE85N_OK && out_len == 3 &&
                            memcmp(out, three_ff, 3) == 0,
                        "the canonical final block for ff ff ff decodes");
            if (dst == BASE85N_OK) free(out);
            ASSERT_TRUE(memcmp(enc, "%nSb", 4) != 0,
                        "%nSb is an alias, not the canonical form");
            free(enc);
        }

        expect_decode_error("%nSb", 4, BASE85N_ERR_INVALID_FINAL_BLOCK,
                            "final_block_alias");
        expect_decode_error("%nSc", 4, BASE85N_ERR_INVALID_FINAL_BLOCK,
                            "final_block_over_limit");
        expect_decode_error("###", 3, BASE85N_ERR_INVALID_FINAL_BLOCK,
                            "final_block_three_chars_over_limit");
        expect_decode_error("##", 2, BASE85N_ERR_INVALID_FINAL_BLOCK,
                            "final_block_two_chars_over_limit");
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
        if (strcmp(code, "undefined_signal") == 0) return BASE85N_ERR_UNDEFINED_SIGNAL;
    if (strcmp(code, "invalid_final_block") == 0) return BASE85N_ERR_INVALID_FINAL_BLOCK;
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
/* Profile selection (spec 6.2 and 6.5) and the substitution it implies.
 *
 * A profile table that swapped two donors would still round-trip, because
 * the decoder would mirror the mistake, but it would emit a stream no other
 * implementation agrees with. So each probe drives one profile through a
 * DP-eligible run and checks the exact characters that come back, against
 * the substitution derived here from this file's own copy of the table.
 *
 * A profile is only chosen once every lower-numbered one has been ruled out
 * by a literal it would have spent, so each probe is built to do exactly
 * that: k R-Set characters, plus one literal per lower profile that the
 * lower profile ranks below k and this one does not. */
static void test_profile_selection(void) {
    for (unsigned p = 0; p < TEST_NUM_PROFILES; p++) {
        int built = 0;

        for (int k = 1; k <= 13 && !built; k++) {
            char literals[TEST_NUM_PROFILES];
            int nlit = 0;
            int ok = 1;

            for (unsigned q = 0; q < p && ok; q++) {
                char pick = 0;
                for (int r = 0; r < k && pick == 0; r++) {
                    char c = TEST_PROFILES[q][r];
                    int spent_by_p = 0;
                    for (int t = 0; t < k; t++) {
                        if (TEST_PROFILES[p][t] == c) spent_by_p = 1;
                    }
                    if (!spent_by_p) pick = c;
                }
                if (pick == 0) ok = 0; else literals[nlit++] = pick;
            }
            if (!ok) continue;

            uint8_t buf[512];
            size_t n = 0;
            while (n + (size_t)k * 5 + (size_t)nlit + 8 < sizeof buf && n < 120) {
                for (int j = 0; j < k; j++) {
                    memcpy(buf + n, "word", 4);
                    n += 4;
                    buf[n++] = TEST_RSET_ASCII[j];
                }
                for (int l = 0; l < nlit; l++) buf[n++] = (uint8_t)literals[l];
            }
            if (n < BASE85N_MIN_PASSTHROUGH_BYTES) continue;

            char *enc = NULL;
            size_t enc_len = 0;
            base85n_status st = base85n_encode(buf, n, &enc, &enc_len);
            char msg[192];
            snprintf(msg, sizeof msg, "profile %u probe encodes", p);
            ASSERT_TRUE(st == BASE85N_OK, msg);
            if (st != BASE85N_OK) continue;
            built = 1;

            ASSERT_TRUE(enc_len == 5 + n, "the probe is one DP segment");
            if (enc_len != 5 + n) { free(enc); continue; }

            uint64_t value = test_group_value(enc);
            ASSERT_TRUE(value >= TEST_DP_BASE && value < TEST_FILL_BASE,
                        "the probe's first group is a DP signal");
            uint64_t payload = value - TEST_DP_BASE;
            unsigned got_profile = (unsigned)((payload >> 24) & 0x7u);
            uint16_t got_mask = (uint16_t)((payload >> 11) & 0x1FFFu);
            size_t got_len = (size_t)(payload & 0x7FFu) + 1;

            snprintf(msg, sizeof msg,
                     "the probe for profile %u selects profile %u", p, got_profile);
            ASSERT_TRUE(got_profile == p, msg);
            ASSERT_TRUE(got_mask == (uint16_t)((1u << k) - 1u),
                        "the mask names exactly the R-Set characters present");
            ASSERT_TRUE(got_len == n, "the signal's length is the segment's");

            int all_match = 1;
            for (size_t i = 0; i < n && all_match; i++) {
                int j = -1;
                for (int r = 0; r < 13; r++) {
                    if (TEST_RSET_ASCII[r] == buf[i]) { j = r; break; }
                }
                char want = j < 0 ? (char)buf[i] : test_donor_for(got_profile, got_mask, j);
                if (enc[5 + i] != want) all_match = 0;
            }
            snprintf(msg, sizeof msg,
                     "profile %u writes every R-Set character as its own donor", p);
            ASSERT_TRUE(all_match, msg);
            free(enc);
        }

        char msg[96];
        snprintf(msg, sizeof msg, "a probe exists that selects profile %u", p);
        ASSERT_TRUE(built, msg);
    }

    /* With an empty mask nothing is substituted, so a run of Alphabet-N
     * characters that includes donors comes back unchanged -- and spec 6.5
     * rule 5 requires profile 0 to be the one named. */
    {
        uint8_t buf[64];
        size_t n = 0;
        for (size_t i = 0; i < TEST_DONOR_COUNT; i++) buf[n++] = (uint8_t)TEST_DONORS[i];
        while (n < 40) { buf[n] = (uint8_t)('a' + n % 26); n++; }

        char *enc = NULL;
        size_t enc_len = 0;
        base85n_status st = base85n_encode(buf, n, &enc, &enc_len);
        ASSERT_TRUE(st == BASE85N_OK && enc_len == 5 + n &&
                     memcmp(enc + 5, buf, n) == 0,
                    "donor characters stand for themselves when the mask is empty");
        if (st == BASE85N_OK) {
            uint64_t payload = test_group_value(enc) - TEST_DP_BASE;
            ASSERT_TRUE(((payload >> 11) & 0x1FFFu) == 0 && ((payload >> 24) & 0x7u) == 0,
                        "an empty mask is emitted with profile 0");
            free(enc);
        }
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

    /* RSET_INDEX and the substitution derivation, all 13 R-Set characters.
     * A run of "word" plus one R-Set byte is comfortably DP-eligible, and
     * the mask names exactly that one character, so it must come back as
     * the rank-0 donor of whichever profile the signal names. (A buffer of
     * the R-Set byte alone would be a Fill segment, not a DP one.) */
    for (int j = 0; j < 13; j++) {
        uint8_t buf[40];
        size_t n = 0;
        while (n + 5 <= sizeof buf) {
            memcpy(buf + n, "word", 4);
            n += 4;
            buf[n++] = TEST_RSET_ASCII[j];
        }

        char *enc = NULL;
        size_t enc_len = 0;
        base85n_status st = base85n_encode(buf, n, &enc, &enc_len);
        char msg[160];
        snprintf(msg, sizeof msg, "encode of R-Set byte %d succeeds", j);
        ASSERT_TRUE(st == BASE85N_OK, msg);
        if (st != BASE85N_OK) continue;

        ASSERT_TRUE(enc_len == 5 + n, "the probe is one DP segment");
        uint64_t payload = test_group_value(enc) - TEST_DP_BASE;
        unsigned profile = (unsigned)((payload >> 24) & 0x7u);
        uint16_t mask = (uint16_t)((payload >> 11) & 0x1FFFu);
        snprintf(msg, sizeof msg, "the mask names R-Set character %d and no other", j);
        ASSERT_TRUE(mask == (uint16_t)(1u << j), msg);

        char want = test_donor_for(profile, mask, j);
        int all_replaced = 1;
        for (size_t k = 0; k < n && all_replaced; k++) {
            char expected = buf[k] == TEST_RSET_ASCII[j] ? want : (char)buf[k];
            if (enc[5 + k] != expected) all_replaced = 0;
        }
        snprintf(msg, sizeof msg,
                  "R-Set byte %d (0x%02x) is substituted by '%c' under profile %u",
                  j, TEST_RSET_ASCII[j], want, profile);
        ASSERT_TRUE(all_replaced, msg);
        free(enc);
    }

    test_profile_selection();
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
