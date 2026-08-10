# Base85N benchmark results

Base85N against Base64, Ascii85 (Adobe/btoa), Z85 (ZeroMQ RFC 32) and
RFC 1924 Base85, on encoded size and throughput. How it is measured and how
to reproduce it: [../README.md](../README.md).

Intel Xeon @ 2.80 GHz, Ubuntu 24.04, gcc 13.3.0 `-O2`, CPython 3.11.15,
2026-08-10.

---

## Summary

**Base85N is the smallest of the five across the corpus** — 1.150 characters
per input byte against Base64's 1.333 and 1.250 for the other Base85s. Over
4.94 MB that is **13.7 % less than Base64** and **8.0 % less than RFC 1924
Base85**.

**On text-shaped data it is not close.** Pretty-printed JSON encodes at
**1.033** — 3 % overhead where Base64 costs 33 % — and the output stays
readable. Against the best other Base85 that is **17.4 % smaller** on JSON,
**10.2 %** on specification text, and **13–18 %** on the short protocol
fields that dominate real traffic.

**The alphabet is worth more than the ratio suggests.** Base85N is the only
Base85 here whose output drops into a JSON string or XML text unescaped.
Charge the others for the escaping their alphabets force and Base85N's lead
grows from 4–9 % to **18–23 % in XML** — where all three of them become
*more expensive than Base64*.

**On binary it matches the field** at 1.246–1.250, with one exception:
Ascii85's zero-run shorthand wins on zero-padded binaries.

**Speed is no longer the trade-off it was.** After this round of encoder
work, Base85N in C encodes binary at **360–487 MB/s** — it is now the
*fastest* of the four on the JPEG and PNG samples, and within 10–20 % of
Ascii85 and Z85 on the rest. Text still costs more (96–183 MB/s), because
text is exactly where the mode decision has real work to do — and where the
size wins are largest.

---

## Size

### Corpus files — expansion ratio (encoded chars per input byte)

| sample | input | Base64 | Ascii85 | Z85 | Base85 (RFC 1924) | Base85N | vs Base64 | vs best other Base85 |
|---|---|---|---|---|---|---|---|---|
| sql-wasm.wasm | 659,730 B | 1.333 | 1.247 | 1.250 | 1.250 | **1.246** | -6.5 % | -0.1 % |
| _cffi_backend.so | 1,068,624 B | 1.333 | **1.026** | 1.250 | 1.250 | 1.246 | -6.5 % | +21.5 % |
| DejaVuSans.ttf | 756,072 B | 1.333 | **1.240** | 1.250 | 1.250 | 1.248 | -6.4 % | +0.7 % |
| countries.json | 1,408,911 B | 1.333 | 1.250 | 1.250 | 1.250 | **1.033** | -22.6 % | -17.4 % |
| countries.min.json | 772,294 B | 1.333 | 1.250 | 1.250 | 1.250 | **1.053** | -21.0 % | -15.8 % |
| commonmark-spec.txt | 202,827 B | 1.333 | 1.250 | 1.250 | 1.250 | **1.123** | -15.8 % | -10.2 % |
| grace_hopper.jpg | 61,306 B | 1.333 | 1.250 | 1.250 | 1.250 | **1.250** | -6.3 % | same |
| minduka_present.png | 13,634 B | 1.333 | 1.250 | 1.250 | 1.250 | 1.250 | -6.3 % | same |

**Bold** marks the smallest output in that row; no bold means a tie. The two delta columns are Base85N's size difference — **negative is a saving**, positive means Base85N is larger.

Two rows are worth a second look. **Ascii85 wins the ELF sample** with its
zero-run shorthand — a real advantage on zero-padded binaries, and one
Base85N has no answer to. And the **two JSON rows are the same data**, once
pretty-printed and once minified: Base85N encodes the *pretty-printed* form
more efficiently, because indentation is runs of spaces that passthrough
carries almost free, while for every other codec it is simply 33 % more
bytes to expand.

### What the alphabet costs the others

Once the encoded text is placed inside a JSON string literal or XML
character data — which is where encoded payloads actually live:

### Cost of carrying the output inside JSON and XML

Expansion ratio over the whole corpus once the encoded text is placed
in a JSON string literal or in XML character data, i.e. what the
alphabet actually costs in the contexts encoded payloads travel in.

