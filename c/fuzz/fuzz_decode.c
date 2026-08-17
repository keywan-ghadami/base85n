/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/*
 * fuzz_decode.c - the decoder against input it did not produce.
 *
 * The decoder is the attack surface: its input is whatever arrives.
 * Section 13 of the specification says what it must not do about that,
 * and this checks the three claims a fuzzer can check.
 *
 *   1. It terminates and returns a status. It never reads outside its
 *      input and never overruns its output -- which is what running this
 *      under AddressSanitizer establishes, not the assertions below.
 *   2. Bounded expansion. A 5-character Fill signal yields at most 2048
 *      bytes, which caps the format at about 410:1. A decoder that
 *      allocated on a length field before validating it would break this
 *      long before it crashed.
 *   3. Encode/decode consistency on anything it accepts. Whatever bytes
 *      it produces, re-encoding and decoding them returns those same
 *      bytes.
 *
 * What is deliberately *not* asserted is that re-encoding reproduces the
 * input text. It does not, and should not: the decoder accepts every
 * construct wherever it appears, so a stream that spends block mode where
 * a conforming encoder would have spent Fill decodes correctly and
 * re-encodes to something shorter. Only the final block is required to be
 * canonical (spec 7.5), and the decoder enforces that on its own.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "base85n.h"

/* Spec 13: at most 2048 bytes per 5-character signal. The slack covers a
 * short final group, whose 1 to 4 characters still yield up to 3 bytes. */
#define MAX_EXPANSION 410
#define EXPANSION_SLACK 8

static void die(const char *what)
{
    fprintf(stderr, "fuzz_decode: %s\n", what);
    abort();
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    uint8_t *d = NULL;
    size_t dl = 0;
    base85n_status st = base85n_decode((const char *)data, size, &d, &dl);
    if (st != BASE85N_OK)
        return 0; /* rejecting malformed input is the correct outcome */

    if (dl > (size_t)MAX_EXPANSION * size + EXPANSION_SLACK)
        die("decoded output exceeds the format's expansion bound");

    char *s = NULL;
    size_t sl = 0;
    base85n_status est = base85n_encode(d, dl, &s, &sl);
    if (est == BASE85N_ERR_ALLOC) {
        free(d);
        return 0;
    }
    if (est != BASE85N_OK)
        die("encode failed on bytes the decoder produced");

    uint8_t *d2 = NULL;
    size_t dl2 = 0;
    if (base85n_decode(s, sl, &d2, &dl2) != BASE85N_OK)
        die("decode rejected the re-encoding of its own output");
    if (dl2 != dl || (dl && memcmp(d2, d, dl) != 0))
        die("decode/encode/decode did not reach a fixed point");

    free(s);
    free(d);
    free(d2);
    return 0;
}
