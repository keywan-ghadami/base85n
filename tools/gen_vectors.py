#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Regenerate the shared test vectors in ``testvectors/``.

The golden vectors are the Python reference implementation's output for a
fixed, deterministic set of inputs; the adversarial vectors are hand-built
byte sequences that a decoder must reject (or accept with a stated result).
Both sets are written in a ``.json`` and a ``.tsv`` form carrying identical
data, which ``tools/check_vectors.py`` verifies.

Run from anywhere: ``python3 tools/gen_vectors.py``. It rewrites the four
files in place; ``check_vectors.py`` should pass immediately afterwards.
"""

from __future__ import annotations

import json
import os
import random
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VECTOR_DIR = os.path.join(REPO_ROOT, "testvectors")
sys.path.insert(0, os.path.join(REPO_ROOT, "python", "src"))

from base85n import (  # noqa: E402  (path set up above)
    ALPHABET_N_CHARS_STR,
    MIN_PASSTHROUGH_BYTES,
    REPLACEMENT_ALPHABETS,
    encode,
)

_RSET_ASCII = (32, 34, 39, 44, 59, 92, 124, 60, 62, 38, 9, 10, 13)
_BLOCK_SIGNAL_BASE = 1 << 32


def signal(alphabet: int, length: int) -> str:
    """The 5-character DP signal for an alphabet and a segment length.

    ``length`` is the real character count (1..1024); Section 9 stores it
    biased by one.
    """
    payload = (alphabet << 10) | (length - 1)
    return raw_signal(_BLOCK_SIGNAL_BASE + payload)


def raw_signal(value: int) -> str:
    digits = []
    for _ in range(5):
        digits.append(ALPHABET_N_CHARS_STR[value % 85])
        value //= 85
    return "".join(reversed(digits))


# ---------------------------------------------------------------------
# Golden vectors
# ---------------------------------------------------------------------


def golden_inputs() -> list[tuple[str, bytes]]:
    out: list[tuple[str, bytes]] = []
    add = lambda name, data: out.append((name, data))  # noqa: E731

    add("empty", b"")
    for n in range(1, 9):
        add("literal_len_%d" % n, b"abcdefgh"[:n])

    add("literal_19_below_min", b"a" * 19)
    add("literal_exactly_20", b"a" * MIN_PASSTHROUGH_BYTES)
    add("literal_21_above_min", b"a" * 21)
    add("literal_long_600", b"abcdefghij" * 60)

    # MAX_DP_ANALYSIS_BYTES boundary: 1024 fits one segment, 1025 needs two.
    add("dp_segment_1023", b"x" * 1023)
    add("dp_segment_exactly_1024", b"x" * 1024)
    add("dp_segment_1025_needs_two", b"x" * 1025)
    add("dp_segment_2049", b"x" * 2049)

    # One vector per replacement alphabet, built from the R-Set characters that
    # alphabet substitutes so that it is the one reaching furthest.
    for a, subs in enumerate(REPLACEMENT_ALPHABETS):
        if not subs:
            add("alphabet_0_none_pure_literals", b"abcdefghijklmnopqrstuvwxyz0123456789")
            continue
        body = bytearray()
        while len(body) < 60:
            for j, _donor in subs:
                body.append(_RSET_ASCII[j])
                body += b"word"
        add("alphabet_%d_selected" % a, bytes(body))

    # A literal donor character is representable under any alphabet that does
    # not spend it -- here alphabet 0, which spends none.
    add("donor_char_literal_stays_in_alphabet_0", b"a" * 30 + b"^" + b"b" * 30)
    # With a space in the run, alphabet 0 is out and the alphabets that could
    # carry the space all spend '^' on it, so the run has to break at the '^'.
    add("donor_char_literal_breaks_run", b"a" * 30 + b" ^" + b"b" * 30)
    add("donor_chars_all_literal", ("".join(
        d for subs in REPLACEMENT_ALPHABETS for _, d in subs)).encode() * 4)

    # Every R-Set character present at once: only alphabet 7 represents them all.
    add("all_rset_chars_present", bytes(_RSET_ASCII) * 3)

    add("spaces_hello_world", b"Hello World, this is a passthrough test string.")
    add("quotes_and_commas", b'"one","two","three","four","five","six","seven"')
    add("tilde_heavy", b"~" * 40)
    add("json_object", b'{"name":"value","list":[1,2,3],"nested":{"a":true}}')
    add("markup_document", b"<html><body><p>Hello &amp; welcome</p></body></html>")
    add("crlf_text", b"line one\r\nline two\r\nline three\r\nline four\r\n")
    add("tab_separated", b"col1\tcol2\tcol3\nval1\tval2\tval3\nx\ty\tz\n")

    add("unrepresentable_first_byte_fallback", b"\x00" + b"a" * 40)
    add("binary_short_1", b"\xff")
    add("binary_short_2", b"\xff\xfe")
    add("binary_short_3", b"\xff\xfe\xfd")
    add("binary_raw", bytes(range(256)))
    add("binary_mixed_with_literal", bytes(range(64)) + b"a" * 40 + bytes(range(64)))
    add("very_long_literal_2000", b"abcdefghij" * 200)

    rng = random.Random(20260812)
    for n in [0, 1, 2, 3, 4, 5, 10, 19, 20, 21, 50, 100, 255, 256, 511, 512,
              513, 1000, 1023, 1024, 1025, 4096]:
        add("random_binary_%d" % n, bytes(rng.randrange(256) for _ in range(n)))

    textish = b"abcXYZ 0123.,;\"'<>&\n\t\r|~^@#$%[]{}"
    for n in [20, 30, 50, 100, 300, 600, 1200, 2400]:
        add("random_textish_%d" % n,
            bytes(rng.choice(textish) for _ in range(n)))

    return out


# ---------------------------------------------------------------------
# Adversarial vectors
# ---------------------------------------------------------------------


def adversarial() -> list[dict]:
    entries: list[dict] = []

    def must_fail(name, category, text, code):
        entries.append({
            "name": name,
            "category": category,
            "kind": "must_fail",
            "input_hex": text.encode("utf-8").hex() if isinstance(text, str)
            else text.hex(),
            "error_code": code,
        })

    def valid(name, category, text, expected: bytes):
        entries.append({
            "name": name,
            "category": category,
            "kind": "valid",
            "input_hex": text.encode("utf-8").hex() if isinstance(text, str)
            else text.hex(),
            "expected_hex": expected.hex(),
        })

    body20 = "0123456789abcdefghij"

    # --- unicode_position -------------------------------------------------
    # A character that is not in Alphabet-N must be rejected wherever it sits,
    # and the rejection must not depend on whether the implementation counts
    # UTF-8 bytes, UTF-16 code units or codepoints.
    must_fail("unicode_leading_emoji", "unicode_position",
              "\U0001F600" + signal(0, 20) + body20, "invalid_character")
    must_fail("unicode_inside_dp_segment", "unicode_position",
              signal(0, 20) + body20[:10] + "é" + body20[11:],
              "invalid_character")
    must_fail("unicode_combining_mark_in_segment", "unicode_position",
              signal(0, 20) + body20[:5] + "́" + body20[6:],
              "invalid_character")
    must_fail("unicode_astral_in_block_group", "unicode_position",
              "ab\U0001D400cd", "invalid_character")
    must_fail("unicode_in_signal_group", "unicode_position",
              "€" + signal(0, 20)[1:] + body20, "invalid_character")

    # --- invalid_signal ---------------------------------------------------
    max_payload = (1 << 13) - 1
    valid("signal_payload_at_maximum", "invalid_signal",
          raw_signal(_BLOCK_SIGNAL_BASE + max_payload) + "a" * 1024,
          bytes("a" * 1024, "ascii"))
    must_fail("signal_payload_one_past_maximum", "invalid_signal",
              raw_signal(_BLOCK_SIGNAL_BASE + max_payload + 1) + "a" * 1024,
              "reserved_signal_value")
    must_fail("signal_payload_far_past_maximum", "invalid_signal",
              raw_signal(_BLOCK_SIGNAL_BASE + (1 << 21)) + "a" * 20,
              "reserved_signal_value")
    must_fail("signal_declares_more_than_remains", "invalid_signal",
              signal(0, 100) + body20, "unexpected_end_of_stream")
    must_fail("signal_declares_one_more_than_remains", "invalid_signal",
              signal(0, 21) + body20, "unexpected_end_of_stream")
    must_fail("signal_at_end_of_stream", "invalid_signal",
              signal(0, 1), "unexpected_end_of_stream")

    # --- length_bias ------------------------------------------------------
    # Section 9 stores the length biased by one, so the smallest segment a
    # signal can name is one character, not zero. A decoder that reads the
    # field without adding one under-reads every segment.
    valid("length_field_zero_is_one_character", "length_bias",
          signal(0, 1) + "a", b"a")
    must_fail("length_field_zero_needs_its_one_character", "length_bias",
              signal(0, 1), "unexpected_end_of_stream")
    valid("length_field_max_is_1024_characters", "length_bias",
          signal(0, 1024) + "b" * 1024, b"b" * 1024)

    # --- alphabet_selection ----------------------------------------------
    # The same segment data under each of the eight identifiers. A decoder that
    # ignores the identifier, truncates it, or confuses two alphabets that
    # share a donor character disagrees on at least one of these.
    donors = "^@%$?!~#"
    for a in range(8):
        seg = donors + "abcdefghijkl"
        expected = bytearray()
        table = {d: _RSET_ASCII[j] for j, d in REPLACEMENT_ALPHABETS[a]}
        for ch in seg:
            expected.append(table.get(ch, ord(ch)))
        valid("alphabet_%d_donors_resolve" % a, "alphabet_selection",
              signal(a, len(seg)) + seg, bytes(expected))

    # --- partial_block ----------------------------------------------------
    valid("partial_block_four_chars_pads_below_limit", "partial_block",
          "%nSb", bytes.fromhex("ffffff"))
    must_fail("partial_block_four_chars_pads_over_limit", "partial_block",
              "%nSc", "invalid_partial_block_length")
    must_fail("partial_block_three_chars_pads_over_limit", "partial_block",
              "###", "invalid_partial_block_length")
    must_fail("partial_block_two_chars_pads_over_limit", "partial_block",
              "##", "invalid_partial_block_length")
    must_fail("partial_block_single_char", "partial_block",
              "a", "invalid_partial_block_length")
    must_fail("partial_block_over_limit_after_full_group", "partial_block",
              "vpA.S%nSc", "invalid_partial_block_length")

    return entries


# ---------------------------------------------------------------------
# Output
# ---------------------------------------------------------------------


def write_json(name: str, entries: list[dict]) -> None:
    path = os.path.join(VECTOR_DIR, name)
    with open(path, "w", encoding="utf-8") as fh:
        json.dump(entries, fh, indent=2)
        fh.write("\n")


def write_tsv(name: str, entries: list[dict], columns: list[str]) -> None:
    path = os.path.join(VECTOR_DIR, name)
    with open(path, "w", encoding="utf-8") as fh:
        fh.write("\t".join(columns) + "\n")
        for entry in entries:
            fh.write("\t".join(entry.get(c, "") for c in columns) + "\n")


def main() -> int:
    golden = []
    for name, data in golden_inputs():
        golden.append({
            "name": name,
            "input_hex": data.hex(),
            "output": encode(data),
        })

    names = [g["name"] for g in golden]
    assert len(names) == len(set(names)), "duplicate golden vector name"

    adv = adversarial()
    adv_names = [a["name"] for a in adv]
    assert len(adv_names) == len(set(adv_names)), "duplicate adversarial name"

    write_json("vectors.json", golden)
    write_tsv("vectors.tsv", golden, ["name", "input_hex", "output"])
    write_json("adversarial_vectors.json", adv)
    write_tsv("adversarial_vectors.tsv", adv,
              ["name", "category", "kind", "input_hex", "error_code",
               "expected_hex"])

    print("wrote %d golden and %d adversarial vectors" % (len(golden), len(adv)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
