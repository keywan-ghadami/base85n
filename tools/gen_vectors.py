#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Regenerate the shared test vectors in ``testvectors/``.

The golden vectors are the implementation's output for a fixed, deterministic
set of inputs; the adversarial vectors are hand-built character sequences that
a decoder must reject (or accept with a stated result). Both sets are written
in a ``.json`` and a ``.tsv`` form carrying identical data, which
``tools/check_vectors.py`` verifies.

The adversarial expectations are built from the specification's tables here in
this file -- the signal layout of section 9, the substitution derivation of
section 4.3 -- and never by asking the implementation what it does. That is the
point of them: they are what catches an implementation and its own tests
agreeing on something the specification does not say.

Requires the `base85n` module, which is the Rust implementation behind PyO3
bindings::

    pip install -e python/            # or: maturin develop -m python/Cargo.toml
    python3 tools/gen_vectors.py

It rewrites the four files in place; ``check_vectors.py`` should pass
immediately afterwards.
"""

from __future__ import annotations

import json
import os
import random
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VECTOR_DIR = os.path.join(REPO_ROOT, "testvectors")

try:
    import base85n
    from base85n import encode
except ImportError:  # pragma: no cover - a setup problem, not a test failure
    sys.exit(
        "base85n is not importable. Build the bindings first:\n"
        "    pip install -e python/\n"
        "or  maturin develop --release -m python/Cargo.toml"
    )

ALPHABET_N = base85n.ALPHABET_N
R_SET = list(base85n.R_SET)
PROFILES = list(base85n.PROFILES)
MIN_PASSTHROUGH_BYTES = base85n.MIN_PASSTHROUGH_BYTES
MIN_FILL_BYTES = base85n.MIN_FILL_BYTES
MAX_FILL_BYTES = base85n.MAX_FILL_BYTES
MAX_DP = base85n.MAX_DP_SEGMENT_CHARS
DP_BASE = base85n.DP_SIGNAL_BASE
FILL_BASE = base85n.FILL_SIGNAL_BASE
TAIL_BASE = base85n.TAIL_SIGNAL_BASE
FUTURE_BASE = base85n.FUTURE_SIGNAL_BASE
MIN_TAIL_ZEROS = base85n.MIN_TAIL_ZEROS
MAX_TAIL_ZEROS = base85n.MAX_TAIL_ZEROS


# ---------------------------------------------------------------------
# Section 8 and 9: values, signals and the substitution they name
# ---------------------------------------------------------------------


def raw_signal(value: int) -> str:
    """Section 8's ValueToBase85Digits, for a whole 5-character group."""
    digits = []
    for _ in range(5):
        digits.append(ALPHABET_N[value % 85])
        value //= 85
    return "".join(reversed(digits))


def dp_signal(profile: int, mask: int, length: int) -> str:
    """The DP signal for a profile, mask and real character length (1..2048)."""
    return raw_signal(DP_BASE + ((profile << 24) | (mask << 11) | (length - 1)))


def fill_signal(byte: int, length: int) -> str:
    """Fill, solid variant: `byte` repeated `length` times (1..2048)."""
    return raw_signal(FILL_BASE + ((byte << 11) | (length - 1)))


def tail_signal(zeros: int, order: int, lit: bytes) -> str:
    """Fill, tail variant: `zeros` zero bytes (1..32) and two literals.

    Order 0 puts the zeros first, order 1 puts the literals first.
    """
    payload = (order << 21) | ((zeros - 1) << 16) | (lit[0] << 8) | lit[1]
    return raw_signal(TAIL_BASE + payload)


def substitution(profile: int, mask: int) -> dict[str, int]:
    """Section 4.3: donor character -> the R-Set byte it stands for."""
    table: dict[str, int] = {}
    rank = 0
    for j in range(len(R_SET)):
        if mask >> j & 1:
            table[PROFILES[profile][rank]] = R_SET[j]
            rank += 1
    return table


def dp_segment(profile: int, mask: int, seg: str) -> tuple[str, bytes]:
    """A complete DP segment and the bytes it must decode to."""
    table = substitution(profile, mask)
    expected = bytes(table.get(ch, ord(ch)) for ch in seg)
    return dp_signal(profile, mask, len(seg)) + seg, expected


