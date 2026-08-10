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

/* allowedPassthroughSafeReplacementCharacters[j] (spec 4.2), indexed by the
 * same j as TEST_RSET_ASCII. */
static const char REPLACEMENT_CHARS_EXPECTED[13] = {
    ':', '+', '=', '^', '!', '/', '*', '?', '`', '(', ')', '[', ']'
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

static uint8_t gen_rset_or_escape(void) {
    int r = rand() % 100;
    if (r < 60) return TEST_RSET_ASCII[rand() % 13];
    return (uint8_t)'~';
}

static uint8_t gen_mixed(void) {
    int r = rand() % 100;
    if (r < 25) return gen_pure_random();
    if (r < 60) return gen_alphabet_literal();
    if (r < 90) return TEST_RSET_ASCII[rand() % 13];
    return (uint8_t)'~';
}

typedef uint8_t (*byte_gen_fn)(void);

static void test_random_roundtrips(void) {
    srand(12345);

    struct { const char *name; byte_gen_fn gen; } generators[] = {
        {"pure_random", gen_pure_random},
        {"alphabet_literal", gen_alphabet_literal},
        {"rset_escape_heavy", gen_rset_or_escape},
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

    /* Segment long enough to require multiple DP signal segments
     * (transformed length > MAX_DP_OUTPUT_CHARS_PER_SIGNAL == 511). Pure
     * alphabet literals so transformed length == byte length exactly. */
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

    /* A run of the escape character long enough to trigger the
     * MAX_CONSECUTIVE_ESCAPES(=3) scan-termination heuristic mid-scan. */
    {
        uint8_t data[80];
        size_t idx = 0;
        for (int i = 0; i < 30; i++) data[idx++] = (uint8_t)('a' + (i % 26));
        for (int i = 0; i < 10; i++) data[idx++] = (uint8_t)'~'; /* > 3 consecutive escapes */
        for (int i = 0; i < 30; i++) data[idx++] = (uint8_t)('A' + (i % 26));
        roundtrip_check(data, idx, "consecutive_escape_break");
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
        test_value_to_5chars(((uint64_t)1 << 32) | 5 /* mask=0, len=5 */, sig);
        char buf[16];
        size_t n = 0;
        memcpy(buf + n, sig, 5); n += 5;
        memcpy(buf + n, "ab&de", 5); n += 5; /* '&' invalid at index 2 */
        expect_decode_error(buf, n, BASE85N_ERR_INVALID_CHAR, "invalid_char_in_dp_data");
    }

    /* DP signal whose declared length overruns available input. */
    {
        char sig[5];
        test_value_to_5chars(((uint64_t)1 << 32) | 50 /* mask=0, len=50 */, sig);
        char buf[32];
        size_t n = 0;
        memcpy(buf + n, sig, 5); n += 5;
        const char *data10 = "abcdefghij"; /* only 10 chars, but 50 declared */
        memcpy(buf + n, data10, 10); n += 10;
        expect_decode_error(buf, n, BASE85N_ERR_UNEXPECTED_EOF, "dp_length_overrun");
    }

    /* Trailing lone '~' at the end of a DP segment (dangling escape). */
    {
        char sig[5];
        test_value_to_5chars(((uint64_t)1 << 32) | 4 /* mask=0, len=4 */, sig);
        char buf[16];
        size_t n = 0;
        memcpy(buf + n, sig, 5); n += 5;
        memcpy(buf + n, "abc~", 4); n += 4; /* ends in unescaped '~' */
        expect_decode_error(buf, n, BASE85N_ERR_DANGLING_ESCAPE, "dangling_escape");
    }

    /* Signal payload in the reserved 22-bit range above 2^22 - 1. */
    {
        char sig[5];
        uint64_t payload = ((uint64_t)1 << 22); /* one above max (2^22 - 1) */
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
    if (strcmp(code, "dangling_escape_character") == 0) return BASE85N_ERR_DANGLING_ESCAPE;
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
 * inside it must not be. R-Set membership and the replacement mapping are
 * observable through a DP-mode encode: a run of an R-Set byte comes back as
 * a run of its replacement character. The escape triggers are observable
 * the same way, by putting a replacement character in a window that does
 * and does not contain its R-Set partner. The base-85 digit-pair table is
 * observable through block mode, against the spec's own conversion. */
/* The escape triggers (spec 6.1, Case ii). A replacement character has to
 * be escaped exactly while its R-Set partner is in the same window, and
 * '~' has to be escaped always -- a table that mixed two triggers up would
 * still round-trip, because the decoder would mirror the mistake, but it
 * would emit a stream no other implementation agrees with.
 *
 * Each probe is a DP-eligible run: one interesting byte followed by enough
 * plain Alphabet-N filler that Dynamic Passthrough beats block mode. The
 * expected output is then the 5-character signal followed by exactly the
 * characters spelled out below. */
static void test_escape_triggers(void) {
    for (int j = 0; j < 13; j++) {
        uint8_t buf[32];
        char expected[64];
        char msg[160];
        size_t n, e;

        /* Partner present: the R-Set byte puts bit j in the window mask, so
         * the replacement character right after it must be escaped. */
        n = 0;
        buf[n++] = TEST_RSET_ASCII[j];
        buf[n++] = (uint8_t)REPLACEMENT_CHARS_EXPECTED[j];
        while (n < 32) buf[n++] = 'a';
        e = 0;
        expected[e++] = REPLACEMENT_CHARS_EXPECTED[j]; /* the R-Set byte */
        expected[e++] = '~';                           /* the escape ... */
        expected[e++] = REPLACEMENT_CHARS_EXPECTED[j]; /* ... and its subject */
        while (e < 33) expected[e++] = 'a';            /* 30 filler bytes */

        char *enc = NULL;
        size_t enc_len = 0;
        base85n_status st = base85n_encode(buf, n, &enc, &enc_len);
        snprintf(msg, sizeof msg,
                  "'%c' is escaped while R-Set byte %d is in the window",
                  REPLACEMENT_CHARS_EXPECTED[j], j);
        ASSERT_TRUE(st == BASE85N_OK && enc_len == 5 + e &&
                     memcmp(enc + 5, expected, e) == 0, msg);
        free(enc);

        /* Partner absent: the same character stands for itself, unescaped. */
        n = 0;
        buf[n++] = (uint8_t)REPLACEMENT_CHARS_EXPECTED[j];
        while (n < 31) buf[n++] = 'a';
        e = 0;
        expected[e++] = REPLACEMENT_CHARS_EXPECTED[j];
        while (e < 31) expected[e++] = 'a';

        enc = NULL;
        enc_len = 0;
        st = base85n_encode(buf, n, &enc, &enc_len);
        snprintf(msg, sizeof msg,
                  "'%c' is not escaped while R-Set byte %d is absent",
                  REPLACEMENT_CHARS_EXPECTED[j], j);
        ASSERT_TRUE(st == BASE85N_OK && enc_len == 5 + e &&
                     memcmp(enc + 5, expected, e) == 0, msg);
        free(enc);
    }

    /* '~' carries no R-Set partner and is escaped unconditionally. */
    uint8_t buf[31];
    char expected[32];
    size_t n = 0, e = 0;
    buf[n++] = '~';
    while (n < 31) buf[n++] = 'a';
    expected[e++] = '~';
    expected[e++] = '~';
    while (e < 32) expected[e++] = 'a';

    char *enc = NULL;
    size_t enc_len = 0;
    base85n_status st = base85n_encode(buf, n, &enc, &enc_len);
    ASSERT_TRUE(st == BASE85N_OK && enc_len == 5 + e &&
                 memcmp(enc + 5, expected, e) == 0,
                "'~' is escaped in DP mode with no mask bit involved");
    free(enc);
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

    /* RSET_INDEX and REPLACEMENT_INDEX, all 13 pairs. MIN_PASSTHROUGH_BYTES
     * of one R-Set byte is comfortably DP-eligible, and every one of those
     * bytes must appear in the output as its replacement character. */
    for (int j = 0; j < 13; j++) {
        uint8_t buf[32];
        memset(buf, TEST_RSET_ASCII[j], sizeof buf);

        char *enc = NULL;
        size_t enc_len = 0;
        base85n_status st = base85n_encode(buf, sizeof buf, &enc, &enc_len);
        char msg[160];
        snprintf(msg, sizeof msg, "encode of R-Set byte %d succeeds", j);
        ASSERT_TRUE(st == BASE85N_OK, msg);
        if (st != BASE85N_OK) continue;

        /* Skip the 5-character DP signal; the rest is the substituted run. */
        int all_replaced = (enc_len > 5);
        for (size_t k = 5; k < enc_len; k++) {
            if (enc[k] != REPLACEMENT_CHARS_EXPECTED[j]) { all_replaced = 0; break; }
        }
        snprintf(msg, sizeof msg,
                  "R-Set byte %d (0x%02x) is substituted by '%c' in DP mode",
                  j, TEST_RSET_ASCII[j], REPLACEMENT_CHARS_EXPECTED[j]);
        ASSERT_TRUE(all_replaced, msg);
        free(enc);
    }

    test_escape_triggers();
    test_block_mode_digits();
}

/* ------------------------------------------------------------------ */
/* Output buffer growth                                                  */
/* ------------------------------------------------------------------ */

/* The encoder sizes its output buffer for block mode's exact 1.25
 * characters per byte and grows it only when Dynamic Passthrough spends
 * more than that. DP is chosen only when it beats block mode, so reaching
 * the growth path at all takes an input built for it: 21 representable
 * bytes -- one R-Set byte to put a bit in the window mask, then four
 * replacement characters spread out so the run of three consecutive
 * escapes is never exceeded -- which DP encodes in 30 characters against
 * block mode's 30, followed by four unrepresentable bytes so the next run
 * starts 4-byte aligned. That runs at 1.40 characters per byte.
 *
 * Without this the growth branch is never taken by any other test here,
 * and a buffer that grows wrongly would be a heap overflow rather than a
 * wrong answer. */
static void test_output_buffer_growth(void) {
    static const uint8_t unit[25] = {
        ' ', ':', 'a', ':', 'a', ':', 'a', ':',
        'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a',
        0x80, 0x81, 0x82, 0x83
    };
    const size_t reps = 4000;
    const size_t n = reps * sizeof unit;

    uint8_t *data = (uint8_t *)malloc(n);
    ASSERT_TRUE(data != NULL, "allocation for the buffer-growth input");
    if (!data) return;
    for (size_t i = 0; i < reps; i++) memcpy(data + i * sizeof unit, unit, sizeof unit);

    char *enc = NULL;
    size_t enc_len = 0;
    ASSERT_TRUE(base85n_encode(data, n, &enc, &enc_len) == BASE85N_OK,
                "encode of a DP-heavy input that outgrows the initial buffer");

    /* If this ratio ever drops to 1.25 the input stopped exercising growth
     * and the test has quietly stopped testing anything. */
    ASSERT_TRUE(enc_len > n + n / 4,
                "the growth input really does exceed block mode's 1.25 "
                "characters per byte");

    uint8_t *dec = NULL;
    size_t dec_len = 0;
    ASSERT_TRUE(base85n_decode(enc, enc_len, &dec, &dec_len) == BASE85N_OK,
                "decode of the grown output");
    ASSERT_TRUE(dec_len == n && memcmp(dec, data, n) == 0,
                "round trip across an output buffer that had to grow");

    free(dec);
    free(enc);
    free(data);
}

/* ------------------------------------------------------------------ */
/* Encoding complexity (spec section 6.6)                                */
/* ------------------------------------------------------------------ */

/* Pass 1 scans to the end of a representable run while the main loop can
 * consume as little as 4 bytes of it, so an encoder that re-runs Pass 1 on
 * every iteration is O(n^2). A buffer of escape characters is the worst
 * case: Pass 2 gives up after 3 bytes every time.
 *
 * Both checks below are timing-based, which on a shared CI runner means
 * they have to tolerate interference. Two things make them stable: every
 * duration is the *minimum* of several runs, since scheduling noise only
 * ever adds time and never removes it, and the thresholds sit far from the
 * values a healthy encoder produces. A linear encoder handles the 128 KiB
 * case in milliseconds even under the sanitizers; the quadratic encoder
 * this test exists to catch needed about 25 seconds for it. */

#define ESCAPE_DENSE_SIZE (128 * 1024)
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
    ASSERT_TRUE(data != NULL, "allocation for escape-dense input");
    if (!data) return 0.0;
    memset(data, '~', n);

    char *encoded = NULL;
    size_t encoded_len = 0;
    clock_t start = clock();
    base85n_status st = base85n_encode(data, n, &encoded, &encoded_len);
    double elapsed = (double)(clock() - start) / (double)CLOCKS_PER_SEC;
    ASSERT_TRUE(st == BASE85N_OK, "escape-dense encode succeeds");

    if (st == BASE85N_OK) {
        uint8_t *decoded = NULL;
        size_t decoded_len = 0;
        ASSERT_TRUE(base85n_decode(encoded, encoded_len, &decoded, &decoded_len) == BASE85N_OK,
                    "escape-dense decode succeeds");
        ASSERT_TRUE(decoded_len == n && memcmp(decoded, data, n) == 0,
                    "escape-dense round trip");
        free(decoded);
        free(encoded);
    }
    free(data);
    return elapsed;
}

/* Fastest of `repeats` encodes of `n` escape characters. */
static double best_encode_seconds(size_t n, int repeats) {
    double best = -1.0;
    for (int i = 0; i < repeats; i++) {
        double t = encode_seconds(n);
        if (best < 0.0 || t < best) best = t;
    }
    return best;
}

static void test_encoding_complexity(void) {
    double elapsed = encode_seconds(ESCAPE_DENSE_SIZE);
    ASSERT_TRUE(elapsed < ENCODE_TIME_LIMIT_SEC,
                "escape-dense input encodes in linear time (spec 6.6); a long "
                "runtime here is the signature of the quadratic Pass 1 rescan");

    encode_seconds(4096); /* warm up */
    double small = best_encode_seconds(GROWTH_SMALL_SIZE, GROWTH_REPEATS);
    double large = best_encode_seconds(GROWTH_LARGE_SIZE, GROWTH_REPEATS);

    if (small > MEASURABLE_SEC) {
        ASSERT_TRUE(large < small * MAX_GROWTH,
                    "doubling escape-dense input roughly doubles encoding time");
    }
    printf("[complexity] 128 KiB of escapes encoded in %.3f s\n", elapsed);
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
    test_output_buffer_growth();
    test_encoding_complexity();

    printf("\n%ld tests run, %ld failed.\n", g_tests_run, g_tests_failed);
    if (g_tests_failed > 0) {
        printf("RESULT: FAIL\n");
        return 1;
    }
    printf("RESULT: PASS\n");
    return 0;
}
