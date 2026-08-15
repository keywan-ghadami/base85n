# Base85N benchmark results

Base85N against Base64, Ascii85 (Adobe/btoa), Z85 (ZeroMQ RFC 32) and
RFC 1924 Base85, on encoded size and throughput. How it is measured and how
to reproduce it: [../README.md](../README.md).

Measured against specification v0.4.0. Size and throughput both measured on an
Intel Xeon, Ubuntu 24.04, gcc 13.3.0 `-O2`, 2026-08-15, over a 6.52 MB corpus
of 13 real files. Size does not depend on the machine; throughput does, which
is why each table carries the three other codecs measured beside Base85N on the
same silicon, and why the version-over-version comparison further down uses
instruction counts instead.

---

## Summary

**Base85N is the smallest of the five across the corpus** — 1.024 characters
per input byte against Base64's 1.333 and 1.250 for the other Base85s. Over
6.52 MB that is **23.2 % less than Base64** and **18.1 % less than RFC 1924
Base85**. It is the only one of the five whose corpus ratio is close to 1.0 at
all: Dynamic Passthrough spends one character per byte, and Solid Fill spends
five characters on runs that would otherwise cost hundreds.

**On structured text it now encodes to less than the input.** Pretty-printed
JSON lands at **0.892** and the CommonMark specification at **0.859** —
encoded output shorter than the bytes it encodes, because indentation and
repeated punctuation runs go through Fill. Against the best other Base85 that
is **28.6 %** and **31.3 %** smaller.

**Block-padded binaries are the other big win.** An uncompressed tar encodes at
**0.763** and a zero-padded ELF at **1.126**, where every fixed-ratio Base85
pays 1.250.

**The alphabet is worth more than the ratio suggests.** Base85N is the only
Base85 here whose output drops into a JSON string or XML text unescaped.
Charge the others for the escaping their alphabets force and Base85N's lead
over them grows to **32–37 % in XML** — where all three become *more expensive
than Base64*.

**Speed is the cost.** Base85N is the slowest encoder of the four on 14 of the
16 inputs: Base64 encodes 2–7× faster, and the other two Base85s are 12–49 %
faster on most rows. Decoding is competitive — fastest of the four on eight of
the sixteen — but the encoder does two scans and a per-segment table build that
the fixed-ratio codecs have no equivalent of. If you are bound by CPU rather
than by bytes, this is the wrong codec.

---

## Size

### Corpus files — expansion ratio (encoded chars per input byte)

| sample | input | Base64 | Ascii85 | Z85 | Base85 (RFC 1924) | Base85N | vs Base64 | vs best other Base85 |
|---|---|---|---|---|---|---|---|---|
| sql-wasm.wasm | 659,730 B | 1.333 | 1.247 | 1.250 | 1.250 | **1.242** | -6.8 % | -0.4 % |
| _cffi_backend.so | 1,068,624 B | 1.333 | **1.026** | 1.250 | 1.250 | 1.126 | -15.5 % | +9.8 % |
| DejaVuSans.ttf | 756,072 B | 1.333 | 1.240 | 1.250 | 1.250 | **1.237** | -7.2 % | -0.2 % |
| requests-2.32.3.tar | 655,360 B | 1.333 | 1.015 | 1.250 | 1.250 | **0.763** | -42.8 % | -24.8 % |
| countries.json | 1,408,911 B | 1.333 | 1.250 | 1.250 | 1.250 | **0.892** | -33.1 % | -28.6 % |
| countries.min.json | 772,294 B | 1.333 | 1.250 | 1.250 | 1.250 | **1.003** | -24.8 % | -19.8 % |
| lodash.js | 544,098 B | 1.333 | 1.250 | 1.250 | 1.250 | **1.003** | -24.8 % | -19.8 % |
| bootstrap.css | 281,046 B | 1.333 | 1.250 | 1.250 | 1.250 | **1.003** | -24.7 % | -19.7 % |
| requests-models.py | 35,418 B | 1.333 | 1.250 | 1.250 | 1.250 | **0.962** | -27.9 % | -23.1 % |
| commonmark-spec.txt | 202,827 B | 1.333 | 1.250 | 1.250 | 1.250 | **0.859** | -35.6 % | -31.3 % |
| requests-history.md | 60,368 B | 1.333 | 1.250 | 1.250 | 1.250 | **0.979** | -26.6 % | -21.7 % |
| grace_hopper.jpg | 61,306 B | 1.333 | 1.250 | 1.250 | 1.250 | **1.249** | -6.4 % | -0.1 % |
| minduka_present.png | 13,634 B | 1.333 | **1.250** | **1.250** | **1.250** | **1.250** | -6.3 % | same |