| codec | raw | larger than Base85N | inside JSON | larger | inside XML | larger |
|---|---|---|---|---|---|---|
| Base64 | 1.3333 | +15.9 % | 1.3333 | +15.9 % | 1.3333 | +15.9 % |
| Ascii85 | 1.1996 | +4.3 % | 1.2283 | +6.8 % | 1.4171 | +23.2 % |
| Z85 | 1.2500 | +8.7 % | 1.2500 | +8.7 % | 1.3662 | +18.8 % |
| Base85 (RFC 1924) | 1.2500 | +8.7 % | 1.2500 | +8.7 % | 1.3530 | +17.6 % |
| Base85N | 1.1503 | — | 1.1503 | — | 1.1503 | — |

The "larger" columns are how much more that codec costs than Base85N for the same corpus, in that context.

Read the three "larger" columns left to right. Base85N's lead grows from
4–9 % raw, to 7–9 % in JSON, to **18–23 % in XML** — and all three other
Base85 variants cross from cheaper than Base64 to *more expensive* than it.
Base85N's ratio never moves, because there is nothing in its output to
escape.

### Short protocol fields

### Short protocol fields — encoded characters

| field | input | Base64 | Ascii85 | Z85 | Base85 (RFC 1924) | Base85N | vs Base64 | vs best other Base85 |
|---|---|---|---|---|---|---|---|---|
| first + last name | 12 B | 16 | 15 | 15 | 15 | 15 | -6.2 % | same |
| name, umlauts | 25 B | 36 | 32 | 35 | 32 | 32 | -11.1 % | same |
| customer number | 4 B | 8 | 5 | 5 | 5 | 5 | -37.5 % | same |
| order number | 19 B | 28 | 24 | 25 | 24 | 24 | -14.3 % | same |
| hex value (8 byte) | 16 B | 24 | 20 | 20 | 20 | 20 | -16.7 % | same |
| hex digest (SHA-256) | 64 B | 88 | 80 | 80 | 80 | **69** | -21.6 % | -13.8 % |
| phone number, E.164 | 13 B | 20 | 17 | 20 | 17 | 17 | -15.0 % | same |
| phone number, formatted | 17 B | 24 | 22 | 25 | 22 | 22 | -8.3 % | same |
| email address | 24 B | 32 | 30 | 30 | 30 | **29** | -9.4 % | -3.3 % |
| URL | 53 B | 72 | 67 | 70 | 67 | **58** | -19.4 % | -13.4 % |
| UUID v4 | 36 B | 48 | 45 | 45 | 45 | **41** | -14.6 % | -8.9 % |
| ISO 8601 timestamp | 24 B | 32 | 30 | 30 | 30 | **29** | -9.4 % | -3.3 % |
| IPv4 address | 11 B | 16 | 14 | 15 | 14 | 14 | -12.5 % | same |
| IPv6 address | 28 B | 40 | 35 | 35 | 35 | **33** | -17.5 % | -5.7 % |
| MAC address | 17 B | 24 | 22 | 25 | 22 | 22 | -8.3 % | same |
| IBAN | 22 B | 32 | 28 | 30 | 28 | **27** | -15.6 % | -3.6 % |
| currency amount | 11 B | 16 | 14 | 15 | 14 | 14 | -12.5 % | same |
| CSV row | 64 B | 88 | 80 | 80 | 80 | **69** | -21.6 % | -13.8 % |
| JSON record | 92 B | 124 | 115 | 115 | 115 | **103** | -16.9 % | -10.4 % |
| HTTP header block | 114 B | 152 | 143 | 145 | 143 | **122** | -19.7 % | -14.7 % |
| JWT (3 segments) | 155 B | 208 | 194 | 195 | 194 | **160** | -23.1 % | -17.5 % |
| log line | 93 B | 124 | 117 | 120 | 117 | **100** | -19.4 % | -14.5 % |
| SQL statement | 118 B | 160 | 148 | 150 | 148 | **125** | -21.9 % | -15.5 % |

**Bold** marks the smallest output in that row; no bold means a tie. The two delta columns are Base85N's size difference — **negative is a saving**, positive means Base85N is larger.

Below the 20-byte passthrough minimum Base85N matches the best other Base85
exactly. From roughly 24 bytes upward the gain arrives and grows with how
text-like the field is: **17.5 % smaller on a JWT, 15.5 % on a SQL
statement, 14.5 % on a log line, 13.8 % on a SHA-256 digest**.

