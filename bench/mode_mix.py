# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Where a file's encoded characters go: block mode, DP, or Fill.

The size benchmark reports one number per file, and that number is
occasionally surprising -- a ratio below 1.0 looks impossible for an
encoding whose passthrough mode is exactly 1:1. This tool takes the
surprise out of it by walking the encoded stream and attributing every
input byte and every output character to the construct that carried it,
using nothing but the signal ranges of specification section 7. It is a
reader, not a second encoder: it never decides anything, so it cannot
agree with the encoder by accident.

The attribution is exact, and the check at the end proves it: the bytes
and the characters it accounts for must add up to the file and to its
encoding.

Usage:
    python3 bench/mode_mix.py                     # the core corpus
    python3 bench/mode_mix.py --groups all        # both corpus groups
    python3 bench/mode_mix.py FILE...             # named files
    python3 bench/mode_mix.py --markdown OUT      # write the report tables
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

BENCH_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(BENCH_DIR))

import corpus  # noqa: E402

ALPHABET_N = (
    "0123456789abcdefghijklmnopqrstuvwxyz"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ.-:+=^!/*?`_~()[]{}@%$#"
)
VALUE = {c: i for i, c in enumerate(ALPHABET_N)}

BLOCK_MAX = 1 << 32                 # V < 2^32 is a block group
DP_BASE = 1 << 32
FILL_SOLID_BASE = 4_429_185_024
FILL_TAIL_BASE = 4_429_709_312
FILL_TAIL_END = 4_433_903_616

MODES = ("block", "dp", "fill solid", "fill tail")


class Mix:
    """Input bytes and output characters, per mode, for one file."""

    def __init__(self, name: str, input_bytes: int, encoded_chars: int):
        self.name = name
        self.input_bytes = input_bytes
        self.encoded_chars = encoded_chars
        self.bytes = dict.fromkeys(MODES, 0)
        self.chars = dict.fromkeys(MODES, 0)
        self.signals = dict.fromkeys(MODES, 0)

    @property
    def ratio(self) -> float:
        return self.encoded_chars / self.input_bytes if self.input_bytes else 0.0

    def share(self, mode: str) -> float:
        return self.bytes[mode] / self.input_bytes if self.input_bytes else 0.0


def analyse(name: str, data: bytes, encoded: str) -> Mix:
    """Attribute every byte of `data` to the construct that carried it."""
    mix = Mix(name, len(data), len(encoded))
    value = VALUE
    n = len(encoded)
    i = 0
    while i < n:
        if n - i < 5:
            # A truncated trailing block: k characters carry k-1 bytes.
            mix.bytes["block"] += n - i - 1
            mix.chars["block"] += n - i
            mix.signals["block"] += 1
            break
        v = 0
        for c in encoded[i : i + 5]:
            v = v * 85 + value[c]
        i += 5
        if v < BLOCK_MAX:
            mix.bytes["block"] += 4
            mix.chars["block"] += 5
            mix.signals["block"] += 1
        elif v < FILL_SOLID_BASE:
            length = ((v - DP_BASE) & 0x7FF) + 1
            mix.bytes["dp"] += length
            mix.chars["dp"] += 5 + length
            mix.signals["dp"] += 1
            i += length
        elif v < FILL_TAIL_BASE:
            length = ((v - FILL_SOLID_BASE) & 0x7FF) + 1
            mix.bytes["fill solid"] += length
            mix.chars["fill solid"] += 5
            mix.signals["fill solid"] += 1
        elif v < FILL_TAIL_END:
            zeros = (((v - FILL_TAIL_BASE) >> 16) & 0x1F) + 1
            mix.bytes["fill tail"] += zeros + 2
            mix.chars["fill tail"] += 5
            mix.signals["fill tail"] += 1
        else:
            raise SystemExit(f"{name}: undefined signal value {v} at char {i - 5}")

    accounted_bytes = sum(mix.bytes.values())
    accounted_chars = sum(mix.chars.values())
    if accounted_bytes != len(data) or accounted_chars != len(encoded):
        raise SystemExit(
            f"{name}: attribution does not add up -- "
            f"{accounted_bytes} of {len(data)} bytes, "
            f"{accounted_chars} of {len(encoded)} chars"
        )
    return mix


def table(rows: list[Mix], title: str = "") -> list[str]:
    out = []
    if title:
        out.append(f"**{title}**\n")
    out += [
        "| sample | input | ratio | block | DP | Fill (solid) | Fill (tail) |",
        "|---|---|---|---|---|---|---|",
    ]
    for m in rows:
        cells = [f"{m.share(mode) * 100:.1f} %" for mode in MODES]
        out.append(
            f"| {m.name} | {m.input_bytes:,} B | {m.ratio:.3f} | "
            + " | ".join(cells)
            + " |"
        )
    out.append("")
    return out


def to_markdown(groups: list[tuple[str, list[Mix]]]) -> str:
    out: list[str] = []
    for title, rows in groups:
        out += table(rows, title)
    out.append(
        "Percentages are the share of the **input bytes** each construct "
        "carried. Block mode spends 1.25 characters per byte, DP spends 1.0 "
        "plus a 5-character signal per segment, and either Fill variant "
        "spends 5 characters however many bytes it covers — which is the "
        "only way a row's ratio gets below 1.0.\n"
    )
    return "\n".join(out)


LABELS = {"core": "The core corpus", "silesia": "The Silesia corpus"}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("files", nargs="*", type=Path)
    ap.add_argument("--groups", default="core",
                    choices=("core", "silesia", "all"),
                    help="which corpus group to walk (default: core)")
    ap.add_argument("--markdown", type=Path)
    args = ap.parse_args()

    import base85n

    def rows_for(targets: list[tuple[str, Path]]) -> list[Mix]:
        rows = []
        for name, path in targets:
            data = path.read_bytes()
            rows.append(analyse(name, data, base85n.encode(data)))
        return rows

    if args.files:
        groups = [("", rows_for([(f.name, f) for f in args.files]))]
    else:
        wanted = corpus.GROUPS if args.groups == "all" else (args.groups,)
        materialised = corpus.ensure_corpus(quiet=True, groups=wanted)
        groups = [
            (LABELS[g] if len(wanted) > 1 else "",
             rows_for([(smp.name, path) for smp, path in materialised
                       if smp.group == g]))
            for g in wanted
        ]

    md = to_markdown(groups)
    if args.markdown:
        args.markdown.parent.mkdir(parents=True, exist_ok=True)
        args.markdown.write_text(md, encoding="utf-8")
        print(f"wrote {args.markdown}", file=sys.stderr)
    else:
        print(md)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
