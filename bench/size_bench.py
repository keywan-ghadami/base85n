# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Encoded-size benchmark: Base85N against four established encodings.

Every measurement here is a round trip. A codec's number is only
recorded once decode(encode(x)) == x, so a size win produced by dropping
data cannot show up as a win.

Usage:
    python3 bench/size_bench.py                # human-readable to stdout
    python3 bench/size_bench.py --markdown OUT # write the report tables
    python3 bench/size_bench.py --json OUT     # write raw measurements
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

BENCH_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(BENCH_DIR))
sys.path.insert(0, str(BENCH_DIR.parent / "python" / "src"))

import bench_codecs as _bench_codecs  # noqa: E402
import corpus  # noqa: E402
import wire_samples  # noqa: E402


class Measurement:
    __slots__ = ("codec", "chars", "ratio", "ok", "error", "json_chars", "xml_chars")

    def __init__(self, codec: str, chars: int | None, ratio: float | None,
                 ok: bool, error: str = "", json_chars: int | None = None,
                 xml_chars: int | None = None):
        self.codec = codec
        self.chars = chars
        self.ratio = ratio
        self.ok = ok
        self.error = error
        self.json_chars = json_chars
        self.xml_chars = xml_chars


def json_escaped_length(s: str) -> int:
    """Length of `s` once placed inside a JSON string literal.

    Only `"` and `\\` need escaping in JSON (RFC 8259); each costs one extra
    character. This is what an encoding's alphabet actually costs when the
    encoded text is carried in a JSON field, which is where most encoded
    payloads end up.
    """
    return len(s) + s.count('"') + s.count("\\")


def xml_escaped_length(s: str) -> int:
    """Length of `s` once placed in XML/HTML character data.

    `&` becomes `&amp;` (+4), `<` becomes `&lt;` and `>` becomes `&gt;` (+3
    each). `>` only strictly requires escaping in the `]]>` sequence, but
    every serializer in practice escapes it, so it is counted.
    """
    return len(s) + 4 * s.count("&") + 3 * s.count("<") + 3 * s.count(">")


def measure(data: bytes, codecs) -> list[Measurement]:
    results = []
    for codec in codecs:
        try:
            encoded = codec.encode(data)
            decoded = codec.decode(encoded)
            if codec.zero_pads_input:
                # The codec padded, so it returns the padding too. Accept the
                # round trip only if the original bytes come back intact and
                # everything after them is the zero padding the encoder added.
                ok = (
                    decoded[: len(data)] == data
                    and decoded[len(data) :] == b"\x00" * (len(decoded) - len(data))
                    and len(decoded) - len(data) < 4
                )
            else:
                ok = decoded == data
            if not ok:
                results.append(
                    Measurement(codec.name, None, None, False, "round-trip mismatch")
                )
                continue
        except Exception as exc:  # noqa: BLE001 - report, do not mask
            results.append(
                Measurement(codec.name, None, None, False, f"{type(exc).__name__}: {exc}")
            )
            continue
        ratio = len(encoded) / len(data) if data else float("inf")
        results.append(Measurement(
            codec.name, len(encoded), ratio, True,
            json_chars=json_escaped_length(encoded),
            xml_chars=xml_escaped_length(encoded),
        ))
    return results


def _fmt_ratio(m: Measurement) -> str:
    if not m.ok:
        return f"**{m.error}**"
    if m.chars is None:
        return "n/a"
    return f"{m.ratio:.3f}"


def _fmt_chars(m: Measurement) -> str:
    if not m.ok:
        return f"**{m.error}**"
    if m.chars is None:
        return "n/a"
    return f"{m.chars:,}"


def _saving_vs_base64(rows: dict[str, Measurement]) -> str:
    b64, b85n = rows.get("Base64"), rows.get("Base85N")
    if not (b64 and b85n and b64.chars and b85n.chars):
        return "-"
    delta = (b64.chars - b85n.chars) / b64.chars * 100.0
    return f"{delta:+.1f} %"


# The other Base85 variants: the field Base85N is actually competing in.
OTHER_BASE85 = ("Ascii85", "Z85", "Base85 (RFC 1924)")


def _saving_vs_best_base85(rows: dict[str, Measurement]) -> str:
    """How much smaller Base85N is than the best of the other Base85s."""
    b85n = rows.get("Base85N")
    if not (b85n and b85n.chars):
        return "-"
    others = [rows[n].chars for n in OTHER_BASE85
              if n in rows and rows[n].chars is not None]
    if not others:
        return "-"
    best = min(others)
    delta = (best - b85n.chars) / best * 100.0
    return f"{delta:+.1f} %"


def run(include_corpus: bool = True) -> dict:
    codecs = _bench_codecs.all_codecs()
    names = [c.name for c in codecs]
    report: dict = {"codecs": names, "files": [], "wire": []}

    if include_corpus:
        print("Preparing corpus ...", file=sys.stderr)
        for sample, path in corpus.ensure_corpus(quiet=True):
            data = path.read_bytes()
            print(f"  {sample.name} ({len(data):,} bytes)", file=sys.stderr)
            ms = measure(data, codecs)
            report["files"].append(
                {
                    "name": sample.name,
                    "category": sample.category,
                    "origin": sample.origin,
                    "bytes": len(data),
                    "results": {m.codec: {"chars": m.chars, "ratio": m.ratio,
                                          "ok": m.ok, "error": m.error,
                                          "json_chars": m.json_chars,
                                          "xml_chars": m.xml_chars} for m in ms},
                }
            )

    for label, data in wire_samples.as_bytes():
        ms = measure(data, codecs)
        report["wire"].append(
            {
                "label": label,
                "bytes": len(data),
                "text": data.decode("utf-8", "replace"),
                "results": {m.codec: {"chars": m.chars, "ratio": m.ratio,
                                      "ok": m.ok, "error": m.error,
                                      "json_chars": m.json_chars,
                                      "xml_chars": m.xml_chars} for m in ms},
            }
        )

    return report


