/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/*
 * Wall-clock harness for the C implementation, and the twin of
 * rust/examples/throughput.rs: same generated inputs, same loop, same
 * reporting, so that the two numbers can be divided by one another.
 *
 * Builds one input, encodes or decodes it `reps` times, and reports the
 * fastest round in MiB/s. Fastest rather than mean: on a shared machine the
 * slow rounds are the ones that were interrupted, and the fastest round is
 * the closest thing to the work the code actually does.
 *
 * Usage: time <random|text|mixed> <encode|decode> <bytes> <reps> [file]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <base85n.h>

static unsigned long long s_state = 0x2545F4914F6CDD1DULL;
static unsigned char xorshift(void) {
    s_state ^= s_state << 13;
    s_state ^= s_state >> 7;
    s_state ^= s_state << 17;
    return (unsigned char)(s_state >> 24);
}

static double now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

int main(int argc, char **argv) {
    if (argc < 5) {
        fprintf(stderr, "usage: %s <random|text|mixed> <encode|decode> <bytes> <reps> [file]\n",
                argv[0]);
        return 2;
    }
    const char *kind = argv[1];
    const char *phase = argv[2];
    size_t n = (size_t)atol(argv[3]);
    int reps = atoi(argv[4]);

    unsigned char *data = malloc(n);
    if (!data) return 1;
    if (strcmp(kind, "random") == 0) {
        for (size_t i = 0; i < n; i++) data[i] = xorshift();
    } else if (strcmp(kind, "text") == 0) {
        FILE *f = fopen(argv[5], "rb");
        if (!f) return 1;
        size_t got = fread(data, 1, n, f);
        fclose(f);
        if (!got) return 1;
        /* Tile the file until the buffer is full, so the input is the same
         * length whatever the file's is. */
        for (size_t i = got; i < n; i++) data[i] = data[i - got];
    } else {
        size_t i = 0;
        const char *lit = "hello world this is text 0123456789 ";
        while (i < n) {
            for (int k = 0; k < 40 && i < n; k++) data[i++] = xorshift();
            for (const char *p = lit; *p && i < n; p++) data[i++] = (unsigned char)*p;
        }
    }

    char *enc = NULL;
    size_t enc_len = 0;
    if (base85n_encode(data, n, &enc, &enc_len) != BASE85N_OK) return 1;

    double best = 1e30;
    size_t sink = 0;
    for (int r = 0; r < reps; r++) {
        double t0 = now();
        if (strcmp(phase, "encode") == 0) {
            char *e = NULL;
            size_t l = 0;
            if (base85n_encode(data, n, &e, &l) != BASE85N_OK) return 1;
            sink += l;
            free(e);
        } else {
            unsigned char *o = NULL;
            size_t l = 0;
            if (base85n_decode(enc, enc_len, &o, &l) != BASE85N_OK) return 1;
            sink += l;
            free(o);
        }
        double dt = now() - t0;
        if (dt < best) best = dt;
    }
    /* The encoded length goes to stderr, where the driver compares it against
     * the Rust harness's: two implementations that disagree about the output
     * are not being compared on speed. */
    fprintf(stderr, "encoded %zu bytes (checksum of runs %zu)\n", enc_len, sink);
    printf("%.2f\n", (double)n / best / (1024.0 * 1024.0));
    free(enc);
    free(data);
    return 0;
}
