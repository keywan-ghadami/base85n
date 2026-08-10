/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/*
 * base85n.c - Implementation of the Base85N binary-to-text encoding
 * scheme, per the spec, including Section 6.1's two-pass ("Pass 1"
 * window/mask discovery, "Pass 2" boundary finalization) Dynamic
 * Passthrough procedure.
 */

#include "base85n.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Constants                                                           */
/* ------------------------------------------------------------------ */

#define ALPHABET_SIZE 85

static const char ALPHABET_N_CHARS_STR[ALPHABET_SIZE + 1] =
    "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
    ".-:+=^!/*?`_~()[]{}@%$#";

#define RSET_COUNT 13

/* R-Set ASCII values, indexed by R-Set index j (spec 4.1). */
static const uint8_t RSET_ASCII[RSET_COUNT] = {
    32, /* 0  space */
    34, /* 1  "     */
    39, /* 2  '     */
    44, /* 3  ,     */
    59, /* 4  ;     */
    92, /* 5  \     */
    124,/* 6  |     */
    60, /* 7  <     */
    62, /* 8  >     */
    38, /* 9  &     */
    9,  /* 10 \t    */
    10, /* 11 \n    */
    13  /* 12 \r    */
};

/* allowedPassthroughSafeReplacementCharacters[j] (spec 4.2). */
static const char REPLACEMENT_CHARS[RSET_COUNT] = {
    ':', '+', '=', '^', '!', '/', '*', '?', '`', '(', ')', '[', ']'
};

#define ESCAPE_CHAR '~'

#define MAX_CONSECUTIVE_ESCAPES BASE85N_MAX_CONSECUTIVE_ESCAPES
#define MAX_DP_OUTPUT_CHARS_PER_SIGNAL BASE85N_MAX_DP_OUTPUT_CHARS_PER_SIGNAL
#define MIN_PASSTHROUGH_BYTES BASE85N_MIN_PASSTHROUGH_BYTES

#define POW2_32 ((uint64_t)1u << 32)
#define SIGNAL_PAYLOAD_MAX ((uint64_t)(1u << 22) - 1u) /* 2^22 - 1 */

/* Reverse lookup: ASCII byte value -> Alphabet-N digit value (0-84), or
 * -1 if that byte is not part of Alphabet-N. Built once at first use. */
static int8_t g_alphabet_value[256];
static int g_alphabet_value_ready = 0;

static void ensure_alphabet_table(void) {
    if (g_alphabet_value_ready) return;
    for (int i = 0; i < 256; i++) g_alphabet_value[i] = -1;
    for (int v = 0; v < ALPHABET_SIZE; v++) {
        unsigned char c = (unsigned char)ALPHABET_N_CHARS_STR[v];
        g_alphabet_value[c] = (int8_t)v;
    }
    g_alphabet_value_ready = 1;
}

static int alphabet_value(unsigned char c) {
    ensure_alphabet_table();
    return g_alphabet_value[c];
}

static int rset_index_for_byte(uint8_t b) {
    for (int j = 0; j < RSET_COUNT; j++) {
        if (RSET_ASCII[j] == b) return j;
    }
    return -1;
}

static int replacement_index_for_char(unsigned char c) {
    for (int j = 0; j < RSET_COUNT; j++) {
        if ((unsigned char)REPLACEMENT_CHARS[j] == c) return j;
    }
    return -1;
}

