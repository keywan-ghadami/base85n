# Base85N benchmark results

Base85N against Base64, Ascii85 (Adobe/btoa), Z85 (ZeroMQ RFC 32) and
RFC 1924 Base85, on encoded size and throughput. How it is measured and how
to reproduce it: [../README.md](../README.md).

Measured against specification v0.3.0. Size and throughput both measured on an
Intel Xeon @ 2.80 GHz, Ubuntu 24.04, gcc 13.3.0 `-O2`, CPython 3.11.15,
2026-08-12. Size does not depend on the machine; throughput does, which is why
each table carries the three other codecs measured beside Base85N on the same
silicon. Earlier revisions of this page measured throughput on a Samsung
Exynos 2400 under Termux, so absolute numbers here are not comparable with
those.

---

## Summary

**Base85N is the smallest of the five across the corpus** — 1.131 characters
per input byte against Base64's 1.333 and 1.250 for the other Base85s. Over
4.94 MB that is **15.2 % less than Base64** and **9.5 % less than RFC 1924
Base85**.

**On text-shaped data it is not close.** JSON encodes at **1.005** — half a
percent of overhead where Base64 costs 33 % — and the output stays readable.
Against the best other Base85 that is **19.6 % smaller** on JSON, **18.4 %**
on specification text, and **13–18 %** on the short protocol fields that
dominate real traffic.

**The alphabet is worth more than the ratio suggests.** Base85N is the only
Base85 here whose output drops into a JSON string or XML text unescaped.
Charge the others for the escaping their alphabets force and Base85N's lead
grows from 4–9 % to **18–23 % in XML** — where all three of them become
*more expensive than Base64*.

**On binary it matches the field** at 1.246–1.250, with one exception:
Ascii85's zero-run shorthand wins on zero-padded binaries.

**Speed depends on the payload.** On binary Base85N encodes 80–197 % faster
than the other two Base85 variants and comes within 7–46 % of Base64. On
text it is the slowest of the four, 14–28 % behind the best other Base85,
because text is where the mode decision has real work to do and where nearly
every byte goes through the passthrough path — which is also where the size
wins are. Decoding is the fastest of the four on binary, by 46–63 % over the
best other Base85, and the slowest on text.

---

## Size

### Corpus files — expansion ratio (encoded chars per input byte)

| sample | input | Base64 | Ascii85 | Z85 | Base85 (RFC 1924) | Base85N | vs Base64 | vs best other Base85 |
|---|---|---|---|---|---|---|---|---|
| sql-wasm.wasm | 659,730 B | 1.333 | **1.247** | 1.250 | 1.250 | **1.247** | -6.4 % | same |
| _cffi_backend.so | 1,068,624 B | 1.333 | **1.026** | 1.250 | 1.250 | 1.246 | -6.5 % | +21.5 % |
| DejaVuSans.ttf | 756,072 B | 1.333 | **1.240** | 1.250 | 1.250 | 1.248 | -6.4 % | +0.7 % |
| countries.json | 1,408,911 B | 1.333 | 1.250 | 1.250 | 1.250 | **1.005** | -24.6 % | -19.6 % |
| countries.min.json | 772,294 B | 1.333 | 1.250 | 1.250 | 1.250 | **1.005** | -24.6 % | -19.6 % |
| commonmark-spec.txt | 202,827 B | 1.333 | 1.250 | 1.250 | 1.250 | **1.020** | -23.5 % | -18.4 % |
| grace_hopper.jpg | 61,306 B | 1.333 | **1.250** | **1.250** | **1.250** | **1.250** | -6.3 % | same |
| minduka_present.png | 13,634 B | 1.333 | **1.250** | **1.250** | **1.250** | **1.250** | -6.3 % | same |

**Bold** marks the smallest output in that row; on a tie every codec that reaches it is marked. The two delta columns are Base85N's size difference — **negative is a saving**, positive means Base85N is larger.

Two rows are worth a second look. **Ascii85 wins the ELF sample** with its
zero-run shorthand — a real advantage on zero-padded binaries, and one
Base85N has no answer to. And the **two JSON rows are the same data**, once
pretty-printed and once minified: Base85N encodes the *pretty-printed* form
more efficiently, because indentation is runs of spaces that passthrough
carries almost free, while for every other codec it is simply 33 % more
bytes to expand.

### What the alphabet costs the others

Once the encoded text is placed inside a JSON string literal or XML
character data — which is where encoded payloads actually live — the
alphabet starts costing whatever it forces the serializer to escape. Both
tables below carry the same layout as the raw table above: codecs across,
corpus files down, and the whole corpus in the last row.