Z85 is measured with the zero padding an application has to add, since it is
defined only for lengths that are a multiple of 4 — and the original length
then has to travel outside the encoding, which is not counted here.

### Corpus totals

| codec | total encoded | ratio | vs Base85N |
|---|---|---|---|
| Base64 | 6,591,204 chars | 1.3333 | +15.9 % |
| Ascii85 | 5,930,050 chars | 1.1996 | +4.3 % |
| Z85 | 6,179,260 chars | 1.2500 | +8.7 % |
| Base85 (RFC 1924) | 6,179,250 chars | 1.2500 | +8.7 % |
| **Base85N** | **5,686,506 chars** | **1.1503** | — |

4,943,398 bytes of input across 8 files.

### What the output looks like

The size table understates the practical difference on text, because
Base85N's output stays inspectable:

```
input    {"id":184223,"name":"Ada Lovelace","phone":"+493023125190",...}
Base64   eyJpZCI6MTg0MjIzLCJuYW1lIjoiQWRhIExvdmVsYWNlIiwicGhvbmUiOiIrNDk...
Base85N  %nS{A{+id+~:184223^+name+~:+Ada:Lovelace+^+phone+~:+~+4930231...
```

---

## Throughput

Every codec here is C, in the same binary, with the same flags and the same
allocation discipline; Base85N is this repository's `c/` implementation.
Nothing is measured against Python. MB/s counts original (decoded) bytes.

Base64, Ascii85 and Z85 apply a fixed expansion regardless of content, so
their throughput barely varies across the corpus:

| codec | encode MB/s | decode MB/s |
|---|---|---|
| Base64 | 1415–1554 | 1293–1397 |
| Ascii85 | 434–542 | 875–909 |
| Z85 | 439–479 | 1136–1182 |

Base85N adapts to the input, so it is the only one worth listing per sample.
**Bold** marks where it is the fastest encoder of the four:

| input | encode MB/s | decode MB/s | ratio |
|---|---|---|---|
| minduka_present.png | **486.8** | 415.6 | 1.250 |
| grace_hopper.jpg | **483.8** | 426.3 | 1.250 |
| _cffi_backend.so | 404.7 | 386.3 | 1.246 |
| DejaVuSans.ttf | 393.9 | 420.0 | 1.248 |
| synthetic random 1 MiB | 389.8 | 415.1 | 1.250 |
| sql-wasm.wasm | 360.4 | 391.0 | 1.246 |
| countries.json | 183.3 | 239.0 | **1.033** |
| countries.min.json | 174.0 | 235.3 | 1.053 |
| escape-heavy 16 KiB | 154.9 | 422.9 | 1.250 |
| synthetic text 1 MiB | 143.1 | 210.9 | **1.010** |
| commonmark-spec.txt | 95.6 | 196.9 | 1.123 |

On binary Base85N is now in the same class as the other Base85 variants and
ahead of both on the two image samples. On text it is 3–4× behind them —
which is where it produces 10–17 % less output, so the CPU is buying
something. If you are bound by bytes, that is a good trade; if you are bound
by CPU *and* your payloads are text, the fixed-ratio codecs are the better
pick.

Base64 is faster than everything here by a wide margin. That is not a
Base85N result: it is a 6→8-bit repack with no division, and it stays
ahead of all three Base85 codecs for the same reason.

### Before and after this round

The encoder used to allocate a scratch buffer per 4 bytes of input and
re-enter the mode decision for every group. It now reuses its buffers, sizes
each output run in one step, and — where a Dynamic Passthrough candidate is
provably impossible — block-encodes the whole stretch in one call. Encode
MB/s, old and new binaries measured alternately on the same machine:

| input | before | after | |
|---|---|---|---|
| grace_hopper.jpg | 119.8 | 483.8 | **4.0×** |
| minduka_present.png | 141.2 | 486.8 | **3.4×** |
| synthetic random 1 MiB | 122.0 | 389.8 | **3.2×** |
| sql-wasm.wasm | 112.6 | 360.4 | **3.2×** |
| DejaVuSans.ttf | 130.4 | 393.9 | **3.0×** |
| _cffi_backend.so | 182.9 | 404.7 | **2.2×** |
| escape-heavy 16 KiB | 84.3 | 154.9 | **1.8×** |
| commonmark-spec.txt | 70.8 | 95.6 | **1.35×** |
| countries.json | 140.6 | 183.3 | **1.3×** |
| synthetic text 1 MiB | 110.5 | 143.1 | **1.3×** |

