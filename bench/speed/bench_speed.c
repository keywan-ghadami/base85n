/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/*
 * bench_speed.c - encode/decode throughput for Base85N and three
 * established encodings.
 *
 * What this measures, and what it does not
 * ----------------------------------------
 * The Base64, Ascii85 and Z85 codecs below are straightforward scalar
 * reference implementations in the usual table-driven style. They are
 * not SIMD, and a tuned production Base64 (which processes 32 bytes per
 * vector instruction) is several times faster than the one here. The
 * comparison is therefore between algorithms written at a comparable
 * level of effort, not between Base85N and the fastest Base64 in
 * existence. The number to read off is "what does Base85N's extra work
 * cost relative to a plain scalar codec", not "Base85N beats Base64".
 *
 * To keep the playing field level, every codec allocates its output
 * buffer with malloc() on every call, mirroring the base85n public API,
 * and every measurement is verified by a round trip before it is
 * reported.
 *
 * Z85 is only defined for inputs whose length is a multiple of four; for
 * those inputs it is measured over the largest such prefix, which is
 * noted in the output.
 */

/* clock_gettime/CLOCK_MONOTONIC are not exposed by a strict -std=c11 build. */
#define _POSIX_C_SOURCE 199309L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#include "base85n.h"

#define OK 0
#define FAIL 1

/* ------------------------------------------------------------------ */
/* Base64 (RFC 4648)                                                   */
/* ------------------------------------------------------------------ */

static const char B64_ALPHABET[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int8_t b64_rev[256];

static void b64_init(void)
{
    memset(b64_rev, -1, sizeof(b64_rev));
    for (int i = 0; i < 64; i++)
        b64_rev[(unsigned char)B64_ALPHABET[i]] = (int8_t)i;
    b64_rev[(unsigned char)'='] = -2;
}

static int b64_encode(const uint8_t *in, size_t n, char **out, size_t *out_len)
{
    size_t groups = (n + 2) / 3;
    size_t len = groups * 4;
    char *s = malloc(len + 1);
    if (!s)
        return FAIL;

    size_t o = 0, i = 0;
    for (; i + 3 <= n; i += 3) {
        uint32_t v = ((uint32_t)in[i] << 16) | ((uint32_t)in[i + 1] << 8) | in[i + 2];
        s[o++] = B64_ALPHABET[(v >> 18) & 63];
        s[o++] = B64_ALPHABET[(v >> 12) & 63];
        s[o++] = B64_ALPHABET[(v >> 6) & 63];
        s[o++] = B64_ALPHABET[v & 63];
    }
    size_t rem = n - i;
    if (rem == 1) {
        uint32_t v = (uint32_t)in[i] << 16;
        s[o++] = B64_ALPHABET[(v >> 18) & 63];
        s[o++] = B64_ALPHABET[(v >> 12) & 63];
        s[o++] = '=';
        s[o++] = '=';
    } else if (rem == 2) {
        uint32_t v = ((uint32_t)in[i] << 16) | ((uint32_t)in[i + 1] << 8);
        s[o++] = B64_ALPHABET[(v >> 18) & 63];
        s[o++] = B64_ALPHABET[(v >> 12) & 63];
        s[o++] = B64_ALPHABET[(v >> 6) & 63];
        s[o++] = '=';
    }
    s[o] = '\0';
    *out = s;
    *out_len = o;
    return OK;
}

static int b64_decode(const char *s, size_t n, uint8_t **out, size_t *out_len)
{
    if (n % 4 != 0)
        return FAIL;
    size_t cap = n / 4 * 3;
    uint8_t *d = malloc(cap ? cap : 1);
    if (!d)
        return FAIL;

    size_t o = 0;
    for (size_t i = 0; i < n; i += 4) {
        int a = b64_rev[(unsigned char)s[i]];
        int b = b64_rev[(unsigned char)s[i + 1]];
        int c = b64_rev[(unsigned char)s[i + 2]];
        int e = b64_rev[(unsigned char)s[i + 3]];
        if (a < 0 || b < 0) {
            free(d);
            return FAIL;
        }
        uint32_t v = ((uint32_t)a << 18) | ((uint32_t)b << 12);
        if (c >= 0)
            v |= (uint32_t)c << 6;
        if (e >= 0)
            v |= (uint32_t)e;
        d[o++] = (uint8_t)(v >> 16);
        if (c >= 0)
            d[o++] = (uint8_t)(v >> 8);
        if (e >= 0)
            d[o++] = (uint8_t)v;
    }
    *out = d;
    *out_len = o;
    return OK;
}

/* ------------------------------------------------------------------ */
/* Ascii85 (Adobe / btoa), including the 'z' all-zero shorthand         */
/* ------------------------------------------------------------------ */

static int a85_encode(const uint8_t *in, size_t n, char **out, size_t *out_len)
{
    size_t cap = (n / 4) * 5 + 6;
    char *s = malloc(cap + 1);
    if (!s)
        return FAIL;

    size_t o = 0, i = 0;
    for (; i + 4 <= n; i += 4) {
        uint32_t v = ((uint32_t)in[i] << 24) | ((uint32_t)in[i + 1] << 16) |
                     ((uint32_t)in[i + 2] << 8) | in[i + 3];
        if (v == 0) {
            s[o++] = 'z';
            continue;
        }
        char g[5];
        for (int k = 4; k >= 0; k--) {
            g[k] = (char)('!' + (v % 85));
            v /= 85;
        }
        memcpy(s + o, g, 5);
        o += 5;
    }
    size_t rem = n - i;
    if (rem) {
        uint32_t v = 0;
        for (size_t k = 0; k < 4; k++)
            v = (v << 8) | (k < rem ? in[i + k] : 0);
        char g[5];
        for (int k = 4; k >= 0; k--) {
            g[k] = (char)('!' + (v % 85));
            v /= 85;
        }
        memcpy(s + o, g, rem + 1);
        o += rem + 1;
    }
    s[o] = '\0';
    *out = s;
    *out_len = o;
    return OK;
}

static int a85_decode(const char *s, size_t n, uint8_t **out, size_t *out_len)
{
    /* The 'z' shorthand expands a single character into four bytes, so the
       worst case is 4 bytes per input character, not 4 per group of 5. */
    uint8_t *d = malloc(n * 4 + 8);
    if (!d)
        return FAIL;

    size_t o = 0;
    uint32_t v = 0;
    int count = 0;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (c == 'z' && count == 0) {
            d[o++] = 0;
            d[o++] = 0;
            d[o++] = 0;
            d[o++] = 0;
            continue;
        }
        if (c < '!' || c > 'u') {
            free(d);
            return FAIL;
        }
        v = v * 85 + (uint32_t)(c - '!');
        if (++count == 5) {
            d[o++] = (uint8_t)(v >> 24);
            d[o++] = (uint8_t)(v >> 16);
            d[o++] = (uint8_t)(v >> 8);
            d[o++] = (uint8_t)v;
            v = 0;
            count = 0;
        }
    }
    if (count) {
        if (count == 1) {
            free(d);
            return FAIL;
        }
        for (int k = count; k < 5; k++)
            v = v * 85 + 84; /* pad with 'u' */
        for (int k = 0; k < count - 1; k++)
            d[o++] = (uint8_t)(v >> (24 - 8 * k));
    }
    *out = d;
    *out_len = o;
    return OK;
}

