/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/*
 * fuzz_roundtrip.c - the encoder's side of the contract, over arbitrary
 * input.
 *
 * Three properties, all of which the specification states and none of
 * which the test vectors can check over anything but the inputs someone
 * thought of:
 *
 *   1. Encoding never fails on the content of its input. Every byte value
 *      is representable in block mode, so the only permitted failure is
 *      allocation.
 *   2. Decoding an encoder's output returns exactly the input bytes.
 *   3. The output contains only Alphabet-N characters. This is the whole
 *      point of the alphabet -- a caller places the output in JSON, in an
 *      HTML attribute or in XML character data without escaping it -- so
 *      a stray character is a correctness bug even though the round trip
 *      would survive it.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "base85n.h"

/* Alphabet-N, spec section 4.1, written out here rather than taken from the
 * library so that this harness checks the library against the specification
 * and not against itself. The C test suite separately validates the
 * implementation's own copy of the table against the same source. */
static const char ALPHABET_N[] =
    "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
    ".-:+=^!/*?`_~()[]{}@%$#";

static int8_t IN_ALPHABET[256];

__attribute__((constructor)) static void init_alphabet(void)
{
    for (const char *p = ALPHABET_N; *p; p++)
        IN_ALPHABET[(unsigned char)*p] = 1;
}

static void die(const char *what)
{
    fprintf(stderr, "fuzz_roundtrip: %s\n", what);
    abort();
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    char *s = NULL;
    size_t sl = 0;
    base85n_status st = base85n_encode(data, size, &s, &sl);
    if (st == BASE85N_ERR_ALLOC)
        return 0; /* the one failure the contract allows */
    if (st != BASE85N_OK)
        die("encode failed on input content");

    for (size_t i = 0; i < sl; i++) {
        if (!IN_ALPHABET[(unsigned char)s[i]])
            die("output contains a character outside Alphabet-N");
    }
    if (s[sl] != '\0')
        die("output is not NUL-terminated at the reported length");

    uint8_t *d = NULL;
    size_t dl = 0;
    st = base85n_decode(s, sl, &d, &dl);
    if (st != BASE85N_OK)
        die("decode rejected the encoder's own output");
    if (dl != size || (size && memcmp(d, data, size) != 0))
        die("round trip changed the data");

    free(s);
    free(d);
    return 0;
}