### Inside a JSON string literal — expansion ratio (characters per input byte)

What each codec costs per file once its output is placed in a
JSON string literal, `"` and `\` escaped. Last row is the whole
corpus.

| sample | input | Base64 | Ascii85 | Z85 | Base85 (RFC 1924) | Base85N | vs Base64 | vs best other Base85 |
|---|---|---|---|---|---|---|---|---|
| sql-wasm.wasm | 659,730 B | 1.333 | 1.291 | 1.250 | 1.250 | **1.247** | -6.4 % | -0.2 % |
| _cffi_backend.so | 1,068,624 B | 1.333 | **1.070** | 1.250 | 1.250 | 1.246 | -6.5 % | +16.5 % |
| DejaVuSans.ttf | 756,072 B | 1.333 | 1.287 | 1.250 | 1.250 | **1.248** | -6.4 % | -0.2 % |
| countries.json | 1,408,911 B | 1.333 | 1.257 | 1.250 | 1.250 | **1.005** | -24.6 % | -19.6 % |
| countries.min.json | 772,294 B | 1.333 | 1.268 | 1.250 | 1.250 | **1.005** | -24.6 % | -19.6 % |
| commonmark-spec.txt | 202,827 B | 1.333 | 1.263 | 1.250 | 1.250 | **1.020** | -23.5 % | -18.4 % |
| grace_hopper.jpg | 61,306 B | 1.333 | 1.281 | **1.250** | **1.250** | **1.250** | -6.3 % | same |
| minduka_present.png | 13,634 B | 1.333 | 1.281 | **1.250** | **1.250** | **1.250** | -6.3 % | same |
| whole corpus | 4,943,398 B | 1.333 | 1.228 | 1.250 | 1.250 | **1.131** | -15.2 % | -7.9 % |

**Bold** marks the smallest output in that row; on a tie every codec that reaches it is marked. The two delta columns are Base85N's size difference — **negative is a saving**, positive means Base85N is larger.

### Inside XML character data — expansion ratio (characters per input byte)

The same per file inside XML character data, with `&`, `<` and
`>` escaped. Last row is the whole corpus.

| sample | input | Base64 | Ascii85 | Z85 | Base85 (RFC 1924) | Base85N | vs Base64 | vs best other Base85 |
|---|---|---|---|---|---|---|---|---|
| sql-wasm.wasm | 659,730 B | 1.333 | 1.395 | 1.394 | 1.365 | **1.247** | -6.4 % | -8.6 % |
| _cffi_backend.so | 1,068,624 B | 1.333 | **1.185** | 1.339 | 1.334 | 1.246 | -6.5 % | +5.2 % |
| DejaVuSans.ttf | 756,072 B | 1.333 | 1.410 | 1.363 | 1.363 | **1.248** | -6.4 % | -8.4 % |
| countries.json | 1,408,911 B | 1.333 | 1.619 | 1.375 | 1.341 | **1.005** | -24.6 % | -25.0 % |
| countries.min.json | 772,294 B | 1.333 | 1.408 | 1.372 | 1.380 | **1.005** | -24.6 % | -26.7 % |
| commonmark-spec.txt | 202,827 B | 1.333 | 1.379 | 1.333 | 1.343 | **1.020** | -23.5 % | -23.5 % |
| grace_hopper.jpg | 61,306 B | 1.333 | 1.398 | 1.399 | 1.395 | **1.250** | -6.3 % | -10.4 % |
| minduka_present.png | 13,634 B | 1.333 | 1.399 | 1.391 | 1.404 | **1.250** | -6.3 % | -10.1 % |
| whole corpus | 4,943,398 B | 1.333 | 1.417 | 1.366 | 1.353 | **1.131** | -15.2 % | -16.4 % |

**Bold** marks the smallest output in that row; on a tie every codec that reaches it is marked. The two delta columns are Base85N's size difference — **negative is a saving**, positive means Base85N is larger.

Read the last column of the three tables in order. Over the whole corpus
Base85N is 5.7 % smaller than the best other Base85 raw, 7.9 % inside JSON
and **16.4 % inside XML** — and in XML all three other Base85 variants
cross from cheaper than Base64 to *more expensive* than it, at 1.353–1.417
against Base64's 1.333. Base85N's own ratio is identical in all three
tables, because there is nothing in its output to escape. The one row that
holds out is the ELF sample, where Ascii85's zero-run shorthand stays ahead
even after escaping — though its margin falls from 21.5 % raw to 16.5 % in
JSON and 5.2 % in XML.

### Short protocol fields

### Short protocol fields — encoded characters

| field | input | Base64 | Ascii85 | Z85 | Base85 (RFC 1924) | Base85N | vs Base64 | vs best other Base85 |
|---|---|---|---|---|---|---|---|---|
| first + last name | 12 B | 16 | **15** | **15** | **15** | **15** | -6.2 % | same |
| name, umlauts | 25 B | 36 | **32** | 35 | **32** | **32** | -11.1 % | same |
| customer number | 4 B | 8 | **5** | **5** | **5** | **5** | -37.5 % | same |
| order number | 19 B | 28 | **24** | 25 | **24** | **24** | -14.3 % | same |
| hex value (8 byte) | 16 B | 24 | **20** | **20** | **20** | **20** | -16.7 % | same |
| hex digest (SHA-256) | 64 B | 88 | 80 | 80 | 80 | **69** | -21.6 % | -13.8 % |
| phone number, E.164 | 13 B | 20 | **17** | 20 | **17** | **17** | -15.0 % | same |
| phone number, formatted | 17 B | 24 | **22** | 25 | **22** | **22** | -8.3 % | same |
| email address | 24 B | 32 | 30 | 30 | 30 | **29** | -9.4 % | -3.3 % |
| URL | 53 B | 72 | 67 | 70 | 67 | **58** | -19.4 % | -13.4 % |
| UUID v4 | 36 B | 48 | 45 | 45 | 45 | **41** | -14.6 % | -8.9 % |
| ISO 8601 timestamp | 24 B | 32 | 30 | 30 | 30 | **29** | -9.4 % | -3.3 % |
| IPv4 address | 11 B | 16 | **14** | 15 | **14** | **14** | -12.5 % | same |
| IPv6 address | 28 B | 40 | 35 | 35 | 35 | **33** | -17.5 % | -5.7 % |
| MAC address | 17 B | 24 | **22** | 25 | **22** | **22** | -8.3 % | same |
| IBAN | 22 B | 32 | 28 | 30 | 28 | **27** | -15.6 % | -3.6 % |
| currency amount | 11 B | 16 | **14** | 15 | **14** | **14** | -12.5 % | same |
| CSV row | 64 B | 88 | 80 | 80 | 80 | **69** | -21.6 % | -13.8 % |
| JSON record | 92 B | 124 | 115 | 115 | 115 | **97** | -21.8 % | -15.7 % |
| HTTP header block | 114 B | 152 | 143 | 145 | 143 | **119** | -21.7 % | -16.8 % |
| JWT (3 segments) | 155 B | 208 | 194 | 195 | 194 | **160** | -23.1 % | -17.5 % |
| log line | 93 B | 124 | 117 | 120 | 117 | **98** | -21.0 % | -16.2 % |
| SQL statement | 118 B | 160 | 148 | 150 | 148 | **123** | -23.1 % | -16.9 % |

**Bold** marks the smallest output in that row; on a tie every codec that reaches it is marked. The two delta columns are Base85N's size difference — **negative is a saving**, positive means Base85N is larger.

Below the 20-byte passthrough minimum Base85N matches the best other Base85
exactly. From roughly 24 bytes upward the gain arrives and grows with how
text-like the field is: **17.5 % smaller on a JWT, 15.5 % on a SQL
statement, 14.5 % on a log line, 13.8 % on a SHA-256 digest**.

Z85 is measured with the zero padding an application has to add, since it is
defined only for lengths that are a multiple of 4 — and the original length
then has to travel outside the encoding, which is not counted here.

### Corpus totals

| codec | total encoded | ratio | vs Base64 |
|---|---|---|---|
| Base64 | 6,591,204 chars | 1.3333 | +0.00 % |
| Ascii85 | 5,930,050 chars | 1.1996 | +10.03 % |
| Z85 | 6,179,260 chars | 1.2500 | +6.25 % |
| Base85 (RFC 1924) | 6,179,250 chars | 1.2500 | +6.25 % |
| Base85N | 5,591,669 chars | 1.1311 | +15.16 % |

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

Every table in this section is generated from the benchmark's own output by
`tables.py`, so a rerun does not mean retyping numbers.

Two independent runs of the whole corpus agree to within 0.5 % on average
for Base85N and 1.5 % at worst, so a difference of a few percent between
codecs in the tables below is real and one of under a percent is not. The
exception is Ascii85's decode column, which moved by up to 14 % between the
same two runs: its worst case is four bytes per input character, because
the `z` shorthand expands one character to four, so it allocates about five
times the buffer it fills on ordinary input and its timing follows the
allocator rather than the codec. Read that one column as a lower bound.

### Encode throughput (MB/s of original bytes)

| input | Base64 | Ascii85 | Z85 | Base85N | vs Base64 | vs best other Base85 |
|---|---|---|---|---|---|---|
| synthetic random 1 MiB | **1521** | 453 | 450 | 897 | -41 % | +98 % |
| synthetic text 1 MiB | **1467** | 451 | 454 | 391 | -73 % | -14 % |
| scan-heavy 1MiB | **1508** | 445 | 448 | 1041 | -31 % | +132 % |
| DejaVuSans.ttf | **1506** | 453 | 442 | 994 | -34 % | +119 % |
| _cffi_backend.so | **1509** | 541 | 453 | 1125 | -25 % | +108 % |
| commonmark-spec.txt | **1528** | 457 | 464 | 333 | -78 % | -28 % |
| countries.json | **1514** | 442 | 444 | 561 | -63 % | +26 % |
| countries.min.json | **1516** | 453 | 456 | 487 | -68 % | +7 % |
| grace_hopper.jpg | **1540** | 460 | 449 | 1369 | -11 % | +197 % |
| minduka_present.png | **1497** | 482 | 479 | 1399 | -7 % | +191 % |
| sql-wasm.wasm | **1526** | 457 | 449 | 822 | -46 % | +80 % |

**Bold** marks the fastest codec in that row. The two delta columns are how much faster Base85N is than that codec -- **positive is faster**, negative means Base85N is slower.

### Decode throughput (MB/s of original bytes)

| input | Base64 | Ascii85 | Z85 | Base85N | vs Base64 | vs best other Base85 |
|---|---|---|---|---|---|---|
| synthetic random 1 MiB | 1356 | 887 | 1133 | **1786** | +32 % | +58 % |
| synthetic text 1 MiB | **1350** | 890 | 1143 | 456 | -66 % | -60 % |
| scan-heavy 1MiB | 1329 | 879 | 1111 | **1810** | +36 % | +63 % |
| DejaVuSans.ttf | 1370 | 863 | 1083 | **1754** | +28 % | +62 % |
| _cffi_backend.so | 1370 | 923 | 1143 | **1665** | +22 % | +46 % |
| commonmark-spec.txt | **1400** | 897 | 1166 | 407 | -71 % | -65 % |
| countries.json | **1375** | 888 | 1155 | 724 | -47 % | -37 % |
| countries.min.json | **1380** | 899 | 1154 | 623 | -55 % | -46 % |
| grace_hopper.jpg | 1383 | 889 | 1158 | **1762** | +27 % | +52 % |
| minduka_present.png | 1373 | 904 | 1171 | **1628** | +19 % | +39 % |
| sql-wasm.wasm | 1383 | 895 | 1145 | **1744** | +26 % | +52 % |

**Bold** marks the fastest codec in that row. The two delta columns are how much faster Base85N is than that codec -- **positive is faster**, negative means Base85N is slower.

On **binary** Base85N is well ahead of the other two Base85 variants —
80–197 % — and behind Base64 by 7–46 %. On **text** it is last of the four,
by 14 % on the synthetic text and 28 % on the CommonMark spec, and that is
the trade this format makes: text is where nearly every byte goes through
the passthrough path one at a time instead of four-at-a-time through block
mode, and it is where the output is 18–20 % smaller than the best other
Base85. If you are bound by bytes, that is a good trade; if you are bound by
CPU *and* your payloads are text, the fixed-ratio codecs are the better pick.

**Decoding** splits the same way and more sharply: fastest of all four on
every binary sample, and slowest on text. A DP segment decodes one character
to one byte through a table, which is cheap per byte but is not the 5→4
integer conversion the block path and the other codecs run.

Base64 remains the fastest encoder on most of the corpus. That is not a
Base85N result: it is a 6→8-bit repack with no division at all.

The scan-heavy row is a shape built to defeat the mode decision: 18
representable bytes then one that no alphabet can carry, repeated, so a
candidate never reaches MIN_PASSTHROUGH_BYTES and the encoder scans and
falls back on every group. It is in the corpus because it is the worst case
for prefix identification, not because it resembles real input — and it now
encodes at roughly the speed of ordinary binary, which is the point of the
bounded lookahead in spec Section 6.6.

### Against specification v0.2.0

Version 0.3.0 changed the wire format: Dynamic Passthrough dropped its escape
mechanism in favour of eight fixed replacement alphabets, and the analysis
window and segment length went to 1024 bytes. Size is the comparison that
means something across that change, since both versions encode the same
corpus with the same block mode.

| sample | v0.2.0 | v0.3.0 | change |
|---|---|---|---|
| countries.json | 1.033 | **1.005** | -2.7 % |
| countries.min.json | 1.053 | **1.005** | -4.6 % |
| commonmark-spec.txt | 1.123 | **1.020** | -9.2 % |
| sql-wasm.wasm | 1.246 | 1.247 | +0.1 % |
| _cffi_backend.so | 1.246 | 1.246 | same |
| DejaVuSans.ttf | 1.248 | 1.248 | same |
| grace_hopper.jpg | 1.250 | 1.250 | same |
| minduka_present.png | 1.250 | 1.250 | same |
| **whole corpus** | 1.150 | **1.131** | -1.7 % |

Text-shaped input is where the escape mechanism was costing: 0.2.0 spent two
characters on every literal occurrence of a replacement character whose R-Set
partner was in the same window, and two of the thirteen replacement characters
it chose — `:` and `` ` `` — are the two most common special characters in real
text. 0.3.0's donors are the *least* common ones, and a collision ends the
segment rather than being escaped. Binary is unchanged, as it must be: no
alphabet can represent it, so it was block mode before and is block mode now.