static int is_ignorable_ws(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

/* ------------------------------------------------------------------ */
/* Growable byte buffer                                                */
/* ------------------------------------------------------------------ */

typedef struct {
    uint8_t *data;
    size_t len;
    size_t cap;
} byte_buf;

static void bb_init(byte_buf *b) {
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

static void bb_free(byte_buf *b) {
    free(b->data);
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

/* Ensures room for at least `extra` more bytes; returns 0 on success,
 * -1 on allocation failure (buffer left unmodified). */
static int bb_reserve(byte_buf *b, size_t extra) {
    if (b->cap - b->len >= extra) return 0;
    size_t need = b->len + extra;
    size_t newcap = b->cap ? b->cap : 64;
    while (newcap < need) {
        if (newcap > (SIZE_MAX / 2)) { newcap = need; break; }
        newcap *= 2;
    }
    uint8_t *p = (uint8_t *)realloc(b->data, newcap);
    if (!p) return -1;
    b->data = p;
    b->cap = newcap;
    return 0;
}

static int bb_push(byte_buf *b, uint8_t byte) {
    if (bb_reserve(b, 1) != 0) return -1;
    b->data[b->len++] = byte;
    return 0;
}

static int bb_push_n(byte_buf *b, const uint8_t *bytes, size_t n) {
    if (n == 0) return 0;
    if (bb_reserve(b, n) != 0) return -1;
    memcpy(b->data + b->len, bytes, n);
    b->len += n;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Base85 digit <-> value conversion (spec section 8)             */
/* ------------------------------------------------------------------ */

/* Converts a value (0 .. 85^5 - 1) into 5 Alphabet-N characters,
 * Big-Endian digit order (most significant digit first). */
static void value_to_5chars(uint64_t value, char out[5]) {
    uint8_t digits[5];
    for (int i = 4; i >= 0; i--) {
        digits[i] = (uint8_t)(value % 85);
        value /= 85;
    }
    for (int i = 0; i < 5; i++) {
        out[i] = ALPHABET_N_CHARS_STR[digits[i]];
    }
}

/* ------------------------------------------------------------------ */
/* Encoding                                                             */
/* ------------------------------------------------------------------ */

/* Section 6.2: ProcessWithBlockMode. Encodes `n` bytes of `data`
 * starting at full 4-byte blocks; if n is not a multiple of 4, the
 * trailing 1-3 bytes are encoded as a padded partial group per the
 * spec. Appends the resulting Alphabet-N characters to `out`. */
static int process_block_mode(const uint8_t *data, size_t n, byte_buf *out) {
    size_t full_blocks = n / 4;
    for (size_t k = 0; k < full_blocks; k++) {
        const uint8_t *p = data + 4 * k;
        uint32_t val = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
                        ((uint32_t)p[2] << 8) | (uint32_t)p[3];
        char chars[5];
        value_to_5chars(val, chars);
        if (bb_push_n(out, (const uint8_t *)chars, 5) != 0) return -1;
    }
    size_t rem = n % 4;
    if (rem > 0) {
        uint8_t block[4] = {0, 0, 0, 0};
        memcpy(block, data + 4 * full_blocks, rem);
        uint32_t val = ((uint32_t)block[0] << 24) | ((uint32_t)block[1] << 16) |
                        ((uint32_t)block[2] << 8) | (uint32_t)block[3];
        char chars[5];
        value_to_5chars(val, chars);
        /* Take the first rem+1 characters. */
        if (bb_push_n(out, (const uint8_t *)chars, rem + 1) != 0) return -1;
    }
    return 0;
}

/* Pass 1 -- Window and Mask Discovery (spec 6.1, step 1.a), scanned once
 * per representable run rather than once per iteration of the main loop.
 *
 * Bounded *only* by representability: a byte belongs to the run if it is
 * an R-Set character, or chr(byte) is in ALPHABET_N_CHARS_STR (which
 * includes the escape char and all replacement chars unconditionally,
 * regardless of escaping cost). Never terminates on account of escaping
 * cost or the consecutive-escape count.
 *
 * `counts[j]` is the number of occurrences of R_Char[j] in the part of the
 * run that has not been consumed yet; window_mask is simply the set of j
 * with counts[j] > 0. Maintaining the counts incrementally is what makes
 * the encoder linear (spec 6.6): Pass 1's window for a position deeper
 * inside the same run is a suffix of this one, so rescanning it -- which
 * is what a literal reading of 6.1 does -- is redundant and quadratic. */
typedef struct {
    size_t counts[RSET_COUNT];
    uint16_t mask; /* bit j set iff counts[j] != 0 */
    size_t end;    /* offset into data, exclusive, where the run ends */
} run_state;

static void run_scan(const uint8_t *data, size_t data_len, size_t pos,
                     run_state *st) {
    size_t i;
    /* Only counters flagged in mask can still be non-zero, so clearing just
     * those restores the all-zero invariant. Most runs in binary input carry
     * no R-Set character at all, making this loop iterate zero times. */
    uint16_t stale = st->mask;
    for (int j = 0; stale; j++, stale = (uint16_t)(stale >> 1)) {
        if (stale & 1u) st->counts[j] = 0;
    }
    st->mask = 0;

    for (i = pos; i < data_len; i++) {
        uint8_t b = data[i];
        int j = rset_index_for_byte(b);
        if (j >= 0) {
            st->counts[j]++;
            st->mask = (uint16_t)(st->mask | (1u << j));
            continue;
        }
        if (alphabet_value(b) >= 0) {
            continue;
        }
        break; /* unrepresentable byte: the run ends here */
    }
    st->end = i;
}

/* Retire the bytes data[from..to) from the run's counts, clearing a mask
 * bit as soon as its last occurrence is consumed. Maintaining the mask here
 * keeps it a plain field read in the encoding loop. */
static void run_consume(const uint8_t *data, size_t from, size_t to,
                        run_state *st) {
    for (size_t i = from; i < to; i++) {
        int j = rset_index_for_byte(data[i]);
        if (j >= 0 && --st->counts[j] == 0) {
            st->mask = (uint16_t)(st->mask & ~(1u << j));
        }
    }
}

/* Pass 2 -- Boundary Finalization with Fixed Mask (spec 6.1, step
 * 1.b). Re-scans `window` (buf[0..window_len)) byte-by-byte against the
 * *fixed* final_mask (== window_mask from Pass 1, never modified here),
 * applying Case i/ii/iii and the consecutive-escape limit to determine
 * how many leading bytes of window form dp_candidate_prefix. Builds the
 * transformed output text plus, in parallel, `piece_lens[i]` = the
 * number of output characters (1 or 2) contributed by candidate byte i,
 * so that segmentation (spec 6.1, step 1.d) can later split only
 * at whole-piece boundaries. `piece_lens` must have room for window_len
 * entries. Returns 0 on success, -1 on allocation failure. */
static int dp_pass2(const uint8_t *buf, size_t window_len, uint16_t final_mask,
                     size_t *out_candidate_len, byte_buf *out_transformed,
                     uint8_t *piece_lens) {
    int consecutive_escape_trigger_count = 0;
    size_t i;
    for (i = 0; i < window_len; i++) {
        uint8_t b = buf[i];

        int j = rset_index_for_byte(b);
        if (j >= 0) {
            /* Case i: R-Set character. final_mask is guaranteed to have
             * bit j set, since Pass 1 always sets it for any R-Set byte
             * included in window, and bits never clear afterward. */
            if (bb_push(out_transformed, (uint8_t)REPLACEMENT_CHARS[j]) != 0) return -1;
            piece_lens[i] = 1;
            consecutive_escape_trigger_count = 0;
            continue;
        }

        int rj = replacement_index_for_char(b);
        int needs_escape = 0;
        if (b == ESCAPE_CHAR) {
            needs_escape = 1;
        } else if (rj >= 0 && (final_mask & (uint16_t)(1u << rj)) != 0) {
            needs_escape = 1;
        }

        if (needs_escape) {
            /* Case ii: requires escaping, against the fixed final_mask. */
            consecutive_escape_trigger_count++;
            if (consecutive_escape_trigger_count > MAX_CONSECUTIVE_ESCAPES) {
                break; /* terminate scan; b and the rest of window excluded */
            }
            if (bb_push(out_transformed, (uint8_t)ESCAPE_CHAR) != 0) return -1;
            if (bb_push(out_transformed, b) != 0) return -1;
            piece_lens[i] = 2;
            continue;
        }

        /* Case iii: plain literal (window guarantees representability). */
        if (bb_push(out_transformed, b) != 0) return -1;
        piece_lens[i] = 1;
        consecutive_escape_trigger_count = 0;
    }
    *out_candidate_len = i;
    return 0;
}

/* DP Output Segmentation (spec 6.1, step 1.d): packs `transformed`
 * (whose per-source-byte piece lengths are `piece_lens[0..candidate_len)`,
 * summing to transformed_len) greedily into segments of at most
 * MAX_DP_OUTPUT_CHARS_PER_SIGNAL characters each, closing the current
 * segment *before* adding a piece that would push it over the limit --
 * so a segment boundary never falls inside a Case ii 2-character escape
 * pair. Emits each segment as a 5-character signal (spec section 9)
 * followed by its characters. */
static int emit_dp_segments(const uint8_t *transformed, size_t transformed_len,
                             const uint8_t *piece_lens, size_t candidate_len,
                             uint16_t final_mask, byte_buf *out) {
    size_t char_off = 0;   /* offset into transformed already emitted */
    size_t seg_start = 0;  /* offset into transformed where current segment starts */
    size_t piece_idx = 0;

    while (piece_idx < candidate_len) {
        size_t seg_len = char_off - seg_start;
        size_t piece = piece_lens[piece_idx];
        if (seg_len + piece > MAX_DP_OUTPUT_CHARS_PER_SIGNAL && seg_len > 0) {
            uint64_t payload = ((uint64_t)final_mask << 9) | (uint64_t)seg_len;
            uint64_t value = POW2_32 + payload;
            char sig[5];
            value_to_5chars(value, sig);
            if (bb_push_n(out, (const uint8_t *)sig, 5) != 0) return -1;
            if (bb_push_n(out, transformed + seg_start, seg_len) != 0) return -1;
            seg_start = char_off;
        }
        char_off += piece;
        piece_idx++;
    }
    size_t seg_len = char_off - seg_start;
    if (seg_len > 0) {
        uint64_t payload = ((uint64_t)final_mask << 9) | (uint64_t)seg_len;
        uint64_t value = POW2_32 + payload;
        char sig[5];
        value_to_5chars(value, sig);
        if (bb_push_n(out, (const uint8_t *)sig, 5) != 0) return -1;
        if (bb_push_n(out, transformed + seg_start, seg_len) != 0) return -1;
    }
    (void)transformed_len;
    return 0;
}

base85n_status base85n_encode(const uint8_t *data, size_t data_len,
                               char **out_str, size_t *out_len) {
    if (!out_str || !out_len) return BASE85N_ERR_INVALID_ARGUMENT;
    if (!data && data_len != 0) return BASE85N_ERR_INVALID_ARGUMENT;

    byte_buf out;
    bb_init(&out);

    size_t off = 0; /* current front of intermediate_buffer within data */
    base85n_status status = BASE85N_OK;

    /* Reused across iterations so a long run costs one allocation, not one
     * per iteration. */
    uint8_t *piece_lens = NULL;
    size_t piece_cap = 0;

    /* State of the representable run currently being consumed. run.end == 0
     * with off == 0 forces the first scan. */
    run_state run;
    run.end = 0;
    run.mask = 0;
    for (int j = 0; j < RSET_COUNT; j++) run.counts[j] = 0;

    while (off < data_len) {
        const uint8_t *buf = data + off;
        size_t buf_len = data_len - off;

        if (off >= run.end) {
            /* Entering a run that has not been scanned yet. The final
             * block-mode branch below ignores representability and can step
             * past run.end, in which case we land in a later run and scan
             * that one; runs handled this way are disjoint, so total
             * scanning work stays O(data_len). */
            run_scan(data, data_len, off, &run);
        }
        size_t window_len = run.end - off;
        uint16_t final_mask = run.mask;

        size_t candidate_len = 0;
        int use_dp_mode = 0;
        byte_buf transformed;
        bb_init(&transformed);

        if (window_len > 0) {
            if (window_len > piece_cap) {
                uint8_t *grown = (uint8_t *)realloc(piece_lens, window_len);
                if (!grown) {
                    status = BASE85N_ERR_ALLOC;
                    bb_free(&transformed);
                    break;
                }
                piece_lens = grown;
                piece_cap = window_len;
            }
            if (dp_pass2(buf, window_len, final_mask, &candidate_len, &transformed, piece_lens) != 0) {
                status = BASE85N_ERR_ALLOC;
                bb_free(&transformed);
                break;
            }
            if (candidate_len >= MIN_PASSTHROUGH_BYTES) {
                size_t l_actual = transformed.len;
                /* Number of segments the escape-pair-safe greedy packing
                 * (spec 6.1, step 1.d) actually produces -- not the
                 * naive ceil(L/511) estimate, which can undercount by one
                 * when a segment would otherwise have to split a pair. */
                size_t num_segments = 0;
                {
                    size_t char_off = 0, seg_start = 0;
                    for (size_t k = 0; k < candidate_len; k++) {
                        size_t seg_len = char_off - seg_start;
                        size_t piece = piece_lens[k];
                        if (seg_len + piece > MAX_DP_OUTPUT_CHARS_PER_SIGNAL && seg_len > 0) {
                            num_segments++;
                            seg_start = char_off;
                        }
                        char_off += piece;
                    }
                    if (char_off > seg_start) num_segments++;
                }
                size_t conceptual_dp_output_length = num_segments * 5 + l_actual;
                size_t block_mode_output_length = ((candidate_len + 3) / 4) * 5;
                if (conceptual_dp_output_length <= block_mode_output_length) {
                    use_dp_mode = 1;
                }
            }
        }

        size_t consumed;
        if (use_dp_mode) {
            int rc = emit_dp_segments(transformed.data, transformed.len, piece_lens,
                                       candidate_len, final_mask, &out);
            bb_free(&transformed);
            if (rc != 0) {
                status = BASE85N_ERR_ALLOC;
                break;
            }
            consumed = candidate_len;
        } else {
            bb_free(&transformed);

            /* spec section 6.1, step 2.b: block-encode only the exact
             * multiple-of-4 leading portion of dp_candidate_prefix now; any
             * 0-3 trailing bytes are deferred, unpadded, to the next loop
             * iteration rather than treated as a premature partial block. */
            if (candidate_len >= 4) {
                consumed = (candidate_len / 4) * 4;
            } else {
                /* Fewer than 4 candidate bytes. This is the branch that can
                 * consume past the end of the current run. */
                consumed = buf_len < 4 ? buf_len : 4;
            }
            if (process_block_mode(buf, consumed, &out) != 0) {
                status = BASE85N_ERR_ALLOC;
                break;
            }
        }

        if (off + consumed < run.end) {
            /* Still inside the same run: retire the consumed bytes so the
             * next iteration's mask covers exactly the remainder. */
            run_consume(data, off, off + consumed, &run);
        }
        off += consumed;
    }

    free(piece_lens);

    if (status != BASE85N_OK) {
        bb_free(&out);
        return status;
    }

    /* NUL-terminate. */
    if (bb_push(&out, 0) != 0) {
        bb_free(&out);
        return BASE85N_ERR_ALLOC;
    }

    *out_len = out.len - 1;
    *out_str = (char *)out.data;
    return BASE85N_OK;
}

/* ------------------------------------------------------------------ */
/* Decoding                                                             */
/* ------------------------------------------------------------------ */

base85n_status base85n_decode(const char *s, size_t s_len,
                               uint8_t **out_data, size_t *out_len) {
    if (!out_data || !out_len) return BASE85N_ERR_INVALID_ARGUMENT;
    if (!s && s_len != 0) return BASE85N_ERR_INVALID_ARGUMENT;

    /* Filter out ignorable inter-token whitespace (spec 7.1). None
     * of the four whitespace bytes ever appear as meaningful content in
     * a valid Base85N stream (they are not in Alphabet-N and R-Set
     * occurrences are always substituted away in DP output), so a
     * single global strip is equivalent to stripping only "between
     * tokens" as literally worded. */
    uint8_t *clean = NULL;
    size_t n = 0;
    if (s_len > 0) {
        clean = (uint8_t *)malloc(s_len);
        if (!clean) return BASE85N_ERR_ALLOC;
        for (size_t i = 0; i < s_len; i++) {
            unsigned char c = (unsigned char)s[i];
            if (is_ignorable_ws(c)) continue;
            clean[n++] = c;
        }
    }

    byte_buf out;
    bb_init(&out);
    base85n_status status = BASE85N_OK;
    size_t pos = 0;

    while (pos < n) {
        size_t remaining = n - pos;

        if (remaining >= 5) {
            int vals[5];
            for (int k = 0; k < 5; k++) {
                int v = alphabet_value(clean[pos + k]);
                if (v < 0) { status = BASE85N_ERR_INVALID_CHAR; goto done; }
                vals[k] = v;
            }
            uint64_t decoded_value = 0;
            for (int k = 0; k < 5; k++) decoded_value = decoded_value * 85 + (uint64_t)vals[k];
            pos += 5;

            if (decoded_value < POW2_32) {
                /* Standard Base85N block: 4 bytes, Big-Endian. */
                uint32_t v32 = (uint32_t)decoded_value;
                uint8_t bytes[4] = {
                    (uint8_t)(v32 >> 24), (uint8_t)(v32 >> 16),
                    (uint8_t)(v32 >> 8), (uint8_t)v32
                };
                if (bb_push_n(&out, bytes, 4) != 0) { status = BASE85N_ERR_ALLOC; goto done; }
            } else {
                uint64_t signal_payload = decoded_value - POW2_32;
                if (signal_payload > SIGNAL_PAYLOAD_MAX) {
                    status = BASE85N_ERR_RESERVED_SIGNAL;
                    goto done;
                }
                uint16_t mask13 = (uint16_t)((signal_payload >> 9) & 0x1FFFu);
                size_t l_enc = (size_t)(signal_payload & 0x1FFu);

                if (n - pos < l_enc) { status = BASE85N_ERR_UNEXPECTED_EOF; goto done; }

                size_t idx = 0;
                while (idx < l_enc) {
                    unsigned char c1 = clean[pos + idx];
                    if (alphabet_value(c1) < 0) { status = BASE85N_ERR_INVALID_CHAR; goto done; }
                    if (c1 == ESCAPE_CHAR) {
                        idx++;
                        if (idx >= l_enc) { status = BASE85N_ERR_DANGLING_ESCAPE; goto done; }
                        unsigned char c2 = clean[pos + idx];
                        if (alphabet_value(c2) < 0) { status = BASE85N_ERR_INVALID_CHAR; goto done; }
                        if (bb_push(&out, c2) != 0) { status = BASE85N_ERR_ALLOC; goto done; }
                        idx++;
                        continue;
                    }
                    int rj = replacement_index_for_char(c1);
                    if (rj >= 0 && (mask13 & (uint16_t)(1u << rj)) != 0) {
                        if (bb_push(&out, RSET_ASCII[rj]) != 0) { status = BASE85N_ERR_ALLOC; goto done; }
                    } else {
                        if (bb_push(&out, c1) != 0) { status = BASE85N_ERR_ALLOC; goto done; }
                    }
                    idx++;
                }
                pos += l_enc;
            }
        } else if (remaining == 1) {
            /* A lone trailing Alphabet-N character cannot be a valid
             * partial block (minimum 2 chars needed to represent 1
             * original byte). */
            status = BASE85N_ERR_INVALID_PARTIAL_BLOCK;
            goto done;
        } else {
            /* remaining is 2, 3, or 4: final partial block. */
            int vals[5];
            for (size_t k = 0; k < remaining; k++) {
                int v = alphabet_value(clean[pos + k]);
                if (v < 0) { status = BASE85N_ERR_INVALID_CHAR; goto done; }
                vals[k] = v;
            }
            for (size_t k = remaining; k < 5; k++) vals[k] = 84; /* pad with '#' */

            uint64_t decoded_value = 0;
            for (int k = 0; k < 5; k++) decoded_value = decoded_value * 85 + (uint64_t)vals[k];
            uint32_t v32 = (uint32_t)(decoded_value & 0xFFFFFFFFu);
            uint8_t bytes[4] = {
                (uint8_t)(v32 >> 24), (uint8_t)(v32 >> 16),
                (uint8_t)(v32 >> 8), (uint8_t)v32
            };
            size_t nbytes = remaining - 1; /* 1, 2, or 3 */
            if (bb_push_n(&out, bytes, nbytes) != 0) { status = BASE85N_ERR_ALLOC; goto done; }
            pos += remaining;
        }
    }

done:
    free(clean);
    if (status != BASE85N_OK) {
        bb_free(&out);
        return status;
    }

    if (out.data == NULL) {
        /* 0-length result: still hand back a valid, free()-able pointer. */
        uint8_t *empty = (uint8_t *)malloc(1);
        if (!empty) return BASE85N_ERR_ALLOC;
        *out_data = empty;
        *out_len = 0;
        return BASE85N_OK;
    }

    *out_data = out.data;
    *out_len = out.len;
    return BASE85N_OK;
}

/* ------------------------------------------------------------------ */
/* Misc                                                                 */
/* ------------------------------------------------------------------ */

const char *base85n_strerror(base85n_status status) {
    switch (status) {
        case BASE85N_OK: return "ok";
        case BASE85N_ERR_INVALID_CHAR: return "invalid character (not in Alphabet-N)";
        case BASE85N_ERR_UNEXPECTED_EOF: return "unexpected end of stream";
        case BASE85N_ERR_DANGLING_ESCAPE: return "dangling escape character";
        case BASE85N_ERR_RESERVED_SIGNAL: return "reserved/undefined DP signal value";
        case BASE85N_ERR_INVALID_PARTIAL_BLOCK: return "invalid partial final block";
        case BASE85N_ERR_ALLOC: return "memory allocation failure";
        case BASE85N_ERR_INVALID_ARGUMENT: return "invalid argument";
        default: return "unknown error";
    }
}