def signal_of(encoded: str) -> int:
    """The value of an encoded string's first 5-character group."""
    value = 0
    for ch in encoded[:5]:
        value = value * 85 + ALPHABET_N.index(ch)
    return value


# ---------------------------------------------------------------------
# Golden vectors
# ---------------------------------------------------------------------


def profile_cases() -> list[tuple[str, bytes]]:
    """One input per donor profile, constructed rather than written by hand.

    A profile is chosen only when every lower-numbered one has been ruled out
    by a literal it would have spent, so the inputs that select the higher
    profiles are not obvious to write down. Building them from the profile
    table keeps the set honest about which profiles are reachable at all: the
    assertions below fail if any of the eight cannot be produced, or if the
    encoder disagrees with the construction.
    """
    rset = "".join(chr(b) for b in R_SET)
    cases: list[tuple[str, bytes]] = []

    for p in range(len(PROFILES)):
        data = None
        for k in range(1, len(R_SET) + 1):
            # Rule out every lower profile with a literal it would spend at a
            # rank below k, while spending none that profile p would.
            spent_by_p = set(PROFILES[p][:k])
            literals = []
            for q in range(p):
                candidates = [c for c in PROFILES[q][:k] if c not in spent_by_p]
                if not candidates:
                    break
                literals.append(candidates[0])
            else:
                body = bytearray()
                while len(body) < 3 * MIN_PASSTHROUGH_BYTES:
                    for r in rset[:k]:
                        body += b"word"
                        body.append(ord(r))
                    body += "".join(literals).encode()
                data = bytes(body)
                break

        assert data is not None, f"no input construction selects profile {p}"
        # The construction is only a prediction until the encoder confirms it.
        value = signal_of(encode(data))
        assert DP_BASE <= value < FILL_BASE, f"profile {p} case is not a DP segment"
        got = ((value - DP_BASE) >> 24) & 0x7
        assert got == p, f"expected profile {p}, encoder chose {got}"
        cases.append(("profile_%d_selected" % p, data))

    return cases


