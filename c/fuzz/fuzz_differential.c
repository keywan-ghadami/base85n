/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/*
 * fuzz_differential.c - the C and Rust implementations against each other,
 * in one process, on generated input.
 *
 * The format admits exactly one encoding of any input (spec 6.5), and the
 * four implementations in this repository are held to it by the shared test
 * vectors and by a generated differential corpus. Both are fixed sets. This
 * is the same check with a fuzzer choosing the inputs, which is the gap
 * SECURITY.md records as "no differential fuzzing between implementations".
 *
 * It is possible in one process because the Rust crate exports the same C
 * ABI, function for function and status code for status code. The build
 * renames its symbols to `rs_*` so both can be linked at once; see the
 * Makefile.
 *
 * Two properties, in both directions:
 *
 *   - Encoding agrees character for character. A disagreement is a
 *     wire-format divergence between two implementations of a frozen
 *     specification, which is the most serious defect this repository can
 *     have.
 *   - Decoding agrees on the verdict and, when it is OK, on the bytes. The
 *     error *code* is part of the agreement too: spec section 10 names which
 *     condition each malformed stream falls under, so two implementations
 *     disagreeing about which one is a specification bug even though neither
 *     accepted bad data.
 *
 * The fuzzer's input is used twice: once as bytes to encode, and once as
 * text to decode. The second is what reaches the decoders' error paths,
 * since almost nothing a fuzzer generates is a valid stream.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "base85n.h"

/* The Rust crate's C ABI, renamed at link time. */
base85n_status rs_base85n_encode(const uint8_t *data, size_t data_len,
                                 char **out_str, size_t *out_len);
base85n_status rs_base85n_decode(const char *s, size_t s_len,
                                 uint8_t **out_data, size_t *out_len);

static void die(const char *what, base85n_status a, base85n_status b)
{
    fprintf(stderr, "fuzz_differential: %s (C=%d, Rust=%d)\n", what,
            (int)a, (int)b);
    abort();
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    /* Encode: the two must produce the same characters. */
    char *ca = NULL, *ra = NULL;
    size_t cl = 0, rl = 0;
    base85n_status cs = base85n_encode(data, size, &ca, &cl);
    base85n_status rs = rs_base85n_encode(data, size, &ra, &rl);
    if (cs != rs)
        die("encode status differs", cs, rs);
    if (cs == BASE85N_OK) {
        if (cl != rl || (cl && memcmp(ca, ra, cl) != 0))
            die("encode output differs", cs, rs);
        free(ca);
        free(ra);
    }

    /* Decode: same verdict, and the same bytes when that verdict is OK. */
    uint8_t *cd = NULL, *rd = NULL;
    size_t cdl = 0, rdl = 0;
    cs = base85n_decode((const char *)data, size, &cd, &cdl);
    rs = rs_base85n_decode((const char *)data, size, &rd, &rdl);
    if (cs != rs)
        die("decode status differs", cs, rs);
    if (cs == BASE85N_OK) {
        if (cdl != rdl || (cdl && memcmp(cd, rd, cdl) != 0))
            die("decode output differs", cs, rs);
        free(cd);
        free(rd);
    }
    return 0;
}
