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

**On speed, Base85N is last, by a lot.** Its C encoder runs at
62–99 MB/s on binary input against ~400 MB/s for Ascii85 and Z85 and
~1200 MB/s for Base64 — 6× slower than the other Base85s, 18× slower than
Base64. Decoding is 168–198 MB/s against Z85's ~980 MB/s.

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
characters deliberately exclude `"` `'` `\` `` ` `` `<` `>` `&`. The
others do not, and encoded payloads overwhelmingly travel inside JSON or
XML. Measured over the whole corpus:

| codec | raw | inside a JSON string | inside XML text |
|---|---|---|---|
| Base64 | 1.3333 | 1.3333 | 1.3333 |
| Ascii85 | 1.1996 | 1.2283 | **1.4171** |
| Z85 | n/a | n/a | n/a |
| Base85 (RFC 1924) | 1.2500 | 1.2500 | **1.3530** |
| **Base85N** | **1.1503** | **1.1503** | **1.1503** |

Ascii85's 10 % advantage over Base64 shrinks to 8 % in JSON and inverts
into a 6 % *penalty* in XML. RFC 1924's 6 % advantage inverts into a 1.5 %
penalty. Base85N's ratio does not move, because there is nothing in its
output to escape. (Z85 is `n/a` because six of the eight samples have a
length that is not a multiple of 4, which Z85 does not define.)

**3. Arbitrary input lengths, no padding.** Z85 only accepts lengths that
are a multiple of 4 — in this corpus that disqualifies it for six of eight
files and for 13 of 23 protocol fields, which is why its column is mostly
`n/a`. Base85N and Ascii85 handle any length; Base64 pads.

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

**2. All three are far faster.** Encoding, C, same harness, binary input:

| codec | encode MB/s | decode MB/s |
|---|---|---|
| Ascii85 | ~395 | ~580 |
| Z85 | ~410 | ~980 |
| **Base85N** | **62–99** | **168–198** |

Base85N pays for a per-window mode decision that the others do not have
to make. On text the gap widens further (25–48 MB/s encode), because that
is exactly where the passthrough analysis does the most work. If you are
CPU-bound rather than bandwidth-bound, the other Base85s win outright.

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

| sample | input | Base64 | Ascii85 | Z85 | Base85 (RFC 1924) | Base85N | Base85N vs Base64 |
|---|---|---|---|---|---|---|---|
| sql-wasm.wasm | 659,730 B | 1.333 | 1.247 | n/a | 1.250 | 1.246 | +6.5 % |
| _cffi_backend.so | 1,068,624 B | 1.333 | **1.026** | 1.250 | 1.250 | 1.246 | +6.5 % |
| DejaVuSans.ttf | 756,072 B | 1.333 | 1.240 | 1.250 | 1.250 | 1.248 | +6.4 % |
| countries.json | 1,408,911 B | 1.333 | 1.250 | n/a | 1.250 | **1.033** | +22.6 % |
| countries.min.json | 772,294 B | 1.333 | 1.250 | n/a | 1.250 | **1.053** | +21.0 % |
| commonmark-spec.txt | 202,827 B | 1.333 | 1.250 | n/a | 1.250 | **1.123** | +15.8 % |
| grace_hopper.jpg | 61,306 B | 1.333 | 1.250 | n/a | 1.250 | 1.250 | +6.3 % |
| minduka_present.png | 13,634 B | 1.333 | 1.250 | n/a | 1.250 | 1.250 | +6.3 % |

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
| Z85 | not applicable to 6 of 8 samples | – | – | – |
| Base85 (RFC 1924) | 6,179,250 chars | 1.2500 | +6.25 % | 8.7 % larger |
| **Base85N** | **5,686,506 chars** | **1.1503** | **+13.73 %** | — |

Total input: 4,943,398 bytes across 8 files.

### Short protocol fields

The payloads that dominate most real traffic. Figures are encoded
**characters**, not ratios.