def golden_inputs() -> list[tuple[str, bytes]]:
    out: list[tuple[str, bytes]] = []
    add = lambda name, data: out.append((name, data))  # noqa: E731

    add("empty", b"")
    for n in range(1, 9):
        add("literal_len_%d" % n, b"abcdefgh"[:n])

    # Short runs are Fill segments from MIN_FILL_BYTES up, so the "literal"
    # length series uses varied bytes to stay in block and passthrough mode.
    varied = bytes(b"abcdefghijklmnopqrstuvwxyz"[i % 26] for i in range(3000))
    add("literal_19_below_min", varied[:19])
    add("literal_exactly_20", varied[:MIN_PASSTHROUGH_BYTES])
    add("literal_21_above_min", varied[:21])
    add("literal_long_600", varied[:600])

    # MAX_DP_ANALYSIS_BYTES boundary: 2048 fits one segment, 2049 needs two.
    add("dp_segment_2047", varied[:2047])
    add("dp_segment_exactly_2048", varied[:2048])
    add("dp_segment_2049_needs_two", varied[:2049])
    add("dp_segment_3000", varied[:3000])

    out.extend(profile_cases())

    # A literal donor character is representable while the segment does not
    # spend it; with a space in the run the profiles that could carry the space
    # spend '~' or '^' on it, so the choice of profile has to move.
    add("donor_char_literal_no_rset", varied[:30] + b"^" + varied[:30])
    add("donor_char_literal_with_space", varied[:30] + b" ^ " + varied[:30])
    add("donor_chars_all_literal", "".join(sorted(set("".join(PROFILES)))).encode() * 4)

    # Every R-Set character present at once: k reaches 13, so a whole profile
    # is spent and no literal from it can appear.
    add("all_rset_chars_present", bytes(R_SET) * 3)

    add("spaces_hello_world", b"Hello World, this is a passthrough test string.")
    add("quotes_and_commas", b'"one","two","three","four","five","six","seven"')
    add("tilde_heavy", b"~" * 40)
    add("json_object", b'{"name":"value","list":[1,2,3],"nested":{"a":true}}')
    add("markup_document", b"<html><body><p>Hello &amp; welcome</p></body></html>")
    add("crlf_text", b"line one\r\nline two\r\nline three\r\nline four\r\n")
    add("tab_separated", b"col1\tcol2\tcol3\nval1\tval2\tval3\nx\ty\tz\n")
    add("indented_code", b"def f(x):\n" + b" " * 8 + b"return x + 1\n" + b" " * 8 + b"# done\n")

    # --- Solid Fill -------------------------------------------------------
    for n in (MIN_FILL_BYTES - 1, MIN_FILL_BYTES, MIN_FILL_BYTES + 1):
        add("fill_zero_%d" % n, b"\x00" * n)
        add("fill_space_%d" % n, b" " * n)
    add("fill_max_2048", b"\x00" * MAX_FILL_BYTES)
    add("fill_2049_needs_two_signals", b"\x00" * (MAX_FILL_BYTES + 1))
    add("fill_4096_two_full_signals", b"\xff" * (2 * MAX_FILL_BYTES))
    add("fill_between_text", varied[:40] + b"=" * 300 + varied[:40])
    add("fill_short_run_inside_text", varied[:40] + b"=" * MIN_FILL_BYTES + varied[:40])
    add("fill_after_binary", bytes(range(32)) + b"\x00" * 100 + bytes(range(32)))
    add("fill_of_every_length_class", b"\x07" * 7 + b"a" * 25 + b"\x00" * 40)

    # --- Fill with a tail -------------------------------------------------
    # The zero runs a solid Fill cannot reach economically: too short for a
    # signal of their own, and their two neighbours would cost a block group.
    for n in range(MIN_TAIL_ZEROS - 1, MIN_TAIL_ZEROS + 3):
        add("tail_zeros_%d_then_literals" % n, b"\x00" * n + b"AB" + b"\xff\xfe")
        add("tail_literals_then_zeros_%d" % n, b"\xff\xfe" + b"\x00" * n + b"AB")
    add("tail_at_maximum_zeros", b"\x00" * MAX_TAIL_ZEROS + b"AB")
    add("tail_one_past_maximum_zeros", b"\x00" * (MAX_TAIL_ZEROS + 1) + b"AB")
    add("tail_zeros_between_binary", bytes(range(16)) + b"\x00" * 6 + bytes(range(16, 32)))
    add("tail_zeros_between_text", varied[:40] + b"\x00" * 5 + varied[:40])
    add("tail_run_of_runs", (b"\x00" * 4 + b"\x81\x82") * 8)
    add("tail_zeros_at_end_of_input", bytes(range(16)) + b"\x00" * 8)
    add("tail_whole_input_is_zeros_and_two", b"\x00" * 3 + b"\x01\x02")

    add("unrepresentable_first_byte_fallback", b"\x00" + varied[:40])
    add("binary_short_1", b"\xff")
    add("binary_short_2", b"\xff\xfe")
    add("binary_short_3", b"\xff\xfe\xfd")
    add("binary_raw", bytes(range(256)))
    add("binary_mixed_with_literal", bytes(range(64)) + varied[:40] + bytes(range(64)))
    add("very_long_literal_2000", varied[:2000])

    rng = random.Random(20260812)
    for n in [0, 1, 2, 3, 4, 5, 10, 19, 20, 21, 50, 100, 255, 256, 511, 512,
              513, 1000, 2047, 2048, 2049, 4096]:
        add("random_binary_%d" % n, bytes(rng.randrange(256) for _ in range(n)))

    textish = b"abcXYZ 0123.,;\"'<>&\n\t\r|~^@#$%[]{}"
    for n in [20, 30, 50, 100, 300, 600, 1200, 2400]:
        add("random_textish_%d" % n,
            bytes(rng.choice(textish) for _ in range(n)))

    # Runs mixed into text and binary, so the transitions between all three
    # modes are pinned by a vector and not only by each language's own tests.
    for n in [64, 300, 1500, 5000]:
        body = bytearray()
        while len(body) < n:
            r = rng.random()
            if r < 0.35:
                body += bytes([rng.randrange(256)]) * rng.randrange(1, 40)
            elif r < 0.7:
                body += bytes(rng.choice(textish) for _ in range(rng.randrange(1, 40)))
            else:
                body += bytes(rng.randrange(256) for _ in range(rng.randrange(1, 40)))
        add("random_runs_%d" % n, bytes(body[:n]))

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
              "\U0001F600" + dp_signal(0, 0, 20) + body20, "invalid_character")
    must_fail("unicode_inside_dp_segment", "unicode_position",
              dp_signal(0, 0, 20) + body20[:10] + "é" + body20[11:],
              "invalid_character")
    must_fail("unicode_combining_mark_in_segment", "unicode_position",
              dp_signal(0, 0, 20) + body20[:5] + "́" + body20[6:],
              "invalid_character")
    must_fail("unicode_astral_in_block_group", "unicode_position",
              "ab\U0001D400cd", "invalid_character")
    must_fail("unicode_in_signal_group", "unicode_position",
              "€" + dp_signal(0, 0, 20)[1:] + body20, "invalid_character")

    # --- signal_range -----------------------------------------------------
    # The three boundaries of section 9's table, from both sides.
    valid("dp_signal_at_maximum", "signal_range",
          dp_signal(7, 0x1FFF, MAX_DP) + "a" * MAX_DP, b"a" * MAX_DP)
    valid("fill_signal_at_minimum", "signal_range",
          raw_signal(FILL_BASE), b"\x00")
    valid("fill_signal_at_maximum", "signal_range",
          raw_signal(TAIL_BASE - 1), b"\xff" * MAX_FILL_BYTES)
    valid("tail_signal_at_minimum", "signal_range",
          raw_signal(TAIL_BASE), b"\x00" * 3)
    valid("tail_signal_at_maximum", "signal_range",
          raw_signal(FUTURE_BASE - 1), b"\xff\xff" + b"\x00" * MAX_TAIL_ZEROS)
    must_fail("future_signal_space_first_value", "signal_range",
              raw_signal(FUTURE_BASE), "undefined_signal")
    must_fail("future_signal_space_last_value", "signal_range",
              "#" * 5, "undefined_signal")
    must_fail("future_signal_space_middle", "signal_range",
              raw_signal((FUTURE_BASE + 85 ** 5) // 2), "undefined_signal")
    must_fail("signal_declares_more_than_remains", "signal_range",
              dp_signal(0, 0, 100) + body20, "unexpected_end_of_stream")
    must_fail("signal_declares_one_more_than_remains", "signal_range",
              dp_signal(0, 0, 21) + body20, "unexpected_end_of_stream")
    must_fail("signal_at_end_of_stream", "signal_range",
              dp_signal(0, 0, 1), "unexpected_end_of_stream")

    # --- length_bias ------------------------------------------------------
    # Section 9 stores both length fields biased by one, so the smallest
    # segment a signal can name is one, not zero. A decoder that reads either
    # field without adding one under-produces on every segment.
    valid("dp_length_field_zero_is_one_character", "length_bias",
          dp_signal(0, 0, 1) + "a", b"a")
    must_fail("dp_length_field_zero_needs_its_character", "length_bias",
              dp_signal(0, 0, 1), "unexpected_end_of_stream")
    valid("dp_length_field_max_is_2048_characters", "length_bias",
          dp_signal(0, 0, MAX_DP) + "b" * MAX_DP, b"b" * MAX_DP)
    valid("fill_length_field_zero_is_one_byte", "length_bias",
          fill_signal(0x41, 1), b"A")
    valid("fill_length_field_max_is_2048_bytes", "length_bias",
          fill_signal(0x41, MAX_FILL_BYTES), b"A" * MAX_FILL_BYTES)
    valid("tail_length_field_zero_is_one_zero_byte", "length_bias",
          tail_signal(1, 0, b"AB"), b"\x00AB")
    valid("tail_length_field_max_is_32_zero_bytes", "length_bias",
          tail_signal(MAX_TAIL_ZEROS, 0, b"AB"), b"\x00" * MAX_TAIL_ZEROS + b"AB")

    # --- profile_selection ------------------------------------------------
    # The same segment data under each of the eight profile identifiers, with
    # every mask bit set so all 13 donors are in play. A decoder that ignores
    # the identifier, truncates it, or derives the donors in the wrong order
    # disagrees on at least one of these.
    for p in range(len(PROFILES)):
        seg = "".join(PROFILES[p]) + "abcdefghijkl"
        text, expected = dp_segment(p, 0x1FFF, seg)
        valid("profile_%d_donors_resolve" % p, "profile_selection", text, expected)

    # A partial mask consumes the profile's *first k* donors, not the donors at
    # the set bits' own positions -- the mistake worth a vector of its own.
    for mask in (0b1, 0b10, 0b101, 0b1000000000001, 0b1010101010101):
        seg = "".join(PROFILES[2]) + "xyz"
        text, expected = dp_segment(2, mask, seg)
        valid(f"mask_{mask:013b}_consumes_leading_donors", "profile_selection",
              text, expected)

    # --- fill_expansion ---------------------------------------------------
    # Section 13's bound: five characters can name 2048 bytes and no more.
    valid("fill_repeats_one_byte_2048_times", "fill_expansion",
          fill_signal(0x5a, MAX_FILL_BYTES), b"\x5a" * MAX_FILL_BYTES)
    valid("fill_signals_back_to_back", "fill_expansion",
          fill_signal(0x00, MAX_FILL_BYTES) * 4, b"\x00" * (4 * MAX_FILL_BYTES))
    valid("fill_reads_no_characters", "fill_expansion",
          fill_signal(0x20, 3) + "vpA.S", b"   " + b"abcd")

    # The tail variant's order bit: the same three fields, both ways round.
    valid("tail_order_zero_puts_zeros_first", "fill_expansion",
          tail_signal(4, 0, b"\x81\x82"), b"\x00" * 4 + b"\x81\x82")
    valid("tail_order_one_puts_literals_first", "fill_expansion",
          tail_signal(4, 1, b"\x81\x82"), b"\x81\x82" + b"\x00" * 4)
    valid("tail_literals_may_be_zero_too", "fill_expansion",
          tail_signal(2, 0, b"\x00\x00"), b"\x00" * 4)
    valid("tail_reads_no_characters", "fill_expansion",
          tail_signal(3, 1, b"AB") + "vpA.S", b"AB" + b"\x00" * 3 + b"abcd")
    valid("tail_signals_back_to_back", "fill_expansion",
          tail_signal(MAX_TAIL_ZEROS, 0, b"AB") * 3,
          (b"\x00" * MAX_TAIL_ZEROS + b"AB") * 3)

    # --- final_block ------------------------------------------------------
    # Section 7.5: a trailing group must be exactly the canonical encoding of
    # the bytes it produces. Every alias is rejected, which is what stops the
    # same bytes from having several encodings.
    for data in (b"\x61", b"\x61\x62", b"\xff\xff\xff"):
        canonical = encode(data)
        valid("final_block_canonical_%d" % len(data), "final_block", canonical, data)
        last = ALPHABET_N.index(canonical[-1])
        if last + 1 < 85:
            alias = canonical[:-1] + ALPHABET_N[last + 1]
            must_fail("final_block_alias_%d" % len(data), "final_block",
                      alias, "invalid_final_block")
    must_fail("final_block_four_chars_over_limit", "final_block",
              "####", "invalid_final_block")
    must_fail("final_block_three_chars_over_limit", "final_block",
              "###", "invalid_final_block")
    must_fail("final_block_two_chars_over_limit", "final_block",
              "##", "invalid_final_block")
    must_fail("final_block_single_char", "final_block",
              "a", "invalid_final_block")
    must_fail("final_block_over_limit_after_full_group", "final_block",
              "vpA.S####", "invalid_final_block")

    # --- whitespace -------------------------------------------------------
    # Section 7.1: the four whitespace characters are removed before anything
    # else, including from inside a segment, and the length field counts what
    # is left.
    valid("whitespace_between_groups", "whitespace",
          "vpA.S\nvpA.S", b"abcdabcd")
    valid("whitespace_inside_dp_segment", "whitespace",
          dp_signal(0, 0, 20)[:3] + "\n" + dp_signal(0, 0, 20)[3:]
          + body20[:5] + " \t" + body20[5:], body20.encode())

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
