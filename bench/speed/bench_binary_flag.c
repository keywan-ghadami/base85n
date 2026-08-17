/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/*
 * bench_binary_flag.c - what a `--binary` encoder mode would buy, and
 * where the difference would come from.
 *
 * The question
 * ------------
 * A proposed `--binary` encoder flag would tell the encoder that its input
 * is binary, so that it could skip the Dynamic Passthrough machinery that
 * binary input never uses. A flag is a permanent cost -- a second encoder
 * to document, test and keep in step with the first -- so it has to pay
 * for itself in measured throughput, and the measurement has to show that
 * the gain comes from the flag rather than from something else that could
 * be had without one.
 *
 * That needs four encoders, not two. The encoder has two independent
 * optional steps, and a single `default` vs `--binary` comparison confounds
 * them:
 *
 *      variant           DP (steps 2-3)   Fill + zero run (step 1)
 *      --------------------------------------------------------
 *      default                 yes                 yes            <- shipped
 *      binary                  no                  yes
 *      default-nofill          yes                 no
 *      binary-nofill           no                  no
 *
 * `binary` vs `default` is the number the decision rests on. `binary-nofill`
 * vs `default-nofill` shows the same difference with Fill taken out of both
 * sides, which is what separates "skipping DP is fast" from "the flag's
 * inputs happen to have runs in them". The two nofill rows also price
 * Fill itself, which no `--binary` proposal should be charged for.
 *
 * Only `default` is a conforming Base85N encoder. Section 6.5 of the
 * specification makes every step mandatory at every decision point the
 * loop reaches, so the other three are non-conforming dialects: their
 * output decodes, because the decoder accepts every construct wherever it
 * appears, but no conforming encoder would produce it. They are built here
 * to be measured, and they are compiled out of every other build. The
 * round trip below still checks all four, since a dialect that lost data
 * would post a fast number for the wrong reason.
 *
 * Measuring a 4 % difference on a virtual machine
 * -----------------------------------------------
 * The run-to-run spread on a shared machine is of the same order as the
 * difference this benchmark has to resolve, so a best-of-N per variant,
 * measured one variant after another, is not good enough: any drift over
 * the run lands entirely on whichever variant was measured while it
 * happened.
 *
 * Two things are done about it. Rounds are *interleaved* -- every variant
 * is timed once, then all of them again, ROUNDS times -- so drift is
 * spread across all four rather than attributed to one. And the comparison
 * is *paired*: the ratio to `default` is computed within each round, from
 * two timings taken milliseconds apart, and the reported figure is the
 * median of those per-round ratios. A machine that is 10 % slower for one
 * round makes both halves of that round's ratio 10 % slower and leaves the
 * ratio alone. The observed spread of the per-round ratios is printed
 * beside every figure, so a difference smaller than the noise is visible
 * as such instead of being read off as a result.
 *
 * For a number that does not depend on the machine at all, the same four
 * encoders are driven under callgrind by binary_flag_instructions.sh.
 */

/* clock_gettime/CLOCK_MONOTONIC are not exposed by a strict -std=c11 build. */
#define _POSIX_C_SOURCE 199309L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#include "base85n.h"

/* The three non-conforming dialects, built into base85n.c only when
 * BASE85N_BENCH_ENCODERS is defined. Deliberately not in base85n.h: they
 * are not API, and nothing but this benchmark may reach them. */
base85n_status base85n_encode_bench_nodp(const uint8_t *data, size_t data_len,
                                          char **out_str, size_t *out_len);
base85n_status base85n_encode_bench_nofill(const uint8_t *data, size_t data_len,
                                            char **out_str, size_t *out_len);
base85n_status base85n_encode_bench_block(const uint8_t *data, size_t data_len,
                                           char **out_str, size_t *out_len);
base85n_status base85n_encode_bench_narrowgate(const uint8_t *data, size_t data_len,
                                                char **out_str, size_t *out_len);
base85n_status base85n_encode_bench_widegate(const uint8_t *data, size_t data_len,
                                              char **out_str, size_t *out_len);
base85n_status base85n_encode_bench_wordgate(const uint8_t *data, size_t data_len,
                                              char **out_str, size_t *out_len);

typedef base85n_status (*enc_fn)(const uint8_t *, size_t, char **, size_t *);

