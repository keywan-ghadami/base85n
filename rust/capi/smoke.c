/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/*
 * A C caller of the Rust library, used by run.sh as the test that the C
 * ABI in rust/src/ffi.rs is what its header says it is. run.sh compiles
 * this file twice, once against rust/include/base85n.h and once against
 * c/include/base85n.h, linking the Rust library both times: passing with
 * the C implementation's own header is what makes "drop-in replacement"
 * a checked claim rather than a promise.
 *
 * Everything here is from the caller's side of the boundary: buffers are
 * released with free(), the encoded string is used as a C string, and
 * the decoded buffer is used as bytes and never as one.
 */

#include <base85n.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

static void check(int condition, const char *what) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

/* Encode, decode, and require the bytes back unchanged. */
static void round_trip(const uint8_t *data, size_t len, const char *what) {
    char *encoded = NULL;
    size_t encoded_len = 0;
    base85n_status st = base85n_encode(data, len, &encoded, &encoded_len);
    if (st != BASE85N_OK) {
        fprintf(stderr, "FAIL: %s: encode: %s\n", what, base85n_strerror(st));
        failures++;
        return;
    }
    check(encoded != NULL, "encode returns a buffer");
    check(strlen(encoded) == encoded_len, "encoded string is NUL-terminated at out_len");

    uint8_t *decoded = NULL;
    size_t decoded_len = 0;
    st = base85n_decode(encoded, encoded_len, &decoded, &decoded_len);
    if (st != BASE85N_OK) {
        fprintf(stderr, "FAIL: %s: decode: %s\n", what, base85n_strerror(st));
        failures++;
        free(encoded);
        return;
    }
    check(decoded != NULL, "decode returns a buffer even for empty output");
    check(decoded_len == len, what);
    check(len == 0 || memcmp(decoded, data, len) == 0, what);

    free(encoded);
    free(decoded);
}

/* Require a decode to fail with `want`, and to leave the out-parameters alone. */
static void rejects(const char *text, base85n_status want, const char *what) {
    uint8_t *out = (uint8_t *)0;
    size_t out_len = 0;
    uint8_t *before = out;
    base85n_status st = base85n_decode(text, strlen(text), &out, &out_len);
    if (st != want) {
        fprintf(stderr, "FAIL: %s: got %s, wanted %s\n", what,
                base85n_strerror(st), base85n_strerror(want));
        failures++;
        if (st == BASE85N_OK) free(out);
        return;
    }
    check(out == before, "a rejected decode does not touch *out_data");
    check(out_len == 0, "a rejected decode does not touch *out_len");
}

int main(void) {
    const uint8_t text[] = "{\"id\":184223,\"name\":\"Ada Lovelace\"}";
    round_trip(text, sizeof(text) - 1, "JSON record");
    round_trip((const uint8_t *)"hello, world!", 13, "short text");
    round_trip((const uint8_t *)"", 0, "empty input");
    round_trip(NULL, 0, "NULL input with zero length");

    /* Every byte value, so the block path and the passthrough path both run. */
    uint8_t all[256];
    for (int i = 0; i < 256; i++) all[i] = (uint8_t)i;
    round_trip(all, sizeof all, "all 256 byte values");

    /* Long enough to cross MAX_DP_ANALYSIS_BYTES and need several signals. */
    uint8_t *big = malloc(BASE85N_MAX_DP_ANALYSIS_BYTES * 4);
    check(big != NULL, "test allocation");
    if (big) {
        for (size_t i = 0; i < BASE85N_MAX_DP_ANALYSIS_BYTES * 4; i++)
            big[i] = (uint8_t)('a' + (i % 26));
        round_trip(big, BASE85N_MAX_DP_ANALYSIS_BYTES * 4, "multi-segment passthrough");
        free(big);
    }

    rejects("ab\"cd", BASE85N_ERR_INVALID_CHAR, "character outside Alphabet-N");
    rejects("a", BASE85N_ERR_INVALID_FINAL_BLOCK, "lone trailing character");

    /* Argument validation, without dereferencing what it rejects. */
    char *out_str = NULL;
    size_t out_len = 0;
    check(base85n_encode(NULL, 1, &out_str, &out_len) == BASE85N_ERR_INVALID_ARGUMENT,
          "NULL data with a non-zero length is rejected");
    check(base85n_encode((const uint8_t *)"x", 1, NULL, &out_len)
              == BASE85N_ERR_INVALID_ARGUMENT,
          "NULL out_str is rejected, not written through");
    uint8_t *out_data = NULL;
    check(base85n_decode("x", 1, &out_data, NULL) == BASE85N_ERR_INVALID_ARGUMENT,
          "NULL out_len is rejected, not written through");
    check(base85n_decode(NULL, 1, &out_data, &out_len) == BASE85N_ERR_INVALID_ARGUMENT,
          "NULL text with a non-zero length is rejected");

    check(strcmp(base85n_strerror(BASE85N_OK), "ok") == 0, "strerror(BASE85N_OK)");
    check(base85n_strerror(BASE85N_ERR_ALLOC)[0] != '\0', "strerror is never empty");

    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("ok: C caller round-tripped through the Rust library\n");
    return 0;
}