**Bold** marks the smallest output in that row; on a tie every codec that reaches it is marked. The two delta columns are Base85N's size difference — **negative is a saving**, positive means Base85N is larger.

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
| sql-wasm.wasm | 659,730 B | 1.333 | 1.291 | 1.250 | 1.250 | **1.242** | -6.8 % | -0.6 % |
| _cffi_backend.so | 1,068,624 B | 1.333 | **1.070** | 1.250 | 1.250 | 1.126 | -15.5 % | +5.2 % |
| DejaVuSans.ttf | 756,072 B | 1.333 | 1.287 | 1.250 | 1.250 | **1.237** | -7.2 % | -1.0 % |
| requests-2.32.3.tar | 655,360 B | 1.333 | 1.027 | 1.250 | 1.250 | **0.763** | -42.8 % | -25.7 % |
| countries.json | 1,408,911 B | 1.333 | 1.257 | 1.250 | 1.250 | **0.892** | -33.1 % | -28.6 % |
| countries.min.json | 772,294 B | 1.333 | 1.268 | 1.250 | 1.250 | **1.003** | -24.8 % | -19.8 % |
| lodash.js | 544,098 B | 1.333 | 1.265 | 1.250 | 1.250 | **1.003** | -24.8 % | -19.8 % |
| bootstrap.css | 281,046 B | 1.333 | 1.267 | 1.250 | 1.250 | **1.003** | -24.7 % | -19.7 % |
| requests-models.py | 35,418 B | 1.333 | 1.264 | 1.250 | 1.250 | **0.962** | -27.9 % | -23.1 % |
| commonmark-spec.txt | 202,827 B | 1.333 | 1.263 | 1.250 | 1.250 | **0.859** | -35.6 % | -31.3 % |
| requests-history.md | 60,368 B | 1.333 | 1.264 | 1.250 | 1.250 | **0.979** | -26.6 % | -21.7 % |
| grace_hopper.jpg | 61,306 B | 1.333 | 1.281 | 1.250 | 1.250 | **1.249** | -6.4 % | -0.1 % |
| minduka_present.png | 13,634 B | 1.333 | 1.281 | **1.250** | **1.250** | **1.250** | -6.3 % | same |
| whole corpus | 6,519,688 B | 1.333 | 1.213 | 1.250 | 1.250 | **1.024** | -23.2 % | -15.6 % |

**Bold** marks the smallest output in that row; on a tie every codec that reaches it is marked. The two delta columns are Base85N's size difference — **negative is a saving**, positive means Base85N is larger.

### Inside XML character data — expansion ratio (characters per input byte)

The same per file inside XML character data, with `&`, `<` and
`>` escaped. Last row is the whole corpus.

| sample | input | Base64 | Ascii85 | Z85 | Base85 (RFC 1924) | Base85N | vs Base64 | vs best other Base85 |
|---|---|---|---|---|---|---|---|---|
| sql-wasm.wasm | 659,730 B | 1.333 | 1.395 | 1.394 | 1.365 | **1.242** | -6.8 % | -9.0 % |
| _cffi_backend.so | 1,068,624 B | 1.333 | 1.185 | 1.339 | 1.334 | **1.126** | -15.5 % | -5.0 % |
| DejaVuSans.ttf | 756,072 B | 1.333 | 1.410 | 1.363 | 1.363 | **1.237** | -7.2 % | -9.2 % |
| requests-2.32.3.tar | 655,360 B | 1.333 | 1.174 | 1.320 | 1.333 | **0.763** | -42.8 % | -35.1 % |
| countries.json | 1,408,911 B | 1.333 | 1.619 | 1.375 | 1.341 | **0.892** | -33.1 % | -33.5 % |
| countries.min.json | 772,294 B | 1.333 | 1.408 | 1.372 | 1.380 | **1.003** | -24.8 % | -27.0 % |
| lodash.js | 544,098 B | 1.333 | 1.471 | 1.331 | 1.352 | **1.003** | -24.8 % | -24.7 % |
| bootstrap.css | 281,046 B | 1.333 | 1.442 | 1.338 | 1.359 | **1.003** | -24.7 % | -25.0 % |
| requests-models.py | 35,418 B | 1.333 | 1.516 | 1.335 | 1.340 | **0.962** | -27.9 % | -28.0 % |
| commonmark-spec.txt | 202,827 B | 1.333 | 1.379 | 1.333 | 1.343 | **0.859** | -35.6 % | -35.6 % |
| requests-history.md | 60,368 B | 1.333 | 1.390 | 1.346 | 1.384 | **0.979** | -26.6 % | -27.3 % |
| grace_hopper.jpg | 61,306 B | 1.333 | 1.398 | 1.399 | 1.395 | **1.249** | -6.4 % | -10.5 % |
| minduka_present.png | 13,634 B | 1.333 | 1.399 | 1.391 | 1.404 | **1.250** | -6.3 % | -10.1 % |
| whole corpus | 6,519,688 B | 1.333 | 1.399 | 1.357 | 1.351 | **1.024** | -23.2 % | -24.2 % |

