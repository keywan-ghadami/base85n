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
    python3 bench/size_bench.py --no-silesia   # core corpus only, no 202 MiB
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

BENCH_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(BENCH_DIR))

import bench_codecs as _bench_codecs  # noqa: E402
import corpus  # noqa: E402
import wire_samples  # noqa: E402


# Every embedding measured, in the order the report presents them. "chars"
# is the raw encoded length; the rest are what that text costs once it is put
# somewhere real.
EMBEDDINGS = ("chars", "json_chars", "html_chars", "xml_chars", "url_chars")


class Measurement:
    __slots__ = ("codec", "chars", "ratio", "ok", "error", "json_chars",
                 "xml_chars", "html_chars", "url_chars", "input_bytes")

    def __init__(self, codec: str, chars: int | None, ratio: float | None,
                 ok: bool, error: str = "", json_chars: int | None = None,
                 xml_chars: int | None = None, html_chars: int | None = None,
                 url_chars: int | None = None, input_bytes: int = 0):
        self.codec = codec
        self.chars = chars
        self.ratio = ratio
        self.ok = ok
        self.error = error
        self.json_chars = json_chars
        self.xml_chars = xml_chars
        self.html_chars = html_chars
        self.url_chars = url_chars
        self.input_bytes = input_bytes


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


def html_attr_escaped_length(s: str) -> int:
    """Length of `s` inside a double-quoted HTML attribute value.

    `&` becomes `&amp;` (+4), `<` `&lt;` and `>` `&gt;` (+3 each), and `"`
    becomes `&quot;` (+5). This is the most common place an encoded payload
    ends up in a page -- `data-*`, `value=`, an inline `src` -- and it is not
    the same set as XML character data, because the quote matters.
    """
    return (len(s) + 4 * s.count("&") + 3 * s.count("<") + 3 * s.count(">")
            + 5 * s.count('"'))


# RFC 3986 unreserved: everything else is percent-encoded by a library's
# default (`urllib.parse.quote(s, safe="")` and its equivalents elsewhere).
URL_UNRESERVED = frozenset(
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~"
)


def url_escaped_length(s: str) -> int:
    """Length of `s` as a URL query-string value.

    Percent-encoding costs three characters for every byte outside the
    unreserved set. A query component does allow more than that by the
    grammar, but almost nothing emits it: the default of every widely used
    library is to encode down to unreserved, so that is what is measured.
    """
    return sum(1 if c in URL_UNRESERVED else 3 for c in s)


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
            html_chars=html_attr_escaped_length(encoded),
            url_chars=url_escaped_length(encoded),
        ))
    return results


# The other Base85 variants: the field Base85N is actually competing in.
OTHER_BASE85 = ("Ascii85", "Z85", "Base85 (RFC 1924)")


def _chars(m: Measurement, key: str) -> int | None:
    """Encoded characters under `key`: raw, or inside JSON, or inside XML."""
    return getattr(m, key)


def _winners(rows: dict[str, Measurement], ratio: bool,
             key: str = "chars") -> set[str]:
    """Codecs holding the smallest value in the row, as the table prints it.

    Ties win together, and so does a lead too small to survive rounding: two
    codecs shown as 1.247 are shown as equal, so both are marked.
    """
    sized = {}
    for n, m in rows.items():
        c = _chars(m, key)
        if m.ok and c is not None:
            sized[n] = round(c / m.input_bytes, 3) if ratio else c
    if not sized:
        return set()
    best = min(sized.values())
    return {n for n, v in sized.items() if v == best}


def _fmt(m: Measurement, ratio: bool, winners: set[str],
         key: str = "chars") -> str:
    if not m.ok:
        return f"**{m.error}**"
    c = _chars(m, key)
    if c is None:
        return "n/a"
    text = f"{c / m.input_bytes:.3f}" if ratio else f"{c:,}"
    return f"**{text}**" if m.codec in winners else text


def _delta(smaller: int | None, reference: int | None) -> str:
    """Size difference as a signed percentage: negative means smaller."""
    if not smaller or not reference:
        return "–"
    pct = (smaller - reference) / reference * 100.0
    if abs(pct) < 0.05:
        return "same"
    return f"{pct:+.1f} %"


def _vs_base64(rows: dict[str, Measurement], key: str = "chars") -> str:
    b64, b85n = rows.get("Base64"), rows.get("Base85N")
    return _delta(_chars(b85n, key) if b85n else None,
                  _chars(b64, key) if b64 else None)


def _vs_best_base85(rows: dict[str, Measurement], key: str = "chars") -> str:
    b85n = rows.get("Base85N")
    others = [_chars(rows[n], key) for n in OTHER_BASE85
              if n in rows and _chars(rows[n], key) is not None]
    if not others:
        return "–"
    return _delta(_chars(b85n, key) if b85n else None, min(others))