/* ------------------------------------------------------------------ */
/* Z85 (ZeroMQ RFC 32)                                                 */
/* ------------------------------------------------------------------ */

static const char Z85_ALPHABET[] =
    "0123456789abcdefghijklmnopqrstuvwxyz"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ.-:+=^!/*?&<>()[]{}@%$#";

static int8_t z85_rev[256];

static void z85_init(void)
{
    memset(z85_rev, -1, sizeof(z85_rev));
    for (int i = 0; i < 85; i++)
        z85_rev[(unsigned char)Z85_ALPHABET[i]] = (int8_t)i;
}

static int z85_encode(const uint8_t *in, size_t n, char **out, size_t *out_len)
{
    if (n % 4 != 0)
        return FAIL;
    size_t len = n / 4 * 5;
    char *s = malloc(len + 1);
    if (!s)
        return FAIL;

    size_t o = 0;
    for (size_t i = 0; i < n; i += 4) {
        uint32_t v = ((uint32_t)in[i] << 24) | ((uint32_t)in[i + 1] << 16) |
                     ((uint32_t)in[i + 2] << 8) | in[i + 3];
        char g[5];
        for (int k = 4; k >= 0; k--) {
            g[k] = Z85_ALPHABET[v % 85];
            v /= 85;
        }
        memcpy(s + o, g, 5);
        o += 5;
    }
    s[o] = '\0';
    *out = s;
    *out_len = o;
    return OK;
}

static int z85_decode(const char *s, size_t n, uint8_t **out, size_t *out_len)
{
    if (n % 5 != 0)
        return FAIL;
    size_t len = n / 5 * 4;
    uint8_t *d = malloc(len ? len : 1);
    if (!d)
        return FAIL;

    size_t o = 0;
    for (size_t i = 0; i < n; i += 5) {
        uint32_t v = 0;
        for (int k = 0; k < 5; k++) {
            int8_t x = z85_rev[(unsigned char)s[i + k]];
            if (x < 0) {
                free(d);
                return FAIL;
            }
            v = v * 85 + (uint32_t)x;
        }
        d[o++] = (uint8_t)(v >> 24);
        d[o++] = (uint8_t)(v >> 16);
        d[o++] = (uint8_t)(v >> 8);
        d[o++] = (uint8_t)v;
    }
    *out = d;
    *out_len = o;
    return OK;
}

/* ------------------------------------------------------------------ */
/* Base85N adapters                                                     */
/* ------------------------------------------------------------------ */