**Bold** marks the smallest output in that row; on a tie every codec that reaches it is marked. The two delta columns are Base85N's size difference — **negative is a saving**, positive means Base85N is larger.

### Short protocol fields

Most encoded payloads in a real system are small, and small inputs are where a
block encoding's rounding-up to whole groups costs the most. These are authored
in `wire_samples.py` and need no download.

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

### Corpus totals

| codec | total encoded | ratio | vs Base64 |
|---|---|---|---|
| Base64 | 8,692,928 chars | 1.3333 | +0.00 % |
| Ascii85 | 7,746,174 chars | 1.1881 | +10.89 % |
| Z85 | 8,149,630 chars | 1.2500 | +6.25 % |
| Base85 (RFC 1924) | 8,149,614 chars | 1.2500 | +6.25 % |
| Base85N | 6,678,433 chars | 1.0243 | +23.17 % |

Total input: 6,519,688 bytes across 13 files.

### What the output looks like

The size table understates the practical difference on text, because
Base85N's output stays inspectable:

```
input    {"id":184223,"name":"Ada Lovelace","phone":"+493023125190",...}
Base64   eyJpZCI6MTg0MjIzLCJuYW1lIjoiQWRhIExvdmVsYWNlIiwicGhvbmUiOiIrNDkz
Base85N  %nVn3{^id^:184223?^name^:^Ada~Lovelace^?^phone^:^+493023125190^?
```

`^` stands in for `"`, `?` for `,` and `~` for the space; the leading `%nVn3`
is the signal that names them.

---

## Throughput

Every codec here is C, in the same binary, with the same flags and the same
allocation discipline; Base85N is this repository's `c/` implementation.
MB/s counts original (decoded) bytes.

Every table in this section is generated from the benchmark's own output by
`tables.py`, so a rerun does not mean retyping numbers.

This machine is virtualised and its run-to-run spread is wide: repeated runs of
the same binary moved by up to 20 % on some rows. Read a difference of tens of
percent as real and one of a few percent as noise — and for the comparison
between specification versions, read the instruction counts below instead.

### Encode throughput (MB/s of original bytes)

| input | Base64 | Ascii85 | Z85 | Base85N | vs Base64 | vs best other Base85 |
|---|---|---|---|---|---|---|
| synthetic random 1 MiB | **1939** | 512 | 541 | 403 | -79 % | -26 % |
| synthetic text 1 MiB | **1773** | 531 | 525 | 364 | -79 % | -31 % |
| scan-heavy 1MiB | **1823** | 525 | 539 | 610 | -67 % | +13 % |
| DejaVuSans.ttf | **2149** | 618 | 649 | 544 | -75 % | -16 % |
| _cffi_backend.so | **2018** | 661 | 556 | 529 | -74 % | -20 % |
| bootstrap.css | **1902** | 526 | 584 | 407 | -79 % | -30 % |
| commonmark-spec.txt | **2045** | 547 | 560 | 288 | -86 % | -49 % |
| countries.json | **2165** | 500 | 616 | 328 | -85 % | -47 % |
| countries.min.json | **1995** | 582 | 544 | 455 | -77 % | -22 % |
| grace_hopper.jpg | **1935** | 541 | 557 | 487 | -75 % | -12 % |
| lodash.js | **1921** | 529 | 558 | 327 | -83 % | -42 % |
| minduka_present.png | **1951** | 623 | 575 | 1103 | -43 % | +77 % |
| requests-2.32.3.tar | **1971** | 674 | 564 | 362 | -82 % | -46 % |
| requests-history.md | **1931** | 562 | 572 | 397 | -79 % | -31 % |
| requests-models.py | **2298** | 632 | 660 | 357 | -84 % | -46 % |
| sql-wasm.wasm | **2214** | 615 | 659 | 440 | -80 % | -33 % |