typedef struct {
    const char *name;
    enc_fn fn;
    const char *what;
    int conforming;
} variant_t;

static const variant_t VARIANTS[] = {
    { "default",       base85n_encode,                "DP + Fill  (shipped)", 1 },
    { "binary",        base85n_encode_bench_nodp,     "Fill only",            0 },
    { "default-nofill",base85n_encode_bench_nofill,   "DP only",              0 },
    { "binary-nofill", base85n_encode_bench_block,    "block mode only",      0 },
    { "narrow-gate",   base85n_encode_bench_narrowgate,"1-byte gate, step 4", 1 },
    { "gate4-only",    base85n_encode_bench_widegate, "4-byte gate, step 4",  1 },
    { "word-gate",     base85n_encode_bench_wordgate, "word gate, step 4",    1 },
};
#define NVARIANTS ((int)(sizeof VARIANTS / sizeof VARIANTS[0]))
#define V_DEFAULT 0
#define V_BINARY  1
#define V_NARROW  4
#define V_GATE4   5
#define V_WORD    6

/* ------------------------------------------------------------------ */
/* Timing                                                              */
/* ------------------------------------------------------------------ */

#define ROUNDS 15
#define MIN_ROUND_SEC 0.10

static double now_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/* One round: repeated encodes for at least MIN_ROUND_SEC, returning MiB/s
 * in original (input) bytes. Returns 0 if the encoder failed. */
static double one_round(enc_fn fn, const uint8_t *in, size_t n)
{
    double t0 = now_sec(), elapsed = 0.0;
    size_t iters = 0;
    while (elapsed < MIN_ROUND_SEC) {
        char *s = NULL;
        size_t sl = 0;
        if (fn(in, n, &s, &sl) != BASE85N_OK)
            return 0.0;
        free(s);
        iters++;
        elapsed = now_sec() - t0;
    }
    return ((double)n * (double)iters) / elapsed / 1048576.0;
}

static int cmp_double(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : (x > y ? 1 : 0);
}

static double median(double *v, int n)
{
    qsort(v, (size_t)n, sizeof *v, cmp_double);
    return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2.0;
}

/* ------------------------------------------------------------------ */
/* Correctness                                                         */
/* ------------------------------------------------------------------ */

/* Every variant must round-trip: the decoder understands all five encoders,
 * and one that lost data would otherwise post a fast number. A variant that
 * claims to be conforming is held to more than that -- its output must be
 * the shipped encoder's, character for character, since the specification
 * admits exactly one encoding of any input. `ref` is that output, or NULL
 * when this is the reference itself.
 *
 * Returns the encoded length, or 0 on any failure. */
static size_t verify(const variant_t *v, const uint8_t *in, size_t n,
                     const char *ref, size_t ref_len)
{
    char *s = NULL;
    size_t sl = 0;
    if (v->fn(in, n, &s, &sl) != BASE85N_OK) {
        fprintf(stderr, "%s: encode failed\n", v->name);
        return 0;
    }
    uint8_t *d = NULL;
    size_t dl = 0;
    base85n_status st = base85n_decode(s, sl, &d, &dl);
    if (st != BASE85N_OK) {
        fprintf(stderr, "%s: decode failed: %s\n", v->name, base85n_strerror(st));
        free(s);
        return 0;
    }
    int bad = (dl != n) || (n && memcmp(d, in, n) != 0);
    if (bad)
        fprintf(stderr, "%s: ROUND TRIP MISMATCH\n", v->name);
    if (!bad && v->conforming && ref &&
        (sl != ref_len || memcmp(s, ref, sl) != 0)) {
        fprintf(stderr, "%s: output differs from the shipped encoder's, so it "
                        "is not the conforming encoder it claims to be\n",
                v->name);
        bad = 1;
    }
    free(s);
    free(d);
    return bad ? 0 : sl;
}

/* The shipped encoder's output, for the identity check above. */
static char *reference_encoding(const uint8_t *in, size_t n, size_t *out_len)
{
    char *s = NULL;
    if (base85n_encode(in, n, &s, out_len) != BASE85N_OK)
        return NULL;
    return s;
}

/* ------------------------------------------------------------------ */
/* Reporting                                                           */
/* ------------------------------------------------------------------ */