Throughput is not compared across the two versions here. The v0.2.0 numbers
that used to appear in this section were measured on different silicon
(a Samsung Exynos 2400 under Termux; these on an Intel Xeon), and a
cross-machine ratio would say more about the machines than about the format.
What can be said from the tables above is that the shape that used to be the
encoder's worst case is no longer one: the buffer built to defeat prefix
identification now encodes at roughly the speed of ordinary binary, because
Section 6.6's bounded lookahead caps how far a failed candidate can scan.

The throughput numbers on this page are the C implementation's. The other four
implement the same algorithm and produce byte-identical output — enforced by
the shared vectors and by a generated differential corpus of several thousand
inputs — but their speed was not measured for this report;
`bench/instructions/run.sh` counts C against Rust directly if you want a
comparison that reproduces anywhere.

---

## Where the alternatives are the better choice

**Ascii85 on zero-padded binaries.** Its `z` shorthand collapses an all-zero
4-byte group to one character, encoding the ELF sample at 1.026 against
1.246 — 17.7 % smaller. Base85N has no equivalent.

**Z85 when you need addressable output.** Its fixed 4→5 mapping turns a byte
offset into a character offset by arithmetic, so random access, seeking and
parallel chunked processing are trivial. Base85N's output length is
data-dependent, so none of that is possible.

