/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/*
 * differential.c - check this implementation against the generated cases in
 * tools/gen_differential_cases.py, line by line.
 *
 *   python3 tools/gen_differential_cases.py /tmp/cases
 *   cc -O2 -std=c11 -Ic/include c/src/base85n.c c/tools/differential.c -o diff
 *   ./diff /tmp/cases/inputs.txt /tmp/cases/expected.txt
 *
 * The shared vectors pin a few dozen agreed answers; this pins thousands,
 * generated from the shapes the encoder's branches actually turn on.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <base85n.h>

static int hex_nibble(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: differential <inputs.txt> <expected.txt>\n");
        return 2;
    }
    FILE *fi = fopen(argv[1], "r");
    FILE *fe = fopen(argv[2], "r");
    if (!fi || !fe) {
        fprintf(stderr, "cannot open input files\n");
        return 2;
    }

    char *hex = NULL, *want = NULL;
    size_t hex_cap = 0, want_cap = 0;
    long line = 0, mismatches = 0;
    ssize_t hex_len, want_len;

    while ((hex_len = getline(&hex, &hex_cap, fi)) >= 0 &&
           (want_len = getline(&want, &want_cap, fe)) >= 0) {
        line++;
        while (hex_len > 0 && (hex[hex_len - 1] == '\n' || hex[hex_len - 1] == '\r')) {
            hex[--hex_len] = 0;
        }
        while (want_len > 0 && (want[want_len - 1] == '\n' || want[want_len - 1] == '\r')) {
            want[--want_len] = 0;
        }
        if (hex_len % 2 != 0) {
            fprintf(stderr, "line %ld: odd-length hex\n", line);
            return 2;
        }

        size_t n = (size_t)hex_len / 2;
        uint8_t *data = (uint8_t *)malloc(n ? n : 1);
        for (size_t i = 0; i < n; i++) {
            int hi = hex_nibble(hex[2 * i]), lo = hex_nibble(hex[2 * i + 1]);
            if (hi < 0 || lo < 0) {
                fprintf(stderr, "line %ld: bad hex\n", line);
                return 2;
            }
            data[i] = (uint8_t)((hi << 4) | lo);
        }

        char *enc = NULL;
        size_t enc_len = 0;
        if (base85n_encode(data, n, &enc, &enc_len) != BASE85N_OK) {
            fprintf(stderr, "line %ld: encode failed\n", line);
            return 2;
        }
        if (enc_len != (size_t)want_len || memcmp(enc, want, enc_len) != 0) {
            if (mismatches < 5) {
                fprintf(stderr, "line %ld: got %.*s\n              want %s\n",
                        line, (int)enc_len, enc, want);
            }
            mismatches++;
        }

        /* The round trip too, so a case cannot pass by agreeing on garbage. */
        uint8_t *back = NULL;
        size_t back_len = 0;
        if (base85n_decode(enc, enc_len, &back, &back_len) != BASE85N_OK ||
            back_len != n || (n && memcmp(back, data, n) != 0)) {
            fprintf(stderr, "line %ld: round trip failed\n", line);
            mismatches++;
        }
        free(back);
        free(enc);
        free(data);
    }

    printf("checked %ld cases, %ld mismatches\n", line, mismatches);
    return mismatches ? 1 : 0;
}
