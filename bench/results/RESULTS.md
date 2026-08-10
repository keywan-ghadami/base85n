# Base85N benchmark results

Base85N measured against its actual competition — Ascii85 (Adobe/btoa),
Z85 (ZeroMQ RFC 32) and RFC 1924 Base85 — with Base64 as the baseline
everything is replacing. Encoded size and encode/decode throughput.
Methodology, corpus provenance and how to reproduce: [../README.md](../README.md).

Numbers produced 2026-08-10 on an Intel Xeon @ 2.80 GHz (4 vCPU),
Ubuntu 24.04, gcc 13.3.0 `-O2`, CPython 3.11.15.

---

## Summary

**Against the other Base85 variants, on text-shaped data, Base85N wins
clearly.** Pretty-printed JSON encodes at 1.033 characters per byte where
every other Base85 is stuck at 1.250 — 17 % smaller output than its
nearest rival, and the output stays readable. Across the whole 4.94 MB
corpus it is 8.0 % smaller than RFC 1924 Base85 and 4.1 % smaller than
Ascii85.

**Its real structural advantage is the alphabet, and it is bigger than
the raw ratios suggest.** Base85N is the only Base85 here whose output
can be dropped into a JSON string or XML text unescaped. Once you charge
the others for the escaping their alphabets force, the ranking changes:
in XML, both Ascii85 (1.417) and RFC 1924 Base85 (1.353) end up *larger
than Base64*, while Base85N stays at 1.150.

**On high-entropy binary it is a tie, and on one binary shape it loses.**
All four Base85s land at 1.246–1.250 on WASM, ELF, TrueType, JPEG and
PNG. The exception is Ascii85's zero-run shorthand, which encodes the
zero-padded ELF sample at 1.026 against Base85N's 1.246 — Ascii85 is 18 %
smaller there, and no amount of tuning closes that gap without adding an
equivalent feature.