static void bench_buffer(const char *label, const uint8_t *data, size_t n)
{
    size_t enc_len[NVARIANTS];
    double mibs[NVARIANTS][ROUNDS];
    double ratio[NVARIANTS][ROUNDS];

    size_t ref_len = 0;
    char *ref = reference_encoding(data, n, &ref_len);
    if (!ref) {
        printf("%s: skipped, the reference encode failed\n", label);
        return;
    }
    for (int v = 0; v < NVARIANTS; v++) {
        enc_len[v] = verify(&VARIANTS[v], data, n, ref, ref_len);
        if (!enc_len[v]) {
            printf("%s: skipped, `%s` failed its checks\n", label,
                   VARIANTS[v].name);
            free(ref);
            return;
        }
    }
    free(ref);

    /* Interleaved: all four variants in each round, so drift over the run
     * is shared rather than charged to whichever went first. */
    for (int r = 0; r < ROUNDS; r++) {
        for (int v = 0; v < NVARIANTS; v++)
            mibs[v][r] = one_round(VARIANTS[v].fn, data, n);
        for (int v = 0; v < NVARIANTS; v++)
            ratio[v][r] = mibs[V_DEFAULT][r] > 0.0
                              ? mibs[v][r] / mibs[V_DEFAULT][r]
                              : 0.0;
    }

    printf("%s  (%.2f MiB)\n", label, (double)n / 1048576.0);
    printf("  %-15s %-22s %4s %9s %9s %9s %14s\n",
           "variant", "steps", "conf", "MiB/s", "chars/B", "vs def", "round range");

    for (int v = 0; v < NVARIANTS; v++) {
        double speed = median(mibs[v], ROUNDS);
        double rel = median(ratio[v], ROUNDS);
        double rlo = ratio[v][0], rhi = ratio[v][ROUNDS - 1]; /* sorted above */

        printf("  %-15s %-22s %4s %9.1f %9.3f %+8.1f%% ",
               VARIANTS[v].name, VARIANTS[v].what,
               VARIANTS[v].conforming ? "yes" : "no", speed,
               (double)enc_len[v] / (double)n, (rel - 1.0) * 100.0);
        if (v == V_DEFAULT) {
            double lo = mibs[v][0], hi = mibs[v][ROUNDS - 1];
            printf("%7.1f%% spread\n", (hi - lo) / speed * 100.0);
        } else {
            printf("%+6.1f .. %+.1f%%\n", (rlo - 1.0) * 100.0,
                   (rhi - 1.0) * 100.0);
        }
    }

    /* The decision, as a paired per-round ratio to the shipped encoder with
     * the range the rounds actually spanned. A verdict is only meaningful
     * when that whole range sits on one side of the 4 % threshold.
     *
     * `default` here is the encoder *after* the skip's gate was widened,
     * which is the comparison the rule has to be read against: a flag has
     * to beat what the encoder can do without one, not what it happened to
     * do before anyone looked. */
    {
        double *r = ratio[V_BINARY];
        double lo = r[0], hi = r[ROUNDS - 1], mid = median(r, ROUNDS);
        const char *verdict = lo > 1.04   ? "PASS: above +4% for every round"
                              : hi < 1.04 ? "FAIL: below +4% for every round"
                                          : "INCONCLUSIVE: straddles +4%";
        printf("  --binary vs default   %+7.1f%% (%+.1f .. %+.1f%%)  %s\n",
               (mid - 1.0) * 100.0, (lo - 1.0) * 100.0, (hi - 1.0) * 100.0,
               verdict);
    }

    /* What the flag would have been credited with had the gate not been
     * widened first -- the same measurement against the older encoder. The
     * gap between the two lines is the part of `--binary`'s case that was
     * never about `--binary`. */
    double stale[ROUNDS];
    for (int r = 0; r < ROUNDS; r++)
        stale[r] = mibs[V_NARROW][r] > 0.0
                       ? mibs[V_BINARY][r] / mibs[V_NARROW][r]
                       : 0.0;
    double smid = median(stale, ROUNDS);
    printf("  --binary vs narrow-gate %+6.1f%%  (what the same flag would "
           "have scored before)\n", (smid - 1.0) * 100.0);

    /* The other half of the trade. `--binary` is not the same output sooner;
     * it is different, larger output sooner, because every DP segment it
     * declines to look for is spent in block mode at 1.25 characters per
     * byte instead of 1. */
    double bloat = ((double)enc_len[V_BINARY] / (double)enc_len[V_DEFAULT] - 1.0)
                   * 100.0;
    printf("  --binary output size    %+6.1f%%  (%s)\n\n", bloat,
           bloat > 1.0 ? "paid for the speed" : "nothing to give up here");
}