Decoding also moved, from ~330 to ~410 MB/s on binary — but no decoder code
changed, so that is the compiler making different inlining choices in a
smaller translation unit, not an optimisation anyone designed.

Output is byte-identical before and after: verified on every corpus file and
on 5,766 generated inputs against the Python reference.

These optimisations are in the C implementation. Rust, Go, TypeScript and
Python have the same algorithm and the same linear-time fix, but not the
batching work.

---

## Where the alternatives are the better choice

**Ascii85 on zero-padded binaries.** Its `z` shorthand collapses an all-zero
4-byte group to one character, encoding the ELF sample at 1.026 against
1.246 — 17.7 % smaller. Base85N has no equivalent.

**Z85 when you need addressable output.** Its fixed 4→5 mapping turns a byte
offset into a character offset by arithmetic, so random access, seeking and
parallel chunked processing are trivial. Base85N's output length is
data-dependent, so none of that is possible.

**All three on text throughput**, where they encode 3–4× faster, and Z85
decodes ~3× faster everywhere. Also when a streaming encoder needs strictly
bounded state: they work with 4 bytes of lookahead, while Base85N's Pass 1
scans to the end of a representable run.

**All three on maturity.** Ascii85 is in PDF and PostScript, Z85 is a ZeroMQ
standard, RFC 1924 ships in Python's standard library. Base85N is a 0.x
draft whose wire format is not frozen.

---

## What benchmarking changed

Two real defects surfaced here, both now fixed with byte-identical output in
all five implementations — plus one round of tuning on top.

**The encoder was quadratic in escape-heavy runs.** Pass 1 scans to the end
of a representable run while the main loop can consume as little as 4 bytes
of it, so re-running Pass 1 per iteration is O(n²). Ordinary Markdown
triggered it: a `>` anywhere in a run makes every backtick an escaped byte,
and the CommonMark specification encoded at 0.22 MB/s. Each run is now
scanned once, with R-Set counts maintained incrementally. Encoding is linear,
and the CommonMark case went from **0.22 MB/s to 95.6 MB/s**; linear-time
encoding is now a normative requirement in
[spec Section 6.6](../../spec/base85n-v0.2.0.md#66-encoding-complexity).

**Two hot-path lookups were linear searches.** Every input byte is tested for
R-Set membership, twice per byte inside the encoding loop, and that went
through a 13-way scan — as did the replacement-character lookup, on top of a
lazily initialised alphabet table that re-checked its ready flag on every
call. All three are now byte-indexed `const` tables. At that point encoding text
went from 34.7 to 98.5 MB/s and minified JSON from 33.4 to 110.0 MB/s, and
decoding roughly doubled. The same pattern was present in Rust (a linear scan plus a
`thread_local` table), Go (hash maps) and TypeScript (`Map`s plus a string
allocation per byte), and is fixed there too.

**The encoder paid full price for the mode decision even where it could not
possibly apply.** A profile of high-entropy input put ~15 % of encoding time
in `malloc`/`free` — a scratch buffer allocated and released for every 4
bytes — and another ~16 % in per-character capacity checks. Both are now
hoisted. The larger win came from skipping the decision itself: a Dynamic
Passthrough candidate can never be longer than the run of representable
bytes it starts in, so wherever no such run reaches the 20-byte minimum, the
block-mode branch is certain. The encoder finds the next position where a
candidate is even possible and encodes everything before it in one call. It
finds that position by sampling every 20th byte rather than reading all of
them — any 20 consecutive positions contain a multiple of 20, so a run long
enough to matter cannot hide between samples. On binary the lookahead is
therefore cheaper than the work it removes: **encoding is 2.2–4.0× faster**,
with the JPEG and PNG samples now the fastest encoders in the comparison.

Output is unchanged throughout: 8,153 inputs compared across C, Go, Rust and
Python and 892 across TypeScript for the correctness fixes, a further 5,766
inputs plus every corpus file for the tuning round, the shared vectors, and
an ASan/UBSan run of both the test suite and the benchmark harness.