static int b85n_encode(const uint8_t *in, size_t n, char **out, size_t *out_len)
{
    return base85n_encode(in, n, out, out_len) == BASE85N_OK ? OK : FAIL;
}

static int b85n_decode(const char *s, size_t n, uint8_t **out, size_t *out_len)
{
    return base85n_decode(s, n, out, out_len) == BASE85N_OK ? OK : FAIL;
}

/* ------------------------------------------------------------------ */
/* Harness                                                              */
/* ------------------------------------------------------------------ */

typedef int (*enc_fn)(const uint8_t *, size_t, char **, size_t *);
typedef int (*dec_fn)(const char *, size_t, uint8_t **, size_t *);

typedef struct {
    const char *name;
    enc_fn encode;
    dec_fn decode;
    int requires_mul4;
} codec_t;

static const codec_t CODECS[] = {
    {"Base64", b64_encode, b64_decode, 0},
    {"Ascii85", a85_encode, a85_decode, 0},
    {"Z85", z85_encode, z85_decode, 1},
    {"Base85N", b85n_encode, b85n_decode, 0},
};
#define NCODECS ((int)(sizeof(CODECS) / sizeof(CODECS[0])))

static double now_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/* Minimum wall time one timing round must cover, in seconds. */
#define MIN_ROUND_SEC 0.25
#define ROUNDS 3

/* Returns the best observed throughput in MB/s over ROUNDS rounds. */
static double time_encode(enc_fn fn, const uint8_t *in, size_t n, int *ok)
{
    double best = 0.0;
    for (int r = 0; r < ROUNDS; r++) {
        double t0 = now_sec(), elapsed = 0.0;
        size_t iters = 0;
        while (elapsed < MIN_ROUND_SEC) {
            char *s = NULL;
            size_t sl = 0;
            if (fn(in, n, &s, &sl) != OK) {
                *ok = 0;
                return 0.0;
            }
            free(s);
            iters++;
            elapsed = now_sec() - t0;
        }
        double mbps = ((double)n * (double)iters) / elapsed / 1e6;
        if (mbps > best)
            best = mbps;
    }
    return best;
}

static double time_decode(dec_fn fn, const char *s, size_t sl, size_t raw_n, int *ok)
{
    double best = 0.0;
    for (int r = 0; r < ROUNDS; r++) {
        double t0 = now_sec(), elapsed = 0.0;
        size_t iters = 0;
        while (elapsed < MIN_ROUND_SEC) {
            uint8_t *d = NULL;
            size_t dl = 0;
            if (fn(s, sl, &d, &dl) != OK) {
                *ok = 0;
                return 0.0;
            }
            free(d);
            iters++;
            elapsed = now_sec() - t0;
        }
        /* Throughput is expressed in original (decoded) bytes per second,
           so encode and decode columns are directly comparable. */
        double mbps = ((double)raw_n * (double)iters) / elapsed / 1e6;
        if (mbps > best)
            best = mbps;
    }
    return best;
}

