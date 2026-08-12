/* Instruction-count harness: encodes or decodes one fixed input once, so that
 * `callgrind` can be run over it and the setup subtracted with a `none` run. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <base85n.h>

static unsigned long long s_state = 0x2545F4914F6CDD1DULL;
static unsigned char xorshift(void) {
    s_state ^= s_state << 13;
    s_state ^= s_state >> 7;
    s_state ^= s_state << 17;
    return (unsigned char)(s_state >> 24);
}

int main(int argc, char **argv) {
    const char *kind = argv[1];
    const char *phase = argv[2];
    size_t n = (size_t)atol(argv[3]);

    unsigned char *data = malloc(n);
    if (strcmp(kind, "random") == 0) {
        for (size_t i = 0; i < n; i++) data[i] = xorshift();
    } else if (strcmp(kind, "text") == 0) {
        FILE *f = fopen(argv[4], "rb");
        size_t got = fread(data, 1, n, f);
        fclose(f);
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

    if (strcmp(phase, "encode") == 0) {
        char *e2 = NULL;
        size_t l2 = 0;
        if (base85n_encode(data, n, &e2, &l2) != BASE85N_OK) return 1;
        printf("%zu\n", l2);
        free(e2);
    } else if (strcmp(phase, "decode") == 0) {
        unsigned char *out = NULL;
        size_t out_len = 0;
        if (base85n_decode(enc, enc_len, &out, &out_len) != BASE85N_OK) return 1;
        printf("%zu\n", out_len);
        free(out);
    } else {
        printf("%zu\n", enc_len);
    }

    free(enc);
    free(data);
    return 0;
}