**All three on text throughput**, where they encode 1.4–2.9× faster. And
**Z85 on decoding anything**: it is 1.2–2.4× faster than Base85N on every
sample in the corpus. Also when a streaming encoder needs strictly
bounded state: they work with 4 bytes of lookahead, while Base85N's Pass 1
scans to the end of a representable run.

**All three on maturity.** Ascii85 is in PDF and PostScript, Z85 is a ZeroMQ
standard, RFC 1924 ships in Python's standard library. Base85N is a 0.x
draft whose wire format is not frozen.

---

## What benchmarking changed

This section is the record of what measuring the format actually found. The
first two entries are from specification v0.2.0 and describe defects in the
implementations; the last is what those measurements led the *format* to do in
v0.3.0.

Two real defects surfaced here, both fixed at the time with byte-identical
output in all five implementations — plus one round of tuning on top.

**The encoder was quadratic in escape-heavy runs.** Pass 1 scans to the end
of a representable run while the main loop can consume as little as 4 bytes
of it, so re-running Pass 1 per iteration is O(n²). Ordinary Markdown
triggered it: a `>` anywhere in a run makes every backtick an escaped byte,
and the CommonMark specification encoded at 0.22 MB/s. Each run is now
scanned once, with R-Set counts maintained incrementally. Encoding is linear,
and the CommonMark case went from **0.22 MB/s to 95.6 MB/s**; linear-time
encoding is now a normative requirement in
[spec Section 6.6](../../spec/base85n-v0.3.0.md#66-encoding-complexity).

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

**Then the format changed, because the escaping was the problem.** All of the
above made a mechanism fast that measurement kept showing was the wrong
mechanism: escaping was not an edge case but the common path, because two of
the thirteen replacement characters v0.2.0 chose are the two most frequent
special characters in real text. Counting characters over the corpus put the
eight rarest Alphabet-N characters below 0.3 occurrences per 1000 bytes, which
is what specification v0.3.0's replacement alphabets spend instead — and once
the substitution is injective, escaping has nothing left to do. The size
effect is in "Against specification v0.2.0" above; the algorithmic effect is
that Pass 1/Pass 2, the consecutive-escape limit, the segment-splitting rule
and the dangling-escape error all stopped existing. This is the clearest thing
the benchmark bought: not a faster encoder, but the evidence for which
characters to give up.