static uint8_t *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long sz = ftell(f);
    if (sz < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    uint8_t *buf = malloc((size_t)sz ? (size_t)sz : 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *len = (size_t)sz;
    return buf;
}

static const char *basename_of(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static void bench_buffer(const char *label, const uint8_t *data, size_t n)
{
    for (int c = 0; c < NCODECS; c++) {
        const codec_t *cd = &CODECS[c];
        size_t use_n = n;
        const char *note = "";
        if (cd->requires_mul4) {
            use_n = n - (n % 4);
            if (use_n != n)
                note = " (4-byte prefix)";
            if (use_n == 0) {
                printf("%-24s %-9s %10s %10s %10s   %s\n", label, cd->name,
                       "n/a", "n/a", "n/a", "input shorter than one group");
                continue;
            }
        }

        char *enc = NULL;
        size_t enc_len = 0;
        if (cd->encode(data, use_n, &enc, &enc_len) != OK) {
            printf("%-24s %-9s %10s %10s %10s   encode failed\n", label,
                   cd->name, "-", "-", "-");
            continue;
        }

        uint8_t *dec = NULL;
        size_t dec_len = 0;
        if (cd->decode(enc, enc_len, &dec, &dec_len) != OK ||
            dec_len != use_n || memcmp(dec, data, use_n) != 0) {
            printf("%-24s %-9s %10s %10s %10s   ROUND-TRIP FAILED\n", label,
                   cd->name, "-", "-", "-");
            free(enc);
            free(dec);
            continue;
        }
        free(dec);

        double ratio = (double)enc_len / (double)use_n;

        int ok = 1;
        double enc_mbps = time_encode(cd->encode, data, use_n, &ok);
        double dec_mbps = ok ? time_decode(cd->decode, enc, enc_len, use_n, &ok) : 0.0;
        free(enc);

        if (!ok) {
            printf("%-24s %-9s %10s %10s %10s   failed under timing\n", label,
                   cd->name, "-", "-", "-");
            continue;
        }

        printf("%-24s %-9s %10.2f %10.2f %10.3f   %s\n", label, cd->name,
               enc_mbps, dec_mbps, ratio, note);
    }
}

/* Deterministic xorshift64* so the synthetic inputs are identical on
   every machine and every run. */
static uint64_t rng_state = 0x9E3779B97F4A7C15ull;

static uint64_t rng_next(void)
{
    uint64_t x = rng_state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    rng_state = x;
    return x * 0x2545F4914F6CDD1Dull;
}

static void make_synthetic(uint8_t **rand_buf, uint8_t **text_buf, size_t n)
{
    uint8_t *r = malloc(n);
    uint8_t *t = malloc(n);
    if (!r || !t) {
        fprintf(stderr, "out of memory building synthetic inputs\n");
        exit(1);
    }
    for (size_t i = 0; i < n; i++)
        r[i] = (uint8_t)(rng_next() >> 24);

    /* Lowercase words separated by spaces and newlines: the shape of
       input Dynamic Passthrough is designed for. */
    static const char *words[] = {"the", "quick", "brown", "fox", "jumps",
                                  "over", "lazy", "dog", "and", "then",
                                  "returns", "home", "again"};
    size_t nwords = sizeof(words) / sizeof(words[0]);
    size_t o = 0;
    while (o < n) {
        const char *w = words[rng_next() % nwords];
        size_t wl = strlen(w);
        for (size_t k = 0; k < wl && o < n; k++)
            t[o++] = (uint8_t)w[k];
        if (o < n)
            t[o++] = (uint8_t)((rng_next() % 12 == 0) ? '\n' : ' ');
    }
    *rand_buf = r;
    *text_buf = t;
}

int main(int argc, char **argv)
{
    b64_init();
    z85_init();

    printf("Base85N throughput benchmark\n");
    printf("MB/s is measured in original (decoded) bytes; higher is better.\n");
    printf("Best of %d rounds, each round at least %.2f s of repeated calls.\n\n",
           ROUNDS, MIN_ROUND_SEC);
    printf("%-24s %-9s %10s %10s %10s\n", "input", "codec", "encode", "decode",
           "ratio");
    printf("%-24s %-9s %10s %10s %10s\n", "", "", "MB/s", "MB/s", "chars/B");
    printf("--------------------------------------------------------------------"
           "-------\n");

    const size_t SYNTH = 1u << 20; /* 1 MiB */
    uint8_t *rbuf = NULL, *tbuf = NULL;
    make_synthetic(&rbuf, &tbuf, SYNTH);
    bench_buffer("synthetic random 1MiB", rbuf, SYNTH);
    printf("\n");
    bench_buffer("synthetic text 1MiB", tbuf, SYNTH);
    free(rbuf);
    free(tbuf);

    /* Worst case for the Dynamic Passthrough window search: every byte
       needs escaping, so Pass 2 gives up after three of them while Pass 1
       has just scanned the whole remaining run. Encoding is quadratic in
       the length of such a run, which is why this buffer is kept small --
       at 1 MiB the Base85N row alone would run for tens of minutes. See
       ../results/RESULTS.md. */
    const size_t ESCAPE_HEAVY = 16u << 10; /* 16 KiB */
    uint8_t *ebuf = malloc(ESCAPE_HEAVY);
    if (!ebuf) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }
    memset(ebuf, '~', ESCAPE_HEAVY);
    printf("\n");
    bench_buffer("escape-heavy 16KiB", ebuf, ESCAPE_HEAVY);
    free(ebuf);

    for (int i = 1; i < argc; i++) {
        size_t n = 0;
        uint8_t *data = read_file(argv[i], &n);
        if (!data) {
            fprintf(stderr, "cannot read %s, skipping\n", argv[i]);
            continue;
        }
        printf("\n");
        bench_buffer(basename_of(argv[i]), data, n);
        free(data);
    }
    return 0;
}