def run(include_corpus: bool = True,
        groups: tuple[str, ...] = corpus.GROUPS) -> dict:
    codecs = _bench_codecs.all_codecs()
    names = [c.name for c in codecs]
    # "files" is the core corpus; each further group gets its own list, so a
    # 202 MiB corpus cannot quietly swallow the totals of a 6.5 MB one.
    report: dict = {"codecs": names, "files": [], "silesia": [], "wire": []}
    into = {"core": "files", "silesia": "silesia"}

    if include_corpus:
        print("Preparing corpus ...", file=sys.stderr)
        for sample, path in corpus.ensure_corpus(quiet=True, groups=groups):
            data = path.read_bytes()
            print(f"  {sample.name} ({len(data):,} bytes)", file=sys.stderr)
            ms = measure(data, codecs)
            report[into[sample.group]].append(
                {
                    "name": sample.name,
                    "category": sample.category,
                    "origin": sample.origin,
                    "bytes": len(data),
                    "results": {m.codec: {"chars": m.chars, "ratio": m.ratio,
                                          "ok": m.ok, "error": m.error,
                                          **{k: getattr(m, k) for k in EMBEDDINGS[1:]}}
                                for m in ms},
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
                                      **{k: getattr(m, k) for k in EMBEDDINGS[1:]}}
                            for m in ms},
            }
        )

    return report