/* ------------------------------------------------------------------ */
/* Inputs                                                              */
/* ------------------------------------------------------------------ */

static uint64_t rng_state = 0x2545F4914F6CDD1DULL;
static uint8_t xorshift(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return (uint8_t)(rng_state >> 24);
}

static uint8_t *read_file(const char *path, size_t *out_n)
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
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    *out_n = got;
    return buf;
}

static const char *basename_of(const char *p)
{
    const char *slash = strrchr(p, '/');
    return slash ? slash + 1 : p;
}

/* ------------------------------------------------------------------ */

/* Every variant marked conforming must agree with base85n_encode() character
 * for character on every input, not just on the corpus. The gate widths are
 * a change to how the skip of spec 11.1 finds the next decision point, and
 * the whole of that section's licence is that it may only pass over
 * positions where no step applies -- so the property to hold them to is
 * exact equality, over inputs shaped like the boundaries the encoder's
 * branches turn on rather than uniform random bytes, which almost never
 * produce a DP segment or a Fill run at all.
 *
 * Returns 0 if every case agreed. */
static int selftest(long cases)
{
    int failures = 0;
    size_t max = 4096;
    uint8_t *buf = malloc(max);
    if (!buf)
        return 1;

    for (long c = 0; c < cases; c++) {
        size_t n = (size_t)(xorshift() | ((size_t)xorshift() << 8)) % max + 1;
        /* Each case is built from one of a few alphabets, in runs of a
         * random length, so that run boundaries, the DP threshold and the
         * analysis window all fall in the middle of segments rather than
         * only at the ends. */
        size_t i = 0;
        while (i < n) {
            size_t run = (size_t)xorshift() % 40 + 1;
            if (run > n - i)
                run = n - i;
            switch (xorshift() % 6) {
            case 0: /* zeros: Fill and both tail variants */
                memset(buf + i, 0, run);
                break;
            case 1: /* one repeated non-zero byte: solid Fill */
                memset(buf + i, (int)xorshift(), run);
                break;
            case 2: /* ASCII text: long DP segments */
                for (size_t k = 0; k < run; k++)
                    buf[i + k] = (uint8_t)(0x20 + xorshift() % 0x5F);
                break;
            case 3: /* text plus the whitespace only the table admits */
                for (size_t k = 0; k < run; k++) {
                    uint8_t w[3] = { 0x09, 0x0A, 0x0D };
                    buf[i + k] = (xorshift() % 8 == 0)
                                     ? w[xorshift() % 3]
                                     : (uint8_t)(0x20 + xorshift() % 0x5F);
                }
                break;
            case 4: /* the range the word gate admits but the table does not */
                for (size_t k = 0; k < run; k++)
                    buf[i + k] = (uint8_t)(0x09 + xorshift() % 0x18);
                break;
            default: /* high entropy */
                for (size_t k = 0; k < run; k++)
                    buf[i + k] = xorshift();
                break;
            }
            i += run;
        }

        char *ref = NULL;
        size_t ref_len = 0;
        if (base85n_encode(buf, n, &ref, &ref_len) != BASE85N_OK) {
            fprintf(stderr, "selftest: reference encode failed\n");
            free(buf);
            return 1;
        }
        for (int v = 0; v < NVARIANTS; v++) {
            if (!VARIANTS[v].conforming)
                continue;
            if (!verify(&VARIANTS[v], buf, n, ref, ref_len)) {
                fprintf(stderr, "selftest: case %ld, %zu bytes, variant %s\n",
                        c, n, VARIANTS[v].name);
                failures++;
            }
        }
        free(ref);
        if (failures > 10)
            break;
    }
    free(buf);
    if (failures) {
        printf("selftest: %d FAILURES\n", failures);
        return 1;
    }
    printf("selftest: %ld cases, every conforming variant byte-identical to "
           "base85n_encode()\n", cases);
    return 0;
}

