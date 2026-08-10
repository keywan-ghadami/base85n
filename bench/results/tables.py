#!/usr/bin/env python3
"""Turn bench_speed output into the Markdown tables in RESULTS.md.

    python3 bench/results/tables.py new.txt            # current numbers
    python3 bench/results/tables.py new.txt old.txt    # plus a before/after table

Each argument is the captured stdout of one `bench_speed` run. Rows are
matched by input name, so the two runs must have been given the same
corpus. Nothing is rounded beyond what the tables show.
"""
import re
import sys

ROW = re.compile(
    r"^(?P<input>.{1,24}?)\s{2,}"
    r"(?P<codec>Base64|Ascii85|Z85|Base85N)\s+"
    r"(?P<enc>[\d.]+)\s+(?P<dec>[\d.]+)\s+(?P<ratio>[\d.]+)")


def parse(path):
    """{(input, codec): (encode, decode, ratio)}, in file order."""
    out = {}
    with open(path) as f:
        for line in f:
            m = ROW.match(line.strip())
            if m:
                out[(m.group("input").strip(), m.group("codec"))] = (
                    float(m.group("enc")), float(m.group("dec")), m.group("ratio"))
    if not out:
        sys.exit("no benchmark rows found in %s -- is it bench_speed output?" % path)
    return out


def inputs_of(data):
    seen = []
    for name, _codec in data:
        if name not in seen:
            seen.append(name)
    return seen


def pretty(name):
    return {"synthetic random 1MiB": "synthetic random 1 MiB",
            "synthetic text 1MiB": "synthetic text 1 MiB",
            "escape-heavy 16KiB": "escape-heavy 16 KiB"}.get(name, name)


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    new = parse(sys.argv[1])
    old = parse(sys.argv[2]) if len(sys.argv) > 2 else None
    names = inputs_of(new)

    print("### Fixed-ratio codecs\n")
    print("| codec | encode MB/s | decode MB/s |")
    print("|---|---|---|")
    for codec in ("Base64", "Ascii85", "Z85"):
        vals = [new[(n, codec)] for n in names if (n, codec) in new]
        if not vals:
            continue
        e = [v[0] for v in vals]
        d = [v[1] for v in vals]
        print("| %s | %.0f-%.0f | %.0f-%.0f |" % (codec, min(e), max(e), min(d), max(d)))

    print("\n### Base85N per sample\n")
    print("| input | encode MB/s | decode MB/s | ratio |")
    print("|---|---|---|---|")
    rows = [(n,) + new[(n, "Base85N")] for n in names if (n, "Base85N") in new]
    for n, e, d, r in sorted(rows, key=lambda x: -x[1]):
        # bold where Base85N is the fastest encoder of the four
        others = [new[(n, c)][0] for c in ("Base64", "Ascii85", "Z85") if (n, c) in new]
        enc = "**%.1f**" % e if others and e > max(others) else "%.1f" % e
        print("| %s | %s | %.1f | %s |" % (pretty(n), enc, d, r))

    if old is None:
        return

    print("\n### Before and after\n")
    print("| input | encode before | after | | decode before | after | |")
    print("|---|---|---|---|---|---|---|")
    rows = []
    for n in names:
        k = (n, "Base85N")
        if k in old and k in new:
            rows.append((n, old[k][0], new[k][0], old[k][1], new[k][1]))
    for n, oe, ne, od, nd in sorted(rows, key=lambda x: -(x[2] / x[1])):
        print("| %s | %.1f | %.1f | **%.2fx** | %.1f | %.1f | **%.2fx** |"
              % (pretty(n), oe, ne, ne / oe, od, nd, nd / od))


if __name__ == "__main__":
    main()