def to_markdown(report: dict) -> str:
    names = report["codecs"]
    out: list[str] = []

    def measurements(row: dict) -> dict[str, Measurement]:
        return {n: Measurement(n, r["chars"], r["ratio"], r["ok"], r["error"],
                               r.get("json_chars"), r.get("xml_chars"),
                               r.get("html_chars"), r.get("url_chars"),
                               row["bytes"])
                for n, r in row["results"].items()}

    def total_row(rows: list[dict]) -> dict:
        """One synthetic row summing every file, for the last line of a table."""
        results: dict[str, dict] = {}
        for n in names:
            sums = dict.fromkeys(EMBEDDINGS, 0)
            ok = True
            for row in rows:
                r = row["results"][n]
                if not r["ok"] or r["chars"] is None:
                    ok = False
                    break
                for k in sums:
                    sums[k] += r[k]
            results[n] = {"ok": ok, "error": "", "ratio": None,
                          **({k: (v if ok else None) for k, v in sums.items()})}
        return {"name": "whole corpus",
                "bytes": sum(row["bytes"] for row in rows), "results": results}

    def table(rows_key: str, first_col: str, title: str, unit: str,
              key: str = "chars", totals: bool = False,
              intro: str = "") -> None:
        out.append(f"### {title}\n")
        if intro:
            out.append(intro)
        header = (f"| {first_col} | input | " + " | ".join(names)
                  + " | vs Base64 | vs best other Base85 |")
        sep = "|" + "---|" * (len(names) + 4)
        out.append(header)
        out.append(sep)
        ratio = unit == "ratio"
        rows = list(report[rows_key])
        if totals and rows:
            rows.append(total_row(rows))
        for row in rows:
            ms = measurements(row)
            label = row.get("name") or row["label"]
            win = _winners(ms, ratio, key)
            cells = [_fmt(ms[n], ratio, win, key) for n in names]
            out.append(
                f"| {label} | {row['bytes']:,} B | " + " | ".join(cells)
                + f" | {_vs_base64(ms, key)} | {_vs_best_base85(ms, key)} |"
            )
        out.append("")
        out.append(
            "**Bold** marks the smallest output in that row; on a tie every "
            "codec that reaches it is marked. The two delta columns are "
            "Base85N's size difference — **negative is a saving**, positive "
            "means Base85N is larger.\n"
        )

    def embedding_totals(rows_key: str, title: str) -> None:
        """One row per codec, one column per embedding, over a whole group."""
        rows = report[rows_key]
        total_in = sum(row["bytes"] for row in rows)
        out.append(f"### {title}\n")
        out.append("| codec | raw | in JSON | in an HTML attribute "
                   "| in XML | in a URL |")
        out.append("|" + "---|" * 6)
        for n in names:
            sums = dict.fromkeys(EMBEDDINGS, 0)
            ok = True
            for row in rows:
                r = row["results"][n]
                if not r["ok"] or r["chars"] is None:
                    ok = False
                    break
                for k in sums:
                    sums[k] += r[k]
            if not ok:
                out.append(f"| {n} | (round-trip failure) | | | | |")
                continue
            cells = [f"{sums[k] / total_in:.3f}" for k in EMBEDDINGS]
            out.append(f"| {n} | " + " | ".join(cells) + " |")
        out.append("")
        out.append(f"Expansion ratio over all {len(rows)} files "
                   f"({total_in:,} bytes).\n")

    # Encoded payloads almost never travel raw: they travel inside JSON, HTML,
    # XML or a URL. Those tables come first for that reason, and the raw one
    # stays as the reference it is.
    if report["files"]:
        table("files", "sample",
              "Inside a JSON string literal — expansion ratio "
              "(characters per input byte)", "ratio", "json_chars", totals=True,
              intro="The commonest destination of an encoded payload, and therefore\n"
                    "the first table: what each codec costs per file once its output is\n"
                    "placed in a JSON string literal, `\"` and `\\` escaped. Last row is\n"
                    "the whole corpus.\n")
        table("files", "sample",
              "Inside an HTML attribute — expansion ratio "
              "(characters per input byte)", "ratio", "html_chars", totals=True,
              intro="A double-quoted attribute value — `data-*`, `value=`, an inline\n"
                    "`src` — with `&`, `<`, `>` and `\"` escaped. Last row is the whole\n"
                    "corpus.\n")
        table("files", "sample",
              "Inside XML character data — expansion ratio "
              "(characters per input byte)", "ratio", "xml_chars", totals=True,
              intro="The same per file inside XML character data, with `&`, `<` and\n"
                    "`>` escaped. Last row is the whole corpus.\n")
        table("files", "sample",
              "As a URL query value — expansion ratio "
              "(characters per input byte)", "ratio", "url_chars", totals=True,
              intro="Percent-encoded down to RFC 3986's unreserved set, which is what\n"
                    "every library's default does. **This is the embedding Base85N is\n"
                    "worst at**, and it is worst at it by design: `#`, `%`, `+`, `?` and\n"
                    "`&` are in Alphabet-N because they are safe in JSON and XML, and\n"
                    "they are exactly the characters a URL encoder charges three\n"
                    "characters for. Base64url — not measured here — is the right tool\n"
                    "for a query string. Last row is the whole corpus.\n"
                    "\n"
                    "An HTTP header value needs no table: RFC 9110 admits any visible\n"
                    "ASCII, so all five codecs cost their raw length there.\n")

    if report["files"]:
        table("files", "sample",
              "Raw, unembedded — expansion ratio (encoded chars per input byte)",
              "ratio",
              intro="The reference measurement: the encoded text on its own, in a\n"
                    "binary file or a socket, escaped by nobody.\n")
    if report["files"]:
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

    if report.get("silesia"):
        out.append(
            "**The Silesia corpus.**\n"
            "Twelve files, 202 MiB, assembled in 2003 for compression research\n"
            "and unchanged since: a star catalogue, a medical MRI and an X-ray,\n"
            "a chemical structure database, a MySQL dump, a PDF, a dictionary,\n"
            "two tarballs of executables and source, an OpenOffice library and a\n"
            "set of XML files. It is here as a control on the corpus above:\n"
            "nobody chose it with this codec in mind, and it holds input classes\n"
            "the thirteen files have none of.\n"
            "\n"
            "Two per-file tables, not five. The four embeddings differ only in\n"
            "what each alphabet costs per character, which the core corpus\n"
            "already establishes per codec; what Silesia is here to test is the\n"
            "ratio, on data nobody selected. The whole-corpus numbers for raw\n"
            "and for all four embeddings follow the two tables.\n"
        )
        table("silesia", "sample",
              "Silesia, inside a JSON string literal — expansion ratio "
              "(characters per input byte)", "ratio", "json_chars", totals=True,
              intro="`\"` and `\\` escaped, as in the corresponding table above.\n")
        table("silesia", "sample",
              "Silesia, raw — expansion ratio (encoded chars per input byte)",
              "ratio",
              intro="The encoded text on its own, escaped by nobody.\n")
        embedding_totals("silesia", "Silesia totals, per embedding")

    table("wire", "field", "Short protocol fields — encoded characters", "chars")

    return "\n".join(out)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--markdown", type=Path)
    ap.add_argument("--json", type=Path)
    ap.add_argument("--no-corpus", action="store_true",
                    help="only run the short wire samples (no downloads)")
    ap.add_argument("--no-silesia", action="store_true",
                    help="skip the 202 MiB Silesia group (core corpus only)")
    args = ap.parse_args()

    groups = ("core",) if args.no_silesia else corpus.GROUPS
    report = run(include_corpus=not args.no_corpus, groups=groups)

    failures = [
        (row.get("name") or row["label"], name, res["error"])
        for key in ("files", "silesia", "wire")
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
