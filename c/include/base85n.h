/*
 * base85n.h - Public API for the Base85N binary-to-text encoding library.
 *
 * Base85N is a Base85 variant using a single 85-character "protocol
 * friendly" alphabet (Alphabet-N), with an optional Dynamic Passthrough
 * (DP) mode that can represent runs of "safe" bytes (including a small
 * set of substitutable punctuation/whitespace characters, the R-Set)
 * with near 1:1 overhead instead of the usual 4-byte-to-5-character
 * block expansion. See spec/base85n-v0.1.0.md for the full
 * specification.
 *
 * Ownership / memory model
 * -------------------------
 * This library never keeps hidden global state and never retains
 * pointers to caller-owned memory after a call returns.
 *
 *   - base85n_encode() allocates the output string with malloc(). On
 *     BASE85N_OK, the caller owns *out_str and must release it with
 *     free() when done. On any error return, *out_str and *out_len are
 *     left unset (the function performs no partial allocation the
 *     caller needs to clean up).
 *
 *   - base85n_decode() allocates the output byte buffer with malloc().
 *     On BASE85N_OK, the caller owns *out_data and must release it with
 *     free() when done. On any error return, *out_data and *out_len are
 *     left unset.
 *
 * All functions are re-entrant and thread-safe (no shared mutable
 * state); the library performs no I/O and calls no functions other
 * than the standard C library's malloc/realloc/free.
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
                                            allowed inter-token whitespace). */
    BASE85N_ERR_UNEXPECTED_EOF,         /* Stream ended mid-group, mid-signal, or
                                            before a DP segment's declared length
                                            of data was available. */
    BASE85N_ERR_DANGLING_ESCAPE,        /* A '~' was the last character of a DP
                                            segment's transformed data. */
    BASE85N_ERR_RESERVED_SIGNAL,        /* decodedValue >= 2^32 but SignalPayload
                                            (decodedValue - 2^32) > 2^22 - 1. */
    BASE85N_ERR_INVALID_PARTIAL_BLOCK,  /* A malformed / out-of-place partial
                                            trailing group (e.g. a lone 1-character
                                            trailing group, which cannot occur in a
                                            valid stream). */
    BASE85N_ERR_ALLOC,                  /* malloc/realloc failure. */
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
 * Returns BASE85N_OK on success. The only failure mode for encoding
 * (which never fails on the content of its input, since every byte
 * value is representable in Block Mode) is BASE85N_ERR_ALLOC on
 * allocation failure, or BASE85N_ERR_INVALID_ARGUMENT for bad
 * arguments.
 */
base85n_status base85n_encode(const uint8_t *data, size_t data_len,
                               char **out_str, size_t *out_len);

/*
 * base85n_decode - decode a Base85N string back into raw bytes.
 *
 * s:        pointer to s_len characters of Base85N-encoded text. May be
 *           NULL only if s_len == 0. Does not need to be NUL-terminated;
 *           exactly s_len characters are consumed.
 * s_len:    number of input characters.
 * out_data: on BASE85N_OK, receives a newly malloc'd buffer holding the
 *           decoded bytes. Caller must free() it. When the decoded
 *           length is 0, a valid non-NULL 0-byte-usable pointer is
 *           still returned (suitable for free()).
 * out_len:  on BASE85N_OK, receives the number of decoded bytes.
 *
 * Returns BASE85N_OK on success, or a specific base85n_status error
 * code describing the first problem encountered. On error, *out_data
 * and *out_len are left unmodified.
 */
base85n_status base85n_decode(const char *s, size_t s_len,
                               uint8_t **out_data, size_t *out_len);

/* Returns a short, static, human-readable description of a status code. */
const char *base85n_strerror(base85n_status status);

/*
 * Algorithm constants from the Base85N specification (spec section
 * 6.4), exposed for callers / tests that want to construct inputs that
 * straddle these boundaries without hardcoding magic numbers.
 */
#define BASE85N_MAX_CONSECUTIVE_ESCAPES        3
#define BASE85N_MAX_DP_OUTPUT_CHARS_PER_SIGNAL 511
#define BASE85N_MIN_PASSTHROUGH_BYTES          20

#ifdef __cplusplus
}
#endif

#endif /* BASE85N_H */
