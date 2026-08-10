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
/* main                                                                  */
/* ------------------------------------------------------------------ */

int main(void) {
    test_golden_vectors();
    test_random_roundtrips();
    test_edge_cases();
    test_decode_errors();
    test_adversarial_vectors();

    printf("\n%ld tests run, %ld failed.\n", g_tests_run, g_tests_failed);
    if (g_tests_failed > 0) {
        printf("RESULT: FAIL\n");
        return 1;
    }
    printf("RESULT: PASS\n");
    return 0;
}