| field | input | Base64 | Ascii85 | Z85 | Base85 (RFC 1924) | Base85N | Base85N vs Base64 |
|---|---|---|---|---|---|---|---|
| first + last name | 12 B | 16 | 15 | 15 | 15 | 15 | +6.2 % |
| name, umlauts | 25 B | 36 | 32 | n/a | 32 | 32 | +11.1 % |
| customer number | 4 B | 8 | 5 | 5 | 5 | 5 | +37.5 % |
| order number | 19 B | 28 | 24 | n/a | 24 | 24 | +14.3 % |
| hex value (8 byte) | 16 B | 24 | 20 | 20 | 20 | 20 | +16.7 % |
| hex digest (SHA-256) | 64 B | 88 | 80 | 80 | 80 | 69 | +21.6 % |
| phone number, E.164 | 13 B | 20 | 17 | n/a | 17 | 17 | +15.0 % |
| phone number, formatted | 17 B | 24 | 22 | n/a | 22 | 22 | +8.3 % |
| email address | 24 B | 32 | 30 | 30 | 30 | 29 | +9.4 % |
| URL | 53 B | 72 | 67 | n/a | 67 | 58 | +19.4 % |
| UUID v4 | 36 B | 48 | 45 | 45 | 45 | 41 | +14.6 % |
| ISO 8601 timestamp | 24 B | 32 | 30 | 30 | 30 | 29 | +9.4 % |
| IPv4 address | 11 B | 16 | 14 | n/a | 14 | 14 | +12.5 % |
| IPv6 address | 28 B | 40 | 35 | 35 | 35 | 33 | +17.5 % |
| MAC address | 17 B | 24 | 22 | n/a | 22 | 22 | +8.3 % |
| IBAN | 22 B | 32 | 28 | n/a | 28 | 27 | +15.6 % |
| currency amount | 11 B | 16 | 14 | n/a | 14 | 14 | +12.5 % |
| CSV row | 64 B | 88 | 80 | 80 | 80 | 69 | +21.6 % |
| JSON record | 92 B | 124 | 115 | 115 | 115 | 103 | +16.9 % |
| HTTP header block | 114 B | 152 | 143 | n/a | 143 | 122 | +19.7 % |
| JWT (3 segments) | 155 B | 208 | 194 | n/a | 194 | 160 | +23.1 % |
| log line | 93 B | 124 | 117 | n/a | 117 | 100 | +19.4 % |
| SQL statement | 118 B | 160 | 148 | n/a | 148 | 125 | +21.9 % |

Below the 20-byte Dynamic Passthrough minimum, Base85N is
character-for-character identical to the other Base85 variants: a name, a
phone number or an IPv4 address gets no passthrough benefit. The gain
appears from roughly 24 bytes upward and grows with how text-like the field
is — 18 % smaller than the other Base85s on a JWT, 16 % on a SQL statement,
14 % on a CSV row.

The 4-byte customer number is the one row where every Base85 looks
spectacular against Base64 (+37.5 %); that is Base64's padding, not a
Base85 virtue.

---

## Throughput

C implementations, scalar, same harness, same flags, every codec
allocating its output with `malloc()` on every call, every measurement
round-trip verified. MB/s counts **original (decoded) bytes**, so encode
and decode columns are directly comparable. Best of 3 rounds.

The Base64/Ascii85/Z85 implementations here are straightforward
table-driven scalar code, written at the same level of effort as the
Base85N implementation they are compared against. A tuned SIMD Base64 is
several times faster than the ~1200 MB/s shown here.