def to_markdown(report: dict) -> str:
    names = report["codecs"]
    out: list[str] = []

    def table(rows_key: str, first_col: str, title: str, unit: str) -> None:
        out.append(f"### {title}\n")
        header = (f"| {first_col} | input | " + " | ".join(names)
                  + " | vs Base64 | vs best other Base85 |")
        sep = "|" + "---|" * (len(names) + 4)
        out.append(header)
        out.append(sep)
        for row in report[rows_key]:
            ms = {n: Measurement(n, r["chars"], r["ratio"], r["ok"], r["error"],
                                 r.get("json_chars"), r.get("xml_chars"))
                  for n, r in row["results"].items()}
            label = row.get("name") or row["label"]
            cells = [_fmt_ratio(ms[n]) if unit == "ratio" else _fmt_chars(ms[n])
                     for n in names]
            out.append(
                f"| {label} | {row['bytes']:,} B | " + " | ".join(cells)
                + f" | {_saving_vs_base64(ms)} | {_saving_vs_best_base85(ms)} |"
            )
        out.append("")

    if report["files"]:
        table("files", "sample", "Corpus files — expansion ratio (encoded chars per input byte)",
              "ratio")
    table("wire", "field", "Short protocol fields — encoded characters", "chars")

    if report["files"]:
        out.append("### Cost of carrying the output inside JSON and XML\n")
        out.append(
            "Expansion ratio over the whole corpus once the encoded text is placed\n"
            "in a JSON string literal or in XML character data, i.e. what the\n"
            "alphabet actually costs in the contexts encoded payloads travel in.\n"
        )
        total_in = sum(row["bytes"] for row in report["files"])
        totals: dict[str, tuple[int, int, int] | None] = {}
        for n in names:
            raw = jsn = xml = 0
            skipped = False
            for row in report["files"]:
                r = row["results"][n]
                if r["chars"] is None:
                    skipped = True
                    break
                raw += r["chars"]
                jsn += r["json_chars"]
                xml += r["xml_chars"]
            totals[n] = None if skipped else (raw, jsn, xml)

        base = totals.get("Base85N")
        out.append(
            "| codec | raw | vs Base85N | inside JSON | vs Base85N "
            "| inside XML | vs Base85N |"
        )
        out.append("|---|---|---|---|---|---|---|")
        for n in names:
            t = totals[n]
            if t is None:
                out.append(f"| {n} | n/a | – | n/a | – | n/a | – |")
                continue
            cells = []
            for i in range(3):
                ratio = t[i] / total_in
                if n == "Base85N" or base is None:
                    delta = "—"
                else:
                    delta = f"+{(t[i] - base[i]) / base[i] * 100:.1f} %"
                cells.append(f"{ratio:.4f} | {delta}")
            out.append(f"| {n} | " + " | ".join(cells) + " |")
        out.append("")
        out.append(
            "\"vs Base85N\" is how much larger that codec's output is than "
            "Base85N's for the same corpus, in that context.\n"
        )

        out.append("### Corpus totals\n")
        totals = {n: 0 for n in names}
        skipped = {n: 0 for n in names}
        total_in = 0
        for row in report["files"]:
            total_in += row["bytes"]
            for n in names:
                r = row["results"][n]
                if r["chars"] is None:
                    skipped[n] += 1
                else:
                    totals[n] += r["chars"]
        out.append("| codec | total encoded | ratio | vs Base64 |")
        out.append("|---|---|---|---|")
        for n in names:
            if skipped[n]:
                out.append(f"| {n} | (not applicable to {skipped[n]} sample(s)) | - | - |")
                continue
            ratio = totals[n] / total_in
            delta = (totals["Base64"] - totals[n]) / totals["Base64"] * 100.0
            out.append(f"| {n} | {totals[n]:,} chars | {ratio:.4f} | {delta:+.2f} % |")
        out.append(f"\nTotal input: {total_in:,} bytes across "
                   f"{len(report['files'])} files.\n")

    return "\n".join(out)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--markdown", type=Path)
    ap.add_argument("--json", type=Path)
    ap.add_argument("--no-corpus", action="store_true",
                    help="only run the short wire samples (no downloads)")
    args = ap.parse_args()

    report = run(include_corpus=not args.no_corpus)

    failures = [
        (row.get("name") or row["label"], name, res["error"])
        for key in ("files", "wire")
        for row in report[key]
        for name, res in row["results"].items()
        if not res["ok"]
    ]

    md = to_markdown(report)
    if args.markdown:
        args.markdown.parent.mkdir(parents=True, exist_ok=True)
        args.markdown.write_text(md, encoding="utf-8")
        print(f"wrote {args.markdown}", file=sys.stderr)
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(report, indent=2), encoding="utf-8")
        print(f"wrote {args.json}", file=sys.stderr)
    if not args.markdown and not args.json:
        print(md)

    if failures:
        print("\nROUND-TRIP FAILURES:", file=sys.stderr)
        for sample, codec, err in failures:
            print(f"  {sample}: {codec}: {err}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
