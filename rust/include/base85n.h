/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/*
 * base85n.h - C API for the Rust implementation of Base85N.
 *
 * This header declares the same API as the C implementation's
 * c/include/base85n.h -- same function names, same status codes with the
 * same numeric values, same ownership rules -- but the library behind it
 * is the memory-safe Rust one (rust/src/ffi.rs). The two libraries are
 * interchangeable at the ABI level: code written against either header
 * links against either library.
 *
 * Prefer this one when you decode input you did not produce. The parsing
 * is bounds-checked by the Rust compiler, so the failure modes a
 * hand-written C parser has to be audited for -- out-of-bounds reads,
 * pointer arithmetic overflow, use of an uninitialised length -- are not
 * reachable, whatever the input. See SECURITY.md.
 *
 * Base85N is a Base85 variant using a single 85-character "protocol
 * friendly" alphabet (Alphabet-N), with two adaptive modes on top of the
 * usual 4-byte-to-5-character block expansion.
 *
 * Dynamic Passthrough (DP) represents a run of text-like bytes at exactly
 * one output character per input byte. Its signal names which of the 13
 * substitutable punctuation/whitespace characters (the R-Set) the segment
 * contains, and which donor profile lends the Alphabet-N characters that
 * stand in for them, so the substitution is built per segment and is
 * injective -- DP needs no escape mechanism.
 *
 * Solid Fill represents a run of up to 2048 identical bytes in the five
 * characters of its signal alone.
 *
 * See spec/base85n-v0.4.0.md for the full specification.
 *
 * Building
 * --------
 *   cd rust && cargo build --release
 *
 * produces target/release/libbase85n.so (or .dylib/.dll) and
 * target/release/libbase85n.a. Link either, and add this directory to
 * your include path:
 *
 *   cc app.c -Irust/include rust/target/release/libbase85n.a -lpthread -ldl -lm
 *
 * Ownership / memory model
 * -------------------------
 * Identical to the C implementation's, deliberately.
 *
 *   - base85n_encode() allocates the output string with malloc(). On
 *     BASE85N_OK, the caller owns *out_str and must release it with
 *     free() when done. On any error return, *out_str and *out_len are
 *     left unmodified.
 *
 *   - base85n_decode() allocates the output byte buffer with malloc().
 *     On BASE85N_OK, the caller owns *out_data and must release it with
 *     free() when done. On any error return, *out_data and *out_len are
 *     left unmodified.
 *
 * The output buffers come from the C allocator, not from Rust's, so
 * free() is correct and no library-specific deallocator is needed. The
 * decoded buffer is *not* NUL-terminated; the encoded string is, with
 * the terminator not counted in *out_len.
 *
 * The library keeps no global state and retains no pointer the caller
 * passed in after a call returns. All functions are re-entrant and
 * thread-safe, perform no I/O, and call nothing outside malloc().
 *
 * One behavioural difference from the C library, and it is a difference
 * in what cannot happen rather than in what does: a Rust allocation
 * failure inside the encoder or decoder aborts the process instead of
 * returning BASE85N_ERR_ALLOC, because aborting on allocation failure is
 * Rust's global policy. BASE85N_ERR_ALLOC is still returned when the
 * caller-owned output buffer cannot be allocated.
 */

#ifndef BASE85N_H
#define BASE85N_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Status codes returned by base85n_encode() / base85n_decode(). */
typedef enum {
    BASE85N_OK = 0,

    /* Decoding errors */
    BASE85N_ERR_INVALID_CHAR,           /* Character outside Alphabet-N (and not
                                            allowed inter-token whitespace). Input
                                            that is not valid UTF-8 is reported
                                            here too: every Alphabet-N character
                                            is ASCII, so such a byte is an invalid
                                            character either way. */
    BASE85N_ERR_UNEXPECTED_EOF,         /* Stream ended mid-group, mid-signal, or
                                            before a DP segment's declared length
                                            of data was available. */
    BASE85N_ERR_UNDEFINED_SIGNAL,       /* A group's value fell in
                                            FUTURE_SIGNAL_SPACE, above every
                                            signal this version defines. */
    BASE85N_ERR_INVALID_FINAL_BLOCK,    /* A malformed trailing group: a lone
                                            character, a padded value that does
                                            not fit in 32 bits, or one that is
                                            not the canonical encoding of the
                                            bytes it decodes to. */
    BASE85N_ERR_ALLOC,                  /* malloc failure for the output buffer. */
    BASE85N_ERR_INVALID_ARGUMENT        /* NULL out pointer, or s == NULL with
                                            s_len != 0, etc. */
} base85n_status;

/*
 * base85n_encode - encode raw bytes into a Base85N string.
 *
 * data:     pointer to data_len bytes of input. May be NULL only if
 *           data_len == 0.
 * data_len: number of input bytes.
 * out_str:  on BASE85N_OK, receives a newly malloc'd, NUL-terminated
 *           buffer holding the encoded string. Caller must free() it.
 * out_len:  on BASE85N_OK, receives the length of *out_str in bytes,
 *           excluding the terminating NUL.
 *
 * Returns BASE85N_OK on success. Encoding never fails on the content of
 * its input, since every byte value is representable in Block Mode; the
 * only failures are BASE85N_ERR_ALLOC and BASE85N_ERR_INVALID_ARGUMENT.
 */
base85n_status base85n_encode(const uint8_t *data, size_t data_len,
                               char **out_str, size_t *out_len);

/*
 * base85n_decode - decode a Base85N string back into raw bytes.
 *
 * s:        pointer to s_len characters of Base85N-encoded text. May be
 *           NULL only if s_len == 0. Does not need to be NUL-terminated;
 *           exactly s_len characters are consumed.
 * out_data: on BASE85N_OK, receives a newly malloc'd buffer holding the
 *           decoded bytes. Caller must free() it. When the decoded
 *           length is 0, a valid non-NULL pointer is still returned
 *           (suitable for free()).
 * out_len:  on BASE85N_OK, receives the number of decoded bytes.
 *
 * Returns BASE85N_OK on success, or a specific base85n_status error
 * code describing the first problem encountered. On error, *out_data
 * and *out_len are left unmodified.
 */
base85n_status base85n_decode(const char *s, size_t s_len,
                               uint8_t **out_data, size_t *out_len);

/* Returns a short, static, human-readable description of a status code.
 * The pointer is to static storage and must not be freed. */
const char *base85n_strerror(base85n_status status);

/*
 * Algorithm constants from the Base85N specification (spec section
 * 6.4), exposed for callers / tests that want to construct inputs that
 * straddle these boundaries without hardcoding magic numbers.
 */
#define BASE85N_NUM_PROFILES                   8
#define BASE85N_RSET_LEN                       13
#define BASE85N_MAX_DP_ANALYSIS_BYTES          2048
#define BASE85N_MAX_DP_SEGMENT_CHARS           2048
#define BASE85N_MIN_PASSTHROUGH_BYTES          20
#define BASE85N_MIN_FILL_BYTES                 5
#define BASE85N_MIN_FILL_IN_SEGMENT_BYTES      11
#define BASE85N_MAX_FILL_BYTES                 2048

#ifdef __cplusplus
}
#endif

#endif /* BASE85N_H */