| input | codec | encode MB/s | decode MB/s | ratio |
|---|---|---|---|---|
| synthetic random 1 MiB | Base64 | 1207.40 | 1176.31 | 1.333 |
| synthetic random 1 MiB | Ascii85 | 392.72 | 579.96 | 1.250 |
| synthetic random 1 MiB | Z85 | 411.57 | 981.68 | 1.250 |
| synthetic random 1 MiB | **Base85N** | **67.33** | **197.67** | 1.250 |
| synthetic text 1 MiB | Base64 | 1231.97 | 1200.71 | 1.333 |
| synthetic text 1 MiB | Ascii85 | 402.58 | 581.16 | 1.250 |
| synthetic text 1 MiB | Z85 | 410.76 | 979.82 | 1.250 |
| synthetic text 1 MiB | **Base85N** | **34.69** | **69.67** | **1.010** |
| escape-heavy 16 KiB | Base64 | 1240.24 | 1185.47 | 1.333 |
| escape-heavy 16 KiB | Ascii85 | 401.95 | 589.85 | 1.250 |
| escape-heavy 16 KiB | Z85 | 417.15 | 986.01 | 1.250 |
| escape-heavy 16 KiB | **Base85N** | **23.53** | **197.78** | 1.250 |
| DejaVuSans.ttf | Base64 | 1222.81 | 1201.72 | 1.333 |
| DejaVuSans.ttf | Ascii85 | 395.55 | 577.02 | 1.240 |
| DejaVuSans.ttf | Z85 | 407.46 | 961.32 | 1.250 |
| DejaVuSans.ttf | **Base85N** | **68.43** | **192.58** | 1.248 |
| _cffi_backend.so | Base64 | 1233.62 | 1196.13 | 1.333 |
| _cffi_backend.so | Ascii85 | 480.33 | 625.47 | 1.026 |
| _cffi_backend.so | Z85 | 413.32 | 967.53 | 1.250 |
| _cffi_backend.so | **Base85N** | **99.43** | **187.68** | 1.246 |
| commonmark-spec.txt | Base64 | 1261.67 | 1219.30 | 1.333 |
| commonmark-spec.txt | Ascii85 | 402.03 | 596.29 | 1.250 |
| commonmark-spec.txt | Z85 | 421.35 | 999.03 | 1.250 |
| commonmark-spec.txt | **Base85N** | **25.28** | **78.64** | 1.123 |
| countries.json | Base64 | 1188.39 | 1174.58 | 1.333 |
| countries.json | Ascii85 | 397.13 | 571.26 | 1.250 |
| countries.json | Z85 | 410.61 | 971.19 | 1.250 |
| countries.json | **Base85N** | **47.62** | **86.16** | 1.033 |
| countries.min.json | Base64 | 1238.73 | 1203.07 | 1.333 |
| countries.min.json | Ascii85 | 397.71 | 577.60 | 1.250 |
| countries.min.json | Z85 | 412.94 | 978.79 | 1.250 |
| countries.min.json | **Base85N** | **33.43** | **65.86** | 1.053 |
| grace_hopper.jpg | Base64 | 1106.08 | 1076.65 | 1.333 |
| grace_hopper.jpg | Ascii85 | 377.24 | 542.83 | 1.250 |
| grace_hopper.jpg | Z85 | 387.66 | 896.06 | 1.250 |
| grace_hopper.jpg | **Base85N** | **61.72** | **176.50** | 1.250 |
| minduka_present.png | Base64 | 1108.44 | 1119.37 | 1.333 |
| minduka_present.png | Ascii85 | 372.91 | 529.42 | 1.250 |
| minduka_present.png | Z85 | 382.35 | 876.72 | 1.250 |
| minduka_present.png | **Base85N** | **66.83** | **177.20** | 1.250 |
| sql-wasm.wasm | Base64 | 1086.15 | 1073.30 | 1.333 |
| sql-wasm.wasm | Ascii85 | 370.03 | 532.68 | 1.247 |
| sql-wasm.wasm | Z85 | 370.38 | 855.80 | 1.250 |
| sql-wasm.wasm | **Base85N** | **62.38** | **168.60** | 1.246 |

Z85 rows for inputs whose length is not a multiple of 4 are measured over
the largest 4-byte-aligned prefix. The last three files were measured
after the machine had been under load for a while and all four codecs
drop by a similar ~10 %; compare within a row, not across the table.

Reading it:

- **Binary input, encode: ~6× slower than the other Base85s**, ~18× slower
  than scalar Base64. Base85N runs the passthrough analysis even when the
  answer is always "use block mode".
- **Binary input, decode: ~5× slower than Z85**, ~3× slower than
  Ascii85. Decoding has no search to do; the gap is Base85N's per-group
  work plus the DP signal check.
- **Text input is slower than binary, not faster** (25–48 MB/s encode)
  even though it produces far less output. The mode decision is the cost,
  and it is paid precisely where passthrough succeeds.
- **Ascii85 decodes faster than it encodes** (580 vs 395 MB/s): encoding
  does five divisions per group, decoding five multiply-accumulates.

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
