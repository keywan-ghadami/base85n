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

    for which, idx in (("Encode", 0), ("Decode", 1)):
        print("### %s throughput (MB/s of original bytes)\n" % which)
        print("| input | Base64 | Ascii85 | Z85 | Base85N | vs Base64 | "
              "vs best other Base85 |")
        print("|---|---|---|---|---|---|---|")
        for n in names:
            if (n, "Base85N") not in new:
                continue
            cell = {}
            for c in ("Base64", "Ascii85", "Z85", "Base85N"):
                cell[c] = new[(n, c)][idx] if (n, c) in new else None
            best = max(v for v in cell.values() if v is not None)
            out = []
            for c in ("Base64", "Ascii85", "Z85", "Base85N"):
                v = cell[c]
                out.append("n/a" if v is None
                           else ("**%.0f**" % v if v == best else "%.0f" % v))
            mine = cell["Base85N"]
            others = [cell[c] for c in ("Ascii85", "Z85") if cell[c] is not None]
            vs64 = ("%+.0f %%" % ((mine / cell["Base64"] - 1) * 100)
                    if cell["Base64"] else "n/a")
            vs85 = ("%+.0f %%" % ((mine / max(others) - 1) * 100)
                    if others else "n/a")
            print("| %s | %s | %s |" % (pretty(n), " | ".join(out),
                                        " | ".join((vs64, vs85))))
        print("\n**Bold** marks the fastest codec in that row. The two delta "
              "columns are how much faster Base85N is than that codec -- "
              "**positive is faster**, negative means Base85N is slower.\n")

    if old is None:
        return

    print("### Against the previous release\n")
    print("| input | encode MB/s | | decode MB/s | |")
    print("|---|---|---|---|---|")
    rows = []
    for n in names:
        k = (n, "Base85N")
        if k in old and k in new:
            rows.append((n, old[k][0], new[k][0], old[k][1], new[k][1]))
    for n, oe, ne, od, nd in sorted(rows, key=lambda x: -(x[2] / x[1])):
        print("| %s | %.0f -> %.0f | %.1fx | %.0f -> %.0f | %.1fx |"
              % (pretty(n), oe, ne, ne / oe, od, nd, nd / od))


if __name__ == "__main__":
    main()