**On speed, Base85N is still last, but no longer by the margin the first
run reported.** That run measured 62–99 MB/s encoding; the cause turned out
to be a missing optimisation rather than the algorithm, and after fixing it
the C encoder runs at **97–150 MB/s** on binary input and decodes at
**281–294 MB/s**. Against Ascii85 and Z85 at ~400 MB/s encode that is
roughly 3–4× slower, not 6×. See
[What the speed actually cost](#what-the-speed-actually-cost).

**The quadratic encoder found by the previous benchmark run is fixed**, in
all five implementations, with byte-identical output. The CommonMark
specification went from 0.22 MB/s to 25.3 MB/s (115×) and the pathological
case from 0.05 MB/s to 23.5 MB/s (470×). Details in
[The quadratic encoder, and its fix](#the-quadratic-encoder-and-its-fix).

---

## Base85N against the other Base85 variants

This is the comparison that matters: Base64 is the incumbent, but anyone
choosing Base85N is choosing it over Ascii85, Z85 or RFC 1924.

### Where Base85N wins

**1. Text-shaped payloads, by a wide margin.** Dynamic Passthrough is the
only feature in this field that adapts to the input at all; the other
three apply a fixed 5:4 expansion to everything.

| sample | Ascii85 | Z85 | Base85 (RFC 1924) | **Base85N** | Base85N vs best other Base85 |
|---|---|---|---|---|---|
| countries.json (pretty) | 1.250 | n/a | 1.250 | **1.033** | **17.4 % smaller** |
| countries.min.json | 1.250 | n/a | 1.250 | **1.053** | **15.8 % smaller** |
| commonmark-spec.txt | 1.250 | n/a | 1.250 | **1.123** | **10.2 % smaller** |

**2. The alphabet, once you charge for escaping.** Base85N's 85
characters deliberately exclude `"` `'` `\` `` ` `` `<` `>` `&`. The others
do not, and encoded payloads overwhelmingly travel inside JSON or XML. The
percentages are how much **larger** each codec's output is than Base85N's,
over the whole corpus:

| codec | raw | vs Base85N | inside JSON | vs Base85N | inside XML | vs Base85N |
|---|---|---|---|---|---|---|
| Base64 | 1.3333 | +15.9 % | 1.3333 | +15.9 % | 1.3333 | +15.9 % |
| Ascii85 | 1.1996 | +4.3 % | 1.2283 | +6.8 % | 1.4171 | **+23.2 %** |
| Z85 | 1.2500 | +8.7 % | 1.2500 | +8.7 % | 1.3662 | **+18.8 %** |
| Base85 (RFC 1924) | 1.2500 | +8.7 % | 1.2500 | +8.7 % | 1.3530 | **+17.6 %** |
| **Base85N** | **1.1503** | — | **1.1503** | — | **1.1503** | — |

Read the three "vs Base85N" columns left to right: Base85N's lead over the
other Base85 variants grows from 4–9 % raw, to 7–9 % in JSON, to **18–23 %
in XML** — because their alphabets have to be escaped there and Base85N's
does not. All three also cross from cheaper than Base64 to *more expensive
than Base64* in XML.

**3. Arbitrary input lengths, no padding.** Z85 is defined only for inputs
whose length is a multiple of 4 — it has no partial-group form at all. It is
measured here the way an application would have to use it: **zero-padded up
to the next multiple of 4**, with that padding charged to its size. On short
fields the cost is visible — an E.164 phone number (13 B) takes 20
characters under Z85 against 17 under Ascii85 and Base85N.

The padding also has to be undone, and Z85 cannot help with that: padding
zeros are indistinguishable from trailing zeros belonging to the data, so
**the original length must be carried outside the encoding** — a length
field, a framing layer, or a self-delimiting payload. That extra channel is
*not* counted in any figure here, so these numbers flatter Z85 slightly.
Base85N and Ascii85 encode any length directly; Base64 pads but signals the
padding in-band with `=`.

**4. Readable output.** Not a size property, but the reason the encoding
exists:

```
input    {"id":184223,"name":"Ada Lovelace","phone":"+493023125190",...}
Base64   eyJpZCI6MTg0MjIzLCJuYW1lIjoiQWRhIExvdmVsYWNlIiwicGhvbmUiOiIrNDk...
Ascii85  HQm07,!%G<1bpgB/0\t?D.OnP,!faJ+A?]sASbdbAKiZLE+j0-AKj/Z.l9...
Base85N  %nS{A{+id+~:184223^+name+~:+Ada:Lovelace+^+phone+~:+~+4930231...
```

### Where the other Base85 variants win

**1. Ascii85 beats everything on sparse binaries.** Its `z` shorthand
collapses an all-zero 4-byte group to one character. On the zero-padded
ELF sample that is decisive:

| sample | Ascii85 | Base85N | winner |
|---|---|---|---|
| _cffi_backend.so (ELF, zero-padded) | **1.026** | 1.246 | **Ascii85, 17.7 % smaller** |

Base85N has no zero-run shorthand. If your payloads are sparse binaries —
zero-padded executables, sparse buffers, page-aligned images — Ascii85 is
simply the better choice and this benchmark says so.

**2. All three are faster.** Encoding, C, same harness, binary input:

| codec | encode MB/s | decode MB/s |
|---|---|---|
| Ascii85 | ~400 | ~580 |
| Z85 | ~410 | ~960 |
| **Base85N** | **97–150** | **281–294** |

Base85N pays for a per-group mode decision that the others do not have to
make: roughly 3–4× on encode, and 2–3.5× on decode against Z85. On text the
encode gap is similar (62–112 MB/s) while the output is 10–17 % smaller. If
you are CPU-bound rather than bandwidth-bound, the other Base85s win.

**3. Z85's fixed alignment enables things Base85N structurally cannot.**
Because Z85 maps exactly 4 bytes to exactly 5 characters with no modes and
no signals, a byte offset converts to a character offset by arithmetic.
That gives you random access into encoded data, seeking, and trivially
parallel chunked encoding and decoding. Base85N's output length is
data-dependent, so none of that is possible: you decode from the start,
single-threaded. For large encoded blobs at rest, this is a real
architectural advantage, not a micro-optimisation.

**4. Bounded streaming state.** Ascii85, Z85 and RFC 1924 encode with O(1)
state and 4 bytes of lookahead. Base85N's Pass 1 must scan to the end of a
representable run before it can fix the mask, so a streaming encoder needs
either unbounded buffering or an implementation-chosen lookahead cap
(permitted by spec Section 6.1, but it changes where segments break). For
an encoder sitting in a streaming pipeline, the fixed-ratio codecs are
easier to bound.

**5. Base85N's output length leaks input structure.** The fixed-ratio
codecs reveal only the input's length. Base85N's DP-vs-block decision is
data-dependent, so the output length and shape reveal how text-like the
plaintext was. That is called out in spec Section 13 and it matters if
encoded field lengths are observable to someone who should not learn about
the content.

**6. Maturity and ecosystem.** Ascii85 is embedded in PDF and PostScript.
Z85 is a ZeroMQ standard. RFC 1924 Base85 ships in the Python standard
library (`base64.b85encode`). Base85N is a 0.x draft with an unfrozen wire
format, five implementations that were written with heavy AI assistance
and have not been independently audited, and — as the previous run of this
very benchmark demonstrated — defects still being found. The other three
have had a long time to have their bugs found by other people.

**7. Simplicity is a security property.** A Z85 encoder is about fifty
lines with no branches that depend on content. Base85N's encoder has a
mode decision, an escape mechanism, a segmentation rule and a
complexity constraint (spec Section 6.6) that all five implementations
initially got wrong. More moving parts, more places to be wrong.

### Choosing between them

| if your data is… | pick |
|---|---|
| text-shaped, and lands in JSON/XML/logs | **Base85N** |
| sparse binary with long zero runs | **Ascii85** |
| fixed-size records needing random access | **Z85** |
| high-entropy binary, CPU-bound | **Ascii85 or Z85** (6× faster, same size) |
| anything, and you need it to just work everywhere today | **Base64** |

---

## Size

### Corpus files — expansion ratio (encoded chars per input byte)

| sample | input | Base64 | Ascii85 | Z85 | Base85 (RFC 1924) | Base85N | vs Base64 | vs best other Base85 |
|---|---|---|---|---|---|---|---|---|
| sql-wasm.wasm | 659,730 B | 1.333 | 1.247 | 1.250 | 1.250 | 1.246 | +6.5 % | +0.1 % |
| _cffi_backend.so | 1,068,624 B | 1.333 | 1.026 | 1.250 | 1.250 | 1.246 | +6.5 % | -21.5 % |
| DejaVuSans.ttf | 756,072 B | 1.333 | 1.240 | 1.250 | 1.250 | 1.248 | +6.4 % | -0.7 % |
| countries.json | 1,408,911 B | 1.333 | 1.250 | 1.250 | 1.250 | 1.033 | +22.6 % | +17.4 % |
| countries.min.json | 772,294 B | 1.333 | 1.250 | 1.250 | 1.250 | 1.053 | +21.0 % | +15.8 % |
| commonmark-spec.txt | 202,827 B | 1.333 | 1.250 | 1.250 | 1.250 | 1.123 | +15.8 % | +10.2 % |
| grace_hopper.jpg | 61,306 B | 1.333 | 1.250 | 1.250 | 1.250 | 1.250 | +6.3 % | +0.0 % |
| minduka_present.png | 13,634 B | 1.333 | 1.250 | 1.250 | 1.250 | 1.250 | +6.3 % | +0.0 % |

Two rows repay a second look. **Ascii85 wins the ELF sample outright** via
its zero-run shorthand, as discussed above. And the **two JSON rows are the
same data**, once pretty-printed and once minified: Base85N encodes the
pretty-printed form *more* efficiently (1.033 vs 1.053), because
indentation is runs of spaces that passthrough carries almost free, while
for every other codec the extra whitespace is simply 33 % more bytes to
expand.

### Corpus totals

| codec | total encoded | ratio | vs Base64 | vs Base85N |
|---|---|---|---|---|
| Base64 | 6,591,204 chars | 1.3333 | — | 15.9 % larger |
| Ascii85 | 5,930,050 chars | 1.1996 | +10.03 % | 4.3 % larger |
| Z85 | 6,179,260 chars | 1.2500 | +6.25 % | 8.7 % larger |
| Base85 (RFC 1924) | 6,179,250 chars | 1.2500 | +6.25 % | 8.7 % larger |
| **Base85N** | **5,686,506 chars** | **1.1503** | **+13.73 %** | — |

Total input: 4,943,398 bytes across 8 files.

### Short protocol fields

The payloads that dominate most real traffic. Figures are encoded
**characters**, not ratios. "vs best other Base85" is against whichever of
Ascii85, Z85 and RFC 1924 Base85 does best on that row.

| field | input | Base64 | Ascii85 | Z85 | Base85 (RFC 1924) | Base85N | vs Base64 | vs best other Base85 |
|---|---|---|---|---|---|---|---|---|
| first + last name | 12 B | 16 | 15 | 15 | 15 | 15 | +6.2 % | +0.0 % |
| name, umlauts | 25 B | 36 | 32 | 35 | 32 | 32 | +11.1 % | +0.0 % |
| customer number | 4 B | 8 | 5 | 5 | 5 | 5 | +37.5 % | +0.0 % |
| order number | 19 B | 28 | 24 | 25 | 24 | 24 | +14.3 % | +0.0 % |
| hex value (8 byte) | 16 B | 24 | 20 | 20 | 20 | 20 | +16.7 % | +0.0 % |
| hex digest (SHA-256) | 64 B | 88 | 80 | 80 | 80 | 69 | +21.6 % | +13.8 % |
| phone number, E.164 | 13 B | 20 | 17 | 20 | 17 | 17 | +15.0 % | +0.0 % |
| phone number, formatted | 17 B | 24 | 22 | 25 | 22 | 22 | +8.3 % | +0.0 % |
| email address | 24 B | 32 | 30 | 30 | 30 | 29 | +9.4 % | +3.3 % |
| URL | 53 B | 72 | 67 | 70 | 67 | 58 | +19.4 % | +13.4 % |
| UUID v4 | 36 B | 48 | 45 | 45 | 45 | 41 | +14.6 % | +8.9 % |
| ISO 8601 timestamp | 24 B | 32 | 30 | 30 | 30 | 29 | +9.4 % | +3.3 % |
| IPv4 address | 11 B | 16 | 14 | 15 | 14 | 14 | +12.5 % | +0.0 % |
| IPv6 address | 28 B | 40 | 35 | 35 | 35 | 33 | +17.5 % | +5.7 % |
| MAC address | 17 B | 24 | 22 | 25 | 22 | 22 | +8.3 % | +0.0 % |
| IBAN | 22 B | 32 | 28 | 30 | 28 | 27 | +15.6 % | +3.6 % |
| currency amount | 11 B | 16 | 14 | 15 | 14 | 14 | +12.5 % | +0.0 % |
| CSV row | 64 B | 88 | 80 | 80 | 80 | 69 | +21.6 % | +13.8 % |
| JSON record | 92 B | 124 | 115 | 115 | 115 | 103 | +16.9 % | +10.4 % |
| HTTP header block | 114 B | 152 | 143 | 145 | 143 | 122 | +19.7 % | +14.7 % |
| JWT (3 segments) | 155 B | 208 | 194 | 195 | 194 | 160 | +23.1 % | +17.5 % |
| log line | 93 B | 124 | 117 | 120 | 117 | 100 | +19.4 % | +14.5 % |
| SQL statement | 118 B | 160 | 148 | 150 | 148 | 125 | +21.9 % | +15.5 % |

Below the 20-byte Dynamic Passthrough minimum, Base85N is
character-for-character identical to the best other Base85 — a name, a
customer number or an IPv4 address gets no passthrough benefit, and the
"vs best other Base85" column reads +0.0 %. The gain appears from roughly
24 bytes upward and grows with how text-like the field is: **+17.5 % on a
JWT, +15.5 % on a SQL statement, +14.5 % on a log line, +13.8 % on a
SHA-256 digest**. Z85's column is consistently the worst of the four on
short fields, because padding to a multiple of 4 costs proportionally most
when there is little to encode.

The 4-byte customer number is the one row where every Base85 looks
spectacular against Base64 (+37.5 %); that is Base64's padding, not a
Base85 virtue.

---

## Throughput

**Every codec in this section is C.** Base64, Ascii85 and Z85 are scalar
reference implementations in `bench/speed/bench_speed.c`; Base85N is this
repository's `c/` implementation, compiled from the same sources with the
same flags into the same binary. Nothing here is measured against Python —
the Python implementations appear only in the size benchmark, where the
output is identical in every language and speed is irrelevant.

Every codec allocates its output with `malloc()` on every call, every
measurement is round-trip verified, and MB/s counts **original (decoded)
bytes** so encode and decode columns are comparable. Best of 3 rounds.

A tuned SIMD Base64 is several times faster than the ~1200 MB/s shown here;
the point of the Base64 column is a like-for-like scalar reference, not a
speed record.

| input | codec | encode MB/s | decode MB/s | ratio |
|---|---|---|---|---|
| synthetic random 1 MiB | Base64 | 1215.77 | 1145.46 | 1.333 |
| synthetic random 1 MiB | Ascii85 | 396.19 | 578.57 | – |
| synthetic random 1 MiB | Z85 | 402.49 | 959.11 | – |
| synthetic random 1 MiB | **Base85N** | **100.50** | **292.12** | **1.250** |
| synthetic text 1 MiB | Base64 | 1226.35 | 1153.90 | 1.333 |
| synthetic text 1 MiB | Ascii85 | 394.74 | 570.04 | – |
| synthetic text 1 MiB | Z85 | 402.68 | 946.19 | – |
| synthetic text 1 MiB | **Base85N** | **98.48** | **163.41** | **1.010** |
| escape-heavy 16 KiB | Base64 | 1254.09 | 1205.74 | 1.333 |
| escape-heavy 16 KiB | Ascii85 | 402.90 | 592.12 | – |
| escape-heavy 16 KiB | Z85 | 416.25 | 978.15 | – |
| escape-heavy 16 KiB | **Base85N** | **68.00** | **290.24** | **1.250** |
| DejaVuSans.ttf | Base64 | 1159.25 | 1189.97 | 1.333 |
| DejaVuSans.ttf | Ascii85 | 404.77 | 593.66 | – |
| DejaVuSans.ttf | Z85 | 414.94 | 984.03 | – |
| DejaVuSans.ttf | **Base85N** | **109.07** | **287.01** | **1.248** |
| _cffi_backend.so | Base64 | 1211.84 | 1183.74 | 1.333 |
| _cffi_backend.so | Ascii85 | 474.86 | 622.16 | – |
| _cffi_backend.so | Z85 | 413.72 | 929.12 | – |
| _cffi_backend.so | **Base85N** | **149.73** | **281.53** | **1.246** |
| commonmark-spec.txt | Base64 | 1217.19 | 1174.99 | 1.333 |
| commonmark-spec.txt | Ascii85 | 400.15 | 586.08 | – |
| commonmark-spec.txt | Z85 | 412.10 | 990.95 | – |
| commonmark-spec.txt | **Base85N** | **62.46** | **152.46** | **1.123** |
| countries.json | Base64 | 1219.12 | 1195.64 | 1.333 |
| countries.json | Ascii85 | 397.33 | 566.21 | – |
| countries.json | Z85 | 386.78 | 982.87 | – |
| countries.json | **Base85N** | **112.39** | **160.89** | **1.033** |
| countries.min.json | Base64 | 1244.44 | 1182.49 | 1.333 |
| countries.min.json | Ascii85 | 393.92 | 574.78 | – |
| countries.min.json | Z85 | 417.55 | 982.91 | – |
| countries.min.json | **Base85N** | **110.02** | **176.57** | **1.053** |
| grace_hopper.jpg | Base64 | 1253.02 | 1219.57 | 1.333 |
| grace_hopper.jpg | Ascii85 | 401.61 | 583.49 | – |
| grace_hopper.jpg | Z85 | 409.55 | 929.78 | – |
| grace_hopper.jpg | **Base85N** | **96.96** | **294.23** | **1.250** |
| minduka_present.png | Base64 | 1235.29 | 1187.60 | 1.333 |
| minduka_present.png | Ascii85 | 400.48 | 564.03 | – |
| minduka_present.png | Z85 | 407.18 | 981.12 | – |
| minduka_present.png | **Base85N** | **109.67** | **291.71** | **1.250** |
| sql-wasm.wasm | Base64 | 1223.08 | 1190.76 | 1.333 |
| sql-wasm.wasm | Ascii85 | 401.25 | 582.36 | – |
| sql-wasm.wasm | Z85 | 410.98 | 977.18 | – |
| sql-wasm.wasm | **Base85N** | **97.03** | **280.75** | **1.246** |

The `ratio` column is Base85N's; the other codecs' ratios are in the size
tables above. Z85 rows for inputs whose length is not a multiple of 4 are
measured over the largest 4-byte-aligned prefix.

Reading it:

- **Binary encode: ~3–4× slower than the other Base85s**, ~11× slower than
  scalar Base64. Base85N runs the passthrough analysis even when the answer
  is always "use block mode".
- **Binary decode: ~3× slower than Z85**, roughly half Ascii85's speed.
- **Text is the slowest case to encode** (62–112 MB/s) even though it
  produces far less output — the mode decision is the cost, and it is paid
  precisely where passthrough succeeds.
- **`escape-heavy` at 68 MB/s** is the worst case by construction: Pass 2
  aborts after three bytes on every iteration, so the encoder does the most
  analysis per byte consumed.

---

## What the speed actually cost

The first version of this benchmark reported 34–99 MB/s encoding and
concluded that this was what Base85N's mode decision costs. That was wrong,
and worth writing down because the conclusion was plausible and the number
was real.

**The measurement was fair.** All four codecs were C, in the same harness,
with the same allocation discipline — there was no Python-versus-C mixup.

**The implementation was not.** Two of the hottest operations in the
encoder were linear searches. Every input byte is tested for R-Set
membership, and inside the encoding loop that happens twice per byte — once
while scanning a run, once while retiring consumed bytes from the run's
counts. Both went through:

```c
static int rset_index_for_byte(uint8_t b) {
    for (int j = 0; j < RSET_COUNT; j++)   /* 13 comparisons, per byte */
        if (RSET_ASCII[j] == b) return j;
    return -1;
}
```

with the same pattern for the replacement characters, plus a lazily
initialised alphabet table that re-checked its "is it built yet" flag on
every lookup. Replacing all three with byte-indexed `const` tables — the
representation every other codec in the comparison already used — changed
nothing about the output and produced:

| input | encode before | encode after | change | decode before | decode after |
|---|---|---|---|---|---|
| synthetic random 1 MiB | 67.33 | **100.50** | +49 % | 197.67 | **292.12** |
| synthetic text 1 MiB | 34.69 | **98.48** | +184 % | 69.67 | **163.41** |
| escape-heavy 16 KiB | 23.53 | **68.00** | +189 % | 197.78 | **290.24** |
| _cffi_backend.so | 99.43 | **149.73** | +51 % | 187.68 | **281.53** |
| countries.min.json | 33.43 | **110.02** | +229 % | 65.86 | **176.57** |
| commonmark-spec.txt | 25.28 | **62.46** | +147 % | 78.64 | **152.46** |

All figures MB/s, same machine, same harness, same session.

The same pattern was present in the other implementations and is fixed
there too: Rust used a linear scan *and* a `thread_local` table (a TLS
access per byte), Go used hash maps, TypeScript used `Map`s plus a
`String.fromCharCode` allocation per byte for the alphabet check. All five
now use byte-indexed tables. Output is unchanged — verified across
implementations on 8,153 native and 892 TypeScript inputs plus the shared
vectors.

**What remains is genuine.** The residual 3–4× gap on binary is the mode
decision itself: per 4-byte group Base85N scans for a representable run,
derives a mask, evaluates a passthrough candidate and compares it against
block mode, where Ascii85 and Z85 just divide. One further idea was tried
and rejected: pre-reserving the output buffer made no measurable
difference (100.19 vs 100.31 MB/s, inside noise) and was not kept. Batching
consecutive block-mode groups could plausibly help binary input further,
but proving it produces identical output in every case is a larger change
than a benchmark should make on its own.

---

## The quadratic encoder, and its fix

The previous run of this benchmark found the encoder to be quadratic in
the length of an escape-heavy run, in every implementation, reachable from
ordinary content. It is now fixed.

### What was wrong

`encode()` loops. Pass 1 scanned forward to the end of the maximal
*representable* run, because the R-Set mask it computes depends on the
whole run. Pass 2 then stopped at the 4th consecutive byte needing an
escape — after 3 bytes on escape-dense input — and the loop block-encoded
as few as 4 bytes and started over, re-running Pass 1 from scratch. O(N)
work to advance 4 bytes: **O(N²)**.

The trigger in real content was code spans. `commonmark-spec.txt` contains
3,549 `>` characters; a `>` anywhere in a representable run sets the mask
bit whose replacement character is `` ` ``, which makes all 43,278 literal
backticks in the file escaped bytes. 1,303 of those are runs of four or
more, and each one aborted Pass 2 immediately after Pass 1 had scanned the
rest of the run. Pass 1 scanned 411 bytes per input byte on that file,
against 1.0 on JSON.

### The fix

Pass 1's result for a position inside a run is a *suffix* of its result
for any earlier position in the same run. So each run is now scanned once,
keeping an occurrence count for each of the 13 R-Set characters plus the
mask those counts imply; consuming k bytes decrements k counts and clears
a mask bit when its last occurrence goes. When a consumption steps past
the end of a run — which the final block-mode branch can do, since it
ignores representability — the next run is scanned fresh. Runs treated
this way are disjoint, so total scanning stays O(N), in O(1) extra memory.

This is an implementation change only: **no wire-format change, and
byte-identical output.** It is now required by the specification —
[Section 6.6, Encoding Complexity](../../spec/base85n-v0.2.0.md#66-encoding-complexity),
new in spec v0.2.0 — with a matching normative bullet in Section 13, since
an encoder is routinely handed text the encoding system did not author.

### Verification

- All five implementations produce **identical output on 8,055 inputs** —
  exhaustive over short strings from an escape-dense alphabet, thousands of
  random binary/ASCII/escape-dense buffers, the pathological cases, and
  every corpus file.
- The Python encoder was additionally diffed against its pre-fix self on
  the same corpus: identical everywhere.
- The 54 golden and 18 adversarial shared vectors still pass in all five
  languages, and the C harness is clean under ASan/UBSan.
- Each language gained a regression test asserting both a wall-clock
  ceiling and sub-quadratic growth on `~` * N.

### Effect

| case | before | after | speedup |
|---|---|---|---|
| `commonmark-spec.txt` encode (C) | 0.22 MB/s | **25.28 MB/s** | **115×** |
| escape-heavy 16 KiB encode (C) | 0.05 MB/s | **23.53 MB/s** | **470×** |
| `~` × 100,000 encode (C) | 14.3 s | **0.005 s** | **~3000×** |
| `~` × 32,000 encode (Python) | 15.0 s | **0.05 s** | **~300×** |
| random 1 MiB encode (C) | 58.57 MB/s | **67.33 MB/s** | 1.15× |
| `_cffi_backend.so` encode (C) | 84.40 MB/s | **99.43 MB/s** | 1.18× |

Growth on doubling, `~` * N, is now ~2.0 in every implementation where it
was ~4.0. Ordinary binary input got 15–18 % faster as well, because the
old code re-derived the mask on every iteration and the new code does not.