**Bold** marks the fastest codec in that row. The two delta columns are how much faster Base85N is than that codec -- **positive is faster**, negative means Base85N is slower.

### Decode throughput (MB/s of original bytes)

| input | Base64 | Ascii85 | Z85 | Base85N | vs Base64 | vs best other Base85 |
|---|---|---|---|---|---|---|
| synthetic random 1 MiB | **1826** | 1298 | 1711 | 1780 | -3 % | +4 % |
| synthetic text 1 MiB | 1804 | 1262 | 1642 | **2112** | +17 % | +29 % |
| scan-heavy 1MiB | 1826 | 1244 | 1811 | **2103** | +15 % | +16 % |
| DejaVuSans.ttf | **2132** | 1566 | 1983 | 2072 | -3 % | +4 % |
| _cffi_backend.so | **1980** | 1273 | 1707 | 1469 | -26 % | -14 % |
| bootstrap.css | 1926 | 1445 | 1864 | **2494** | +29 % | +34 % |
| commonmark-spec.txt | **1910** | 1342 | 1823 | 1809 | -5 % | -1 % |
| countries.json | 1948 | 1208 | **1976** | 937 | -52 % | -53 % |
| countries.min.json | 1948 | 1510 | 1714 | **2256** | +16 % | +32 % |
| grace_hopper.jpg | 1900 | 1339 | 1786 | **1918** | +1 % | +7 % |
| lodash.js | 1831 | 1369 | 1826 | **2184** | +19 % | +20 % |
| minduka_present.png | 1955 | 1386 | 2012 | **2126** | +9 % | +6 % |
| requests-2.32.3.tar | 1901 | 1628 | **2051** | 1964 | +3 % | -4 % |
| requests-history.md | 1914 | 1453 | 1977 | **2198** | +15 % | +11 % |
| requests-models.py | **2256** | 1623 | 2054 | 1659 | -26 % | -19 % |
| sql-wasm.wasm | **2186** | 1582 | 2111 | 1894 | -13 % | -10 % |

**Bold** marks the fastest codec in that row. The two delta columns are how much faster Base85N is than that codec -- **positive is faster**, negative means Base85N is slower.

**Encoding** is where Base85N pays for what it saves. Per 4-byte group it runs
a Fill scan, a prefix scan that tracks eight donor profiles at once, and — when
a segment is taken — a per-segment substitution table build. Base64 does a
6→8-bit repack with no division at all, and the other two Base85s do one
division chain per group and nothing else.

**Decoding** is close to the field: a block group is the same 5→4 conversion
every Base85 does, a DP segment is one table lookup per character, and a Fill
signal is a `memset`. The one weak row is pretty-printed JSON, where nearly
every group is a short Fill signal and the decoder rebuilds a substitution
table per segment.

The scan-heavy row is a shape built to defeat the mode decision: 18
representable bytes then one that no alphabet can carry, repeated, so a
candidate never reaches MIN_PASSTHROUGH_BYTES and the encoder scans and falls
back on every group. It is in the corpus because it is the worst case for
prefix identification, not because it resembles real input.

### Against specification v0.3.1

Version 0.4.0 changed the wire format: Dynamic Passthrough builds its
substitution per segment from a mask and a donor profile instead of choosing
one of eight fixed replacement alphabets, and Solid Fill is new. Size is the
comparison that matters, and it is machine-independent — the same 13 files
through both implementations of the C library:

| | v0.3.1 | v0.4.0 |
|---|---|---|
| whole corpus | 1.11407 | **1.02435** |
| pretty-printed JSON | 1.00513 | **0.89239** |
| minified JSON | 1.00547 | **1.00260** |
| CommonMark specification | 1.01961 | **0.85889** |
| JavaScript source | 1.05640 | **1.00282** |
| CSS bundle | 1.05066 | **1.00338** |
| Python module | 1.00903 | **0.96166** |
| Markdown changelog | 1.00807 | **0.97905** |
| uncompressed tar | 1.07605 | **0.76266** |
| zero-padded ELF | 1.24640 | **1.12618** |
| WebAssembly module | 1.24737 | **1.24222** |
| TrueType font | 1.24801 | **1.23724** |
| JPEG photograph | 1.24968 | **1.24867** |
| PNG image | 1.25004 | **1.25004** |

For throughput, the comparison is in instructions executed rather than in
MB/s, for the reason `instructions/run.sh` documents: on a shared machine the
run-to-run spread is larger than the difference being measured, while
instruction counts are deterministic. 200 kB per row, C implementation both
sides:

| input | phase | v0.3.1 | v0.4.0 | ratio |
|---|---|---|---|---|
| random | encode | 1,730,370 | 2,478,641 | 1.43 |
| random | decode | 1,850,645 | 2,350,661 | 1.27 |
| text | encode | 4,272,589 | 6,224,362 | 1.46 |
| text | decode | 2,585,373 | 1,804,464 | **0.70** |
| mixed | encode | 4,202,498 | 5,816,644 | 1.38 |
| mixed | decode | 2,322,280 | 2,547,095 | 1.10 |

The encoder does 38–46 % more work per byte, which is the price of the two
scans and the per-segment substitution; the decoder is faster on text, where
0.3.x spent its time on a longer stream. Both numbers are *after* the
lookahead of spec Section 11.1 was restored: without it the v0.4.0 encoder
costs 5,610,181 instructions on random input — 3.24× v0.3.1 — because the two
scans are re-entered every four bytes over binary that can use neither.

---

## Where the alternatives are the better choice

The benchmark is equally explicit about this:

- **Ascii85 on sparse binaries.** Its zero-run shorthand encodes the
  zero-padded ELF at 1.026 against Base85N's 1.126 — the one row in the corpus
  where another codec is smaller. Ascii85 spends one character per four zero
  bytes with no threshold and no signal; a Fill signal costs five characters
  and only fires from five bytes up, but then carries up to 2048, which is why
  Base85N takes the tar sample (0.763 against 1.015) and loses this one.
- **Speed.** See above: Base85N is the slowest encoder of the four on 14 of the
  16 inputs.
- **Z85 for addressable data.** Its fixed 4→5 mapping means a byte offset
  converts to a character offset by arithmetic, so random access, seeking and
  parallel chunked processing are trivial. Base85N's output length is
  data-dependent, so none of that is possible.
- **Maturity.** Ascii85 is in PDF and PostScript, Z85 is a ZeroMQ standard,
  RFC 1924 Base85 ships in Python's standard library. Base85N is a 0.x draft
  whose defects are still being found — the benchmark itself surfaced one this
  version, in the shape of an encoder that had quietly become three times
  slower on binary.

---

## What benchmarking changed

Each of these was found by measuring, not by reading:

**Fill's threshold inside a segment.** Spec Section 6.5 says a run of
`MIN_FILL_BYTES` is a Fill segment. Applied literally inside passthrough text
that is a loss: breaking a DP segment for a run costs the Fill signal *and* the
signal that resumes passthrough, so a run of 5 spends 10 characters to save 5.
Measuring the whole corpus at both thresholds put the break-even at 11 and the
cost of getting it wrong at 0.86 % overall — and on JavaScript source, at worse
than having no in-segment Fill at all (1.0795 against 1.0077).

**The encoder's lookahead.** Version 0.3.x skipped over stretches where no DP
candidate could start. Solid Fill made that skip unsound — a run can begin
anywhere in such a stretch — and dropping it cost 3.24× the instructions on
binary input. The replacement tests both conditions at the only positions the
loop actually visits, which is exact rather than heuristic and recovers most of
it.

**The decoder's output buffer.** Fill is the first construct whose output is
not bounded by its input, so the decoder has to be able to grow its buffer. The
obvious version — pass the buffer by pointer and check on every group — cost
27 % on binary decode, because the pointer could no longer stay in a register.
Keeping a local copy and refreshing it only where the buffer actually moves
brought that back.