static int find_variant(const char *name)
{
    for (int v = 0; v < NVARIANTS; v++)
        if (strcmp(VARIANTS[v].name, name) == 0)
            return v;
    return -1;
}

int main(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "--selftest") == 0)
        return selftest(argc >= 3 ? atol(argv[2]) : 20000);

    /* `--count VARIANT FILE...` encodes each file exactly once with one
     * variant and exits, so callgrind can count the instructions and
     * subtract a `--count none` run for the setup. */
    if (argc >= 3 && strcmp(argv[1], "--count") == 0) {
        int measure = strcmp(argv[2], "none") != 0;
        int v = measure ? find_variant(argv[2]) : 0;
        if (v < 0) {
            fprintf(stderr, "unknown variant %s; expected `none` or one of:",
                    argv[2]);
            for (int k = 0; k < NVARIANTS; k++)
                fprintf(stderr, " %s", VARIANTS[k].name);
            fprintf(stderr, "\n");
            return 2;
        }
        for (int i = 3; i < argc; i++) {
            size_t n = 0;
            uint8_t *data = read_file(argv[i], &n);
            if (!data) {
                fprintf(stderr, "cannot read %s\n", argv[i]);
                return 2;
            }
            if (measure) {
                char *s = NULL;
                size_t sl = 0;
                if (VARIANTS[v].fn(data, n, &s, &sl) != BASE85N_OK) {
                    free(data);
                    return 1;
                }
                free(s);
            }
            free(data);
        }
        return 0;
    }

    printf("Would a --binary encoder flag pay for itself?\n");
    printf("MiB/s is encode throughput in original (input) bytes; higher is "
           "better.\n");
    printf("Median of %d interleaved rounds, each at least %.2f s of repeated "
           "encodes.\n", ROUNDS, MIN_ROUND_SEC);
    printf("`vs def` is the median of the per-round ratios to `default`, "
           "paired within\n");
    printf("each round; `spread` is that ratio's range over the rounds "
           "(the default row\n");
    printf("shows its own MiB/s range instead). Threshold for keeping the "
           "flag: +4%%.\n");
    printf("Only `default` is a conforming encoder; see the file header.\n\n");

    const size_t SYNTH = 1u << 20;
    uint8_t *buf = malloc(SYNTH);
    if (!buf) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }

    /* Incompressible binary: no run, no representable stretch, every byte
     * decided by the lookahead. This is the case a --binary flag exists
     * for, and the ceiling on what one could ever save. */
    for (size_t i = 0; i < SYNTH; i++)
        buf[i] = xorshift();
    bench_buffer("synthetic random 1MiB", buf, SYNTH);

    /* Text is the counter-case, and it is here to keep the binary numbers
     * honest. Anything that speeds binary up by giving Dynamic Passthrough
     * less benefit of the doubt has to leave this row alone. */
    {
        const char *lit = "The quick brown fox jumps over the lazy dog. "
                          "Pack my box with five dozen liquor jugs! 0123456789 ";
        size_t l = strlen(lit);
        for (size_t i = 0; i < SYNTH; i++)
            buf[i] = (uint8_t)lit[i % l];
        bench_buffer("synthetic text 1MiB", buf, SYNTH);
    }

    /* The adversarial case for the DP scan: 18 representable bytes then one
     * no profile can carry, so a candidate is always started and never
     * reaches MIN_PASSTHROUGH_BYTES. It is the input a wider gate could
     * plausibly hurt, since the gate keeps clearing and the scan keeps
     * failing, so it is measured rather than assumed. */
    for (size_t i = 0; i < SYNTH; i++)
        buf[i] = (i % 19 == 18) ? (uint8_t)0x80 : (uint8_t)('a' + (i % 26));
    bench_buffer("scan-heavy 1MiB", buf, SYNTH);

    free(buf);

    for (int i = 1; i < argc; i++) {
        size_t n = 0;
        uint8_t *data = read_file(argv[i], &n);
        if (!data) {
            fprintf(stderr, "cannot read %s, skipping\n", argv[i]);
            continue;
        }
        bench_buffer(basename_of(argv[i]), data, n);
        free(data);
    }
    return 0;
}
