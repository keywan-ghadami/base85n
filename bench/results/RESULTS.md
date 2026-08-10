# Base85N benchmark results

Base85N measured against Base64, Ascii85 (Adobe/btoa), Z85 (ZeroMQ
RFC 32) and RFC 1924 Base85, on encoded size and on encode/decode
throughput. Methodology, corpus provenance and how to reproduce all of
this: [../README.md](../README.md).

Numbers produced 2026-08-10 on an Intel Xeon @ 2.80 GHz (4 vCPU),
Ubuntu 24.04, gcc 13.3.0 `-O2`, CPython 3.11.15.

---

## Summary

**Size.** Across 4.94 MB of real files Base85N encodes to **1.150 chars
per byte** against Base64's 1.333 — **13.7 % smaller**. The other Base85
variants land at 1.250 (RFC 1924) and 1.200 (Ascii85, helped by one
zero-padded binary). On text-shaped input Base85N pulls well clear of
every other codec: pretty-printed JSON encodes at **1.033**, i.e. 3 %
overhead where Base64 costs 33 %, and the output stays readable. On
high-entropy binaries it is 1.246–1.250, exactly where a Base85 should
be, with no regression against the others.

**Throughput.** Base85N's C encoder runs at **54–82 MB/s** on binary
input against Base64's ~1240 MB/s in the same harness — roughly **20×
slower**. Decoding is **183–203 MB/s**, about 6× slower than Base64.
That is the price of the mode decision and the passthrough transform;
whether it matters depends on whether you are bound by CPU or by the
bytes on the wire.

**One finding that needs a decision.** The encoder is **quadratic in the
length of an escape-heavy run**, in every implementation, and it is
reachable from ordinary content. The CommonMark specification — plain
Markdown — encodes at **0.22 MB/s**, over 200× slower than the JSON samples
and 5600× slower than Base64. A 100 kB buffer of `~` takes **14.3
seconds** in optimized C. Details, evidence and a proposed fix in
[Finding: quadratic encoding](#finding-the-encoder-is-quadratic-on-escape-heavy-input)
below.

---

## Size

### Corpus files — expansion ratio (encoded chars per input byte)

| sample | input | Base64 | Ascii85 | Z85 | Base85 (RFC 1924) | Base85N | Base85N vs Base64 |
|---|---|---|---|---|---|---|---|
| sql-wasm.wasm | 659,730 B | 1.333 | 1.247 | n/a | 1.250 | 1.246 | +6.5 % |
| _cffi_backend.so | 1,068,624 B | 1.333 | 1.026 | 1.250 | 1.250 | 1.246 | +6.5 % |
| DejaVuSans.ttf | 756,072 B | 1.333 | 1.240 | 1.250 | 1.250 | 1.248 | +6.4 % |
| countries.json | 1,408,911 B | 1.333 | 1.250 | n/a | 1.250 | **1.033** | +22.6 % |
| countries.min.json | 772,294 B | 1.333 | 1.250 | n/a | 1.250 | **1.053** | +21.0 % |
| commonmark-spec.txt | 202,827 B | 1.333 | 1.250 | n/a | 1.250 | **1.123** | +15.8 % |
| grace_hopper.jpg | 61,306 B | 1.333 | 1.250 | n/a | 1.250 | 1.250 | +6.3 % |
| minduka_present.png | 13,634 B | 1.333 | 1.250 | n/a | 1.250 | 1.250 | +6.3 % |

`n/a` for Z85 means the input length is not a multiple of 4, which Z85
does not define.

Two things worth reading twice:

- **Ascii85 wins the ELF sample** at 1.026, and it is not a fluke:
  `_cffi_backend.so` is full of zero padding and Ascii85's `z` shorthand
  collapses each all-zero 4-byte group to a single character. Base85N
  has no equivalent zero-run shorthand and lands at 1.246. If your
  payloads are sparse binaries, that is a real advantage Ascii85 has.
- **The two JSON rows are the same data**, once pretty-printed and once
  minified. Base85N encodes the pretty-printed form *more* efficiently
  (1.033 vs 1.053) because indentation is runs of spaces, which
  passthrough carries almost free — while for every other codec the
  extra whitespace is just 33 % more bytes to expand.

### Corpus totals

| codec | total encoded | ratio | vs Base64 |
|---|---|---|---|
| Base64 | 6,591,204 chars | 1.3333 | +0.00 % |
| Ascii85 | 5,930,050 chars | 1.1996 | +10.03 % |
| Z85 | not applicable to 6 of 8 samples | – | – |
| Base85 (RFC 1924) | 6,179,250 chars | 1.2500 | +6.25 % |
| **Base85N** | **5,686,506 chars** | **1.1503** | **+13.73 %** |

Total input: 4,943,398 bytes across 8 files.

### Short protocol fields

The payloads that actually dominate most traffic. Figures are encoded
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
character-for-character identical in length to the other Base85
variants — a name, a phone number or an IPv4 address gets no passthrough
benefit, only the 5:4 block ratio. The gain appears from roughly 24 bytes
upward and grows with how text-like the field is: +23.1 % on a JWT,
+21.9 % on a SQL statement, +21.6 % on a CSV row.

The 4-byte customer number is the one row where every Base85 looks
spectacular (+37.5 %) — that is Base64's padding, not a Base85 virtue.

### What the output looks like

The size table understates the practical difference on text, because
Base85N's output stays inspectable:

```
input    {"id":184223,"name":"Ada Lovelace","phone":"+493023125190",...}
Base64   eyJpZCI6MTg0MjIzLCJuYW1lIjoiQWRhIExvdmVsYWNlIiwicGhvbmUiOiIrNDk...
Base85N  %nS{A{+id+~:184223^+name+~:+Ada:Lovelace+^+phone+~:+~+4930231...

input    2026-08-10T07:12:44Z INFO  order.service  order=184223 user=ada ...
Base64   MjAyNi0wOC0xMFQwNzoxMjo0NFogSU5GTyAgb3JkZXIuc2VydmljZSAgb3JkZXI...
Base85N  %nSjd2026-08-10T07~:12~:44Z:INFO::order.service::order=184223:...
```

### Alphabet safety

Size is not the only axis. Of the four Base85 variants only Base85N
avoids every character that has to be escaped inside a JSON string, XML
or HTML text, or a shell argument:

| codec | contains `"` `'` `\` `` ` `` `<` `>` `&` | safe to drop into JSON/XML unescaped |
|---|---|---|
| Base64 | no | yes |
| Ascii85 | `"` `'` `\` `` ` `` `<` `>` `&` | no |
| Z85 | `<` `>` `&` | no |
| Base85 (RFC 1924) | `` ` `` `<` `>` `&` | no |
| **Base85N** | **none** | **yes** |

This matters for the comparison: the two codecs that are competitive
with Base85N on size are both unusable as a drop-in for Base64 in a JSON
or XML payload without a second escaping layer, which would give back
more than the 6 % they saved.

---

## Throughput

C implementations, scalar, same harness, same flags, every codec
allocating its output with `malloc()` on every call, every measurement
round-trip verified. MB/s counts **original (decoded) bytes**, so encode
and decode columns are directly comparable. Best of 3 rounds.

The Base64/Ascii85/Z85 implementations here are straightforward
table-driven scalar code, deliberately written at the same level of
effort as the Base85N implementation they are compared against. A tuned
SIMD Base64 is several times faster than the ~1240 MB/s shown here. The
number to read off is *what Base85N's extra work costs relative to a
plain scalar codec*, not "Base85N versus the fastest Base64 in
existence".

| input | codec | encode MB/s | decode MB/s | ratio |
|---|---|---|---|---|
| synthetic random 1 MiB | Base64 | 1204.32 | 1167.11 | 1.333 |
| synthetic random 1 MiB | Ascii85 | 400.21 | 583.30 | 1.250 |
| synthetic random 1 MiB | Z85 | 412.89 | 976.02 | 1.250 |
| synthetic random 1 MiB | **Base85N** | **58.57** | **202.84** | 1.250 |
| synthetic text 1 MiB | Base64 | 1251.44 | 1213.60 | 1.333 |
| synthetic text 1 MiB | Ascii85 | 405.99 | 590.03 | 1.250 |
| synthetic text 1 MiB | Z85 | 416.70 | 993.30 | 1.250 |
| synthetic text 1 MiB | **Base85N** | **34.19** | **88.93** | **1.010** |
| escape-heavy 16 KiB | Base64 | 1260.19 | 1217.08 | 1.333 |
| escape-heavy 16 KiB | Ascii85 | 407.21 | 590.70 | 1.250 |
| escape-heavy 16 KiB | Z85 | 419.97 | 1001.23 | 1.250 |
| escape-heavy 16 KiB | **Base85N** | **0.05** | 201.21 | 1.250 |
| DejaVuSans.ttf | Base64 | 1244.82 | 1209.70 | 1.333 |
| DejaVuSans.ttf | Ascii85 | 409.39 | 592.91 | 1.240 |
| DejaVuSans.ttf | Z85 | 419.03 | 995.76 | 1.250 |
| DejaVuSans.ttf | **Base85N** | **57.89** | **197.54** | 1.248 |
| _cffi_backend.so | Base64 | 1227.42 | 1187.50 | 1.333 |
| _cffi_backend.so | Ascii85 | 469.82 | 636.82 | 1.026 |
| _cffi_backend.so | Z85 | 416.63 | 990.05 | 1.250 |
| _cffi_backend.so | **Base85N** | **81.50** | **192.61** | 1.246 |
| commonmark-spec.txt | Base64 | 1237.37 | 1175.09 | 1.333 |
| commonmark-spec.txt | Ascii85 | 402.30 | 588.96 | 1.250 |
| commonmark-spec.txt | Z85 | 410.67 | 970.23 | 1.250 |
| commonmark-spec.txt | **Base85N** | **0.22** | **89.16** | 1.123 |
| countries.json | Base64 | 1227.34 | 1118.31 | 1.333 |
| countries.json | Ascii85 | 399.93 | 580.38 | 1.250 |
| countries.json | Z85 | 412.15 | 982.74 | 1.250 |
| countries.json | **Base85N** | **47.66** | **100.32** | 1.033 |
| countries.min.json | Base64 | 1222.79 | 1128.36 | 1.333 |
| countries.min.json | Ascii85 | 386.78 | 585.21 | 1.250 |
| countries.min.json | Z85 | 415.02 | 991.85 | 1.250 |
| countries.min.json | **Base85N** | **33.00** | **88.00** | 1.053 |
| grace_hopper.jpg | Base64 | 1226.71 | 1200.42 | 1.333 |
| grace_hopper.jpg | Ascii85 | 394.11 | 595.19 | 1.250 |
| grace_hopper.jpg | Z85 | 423.13 | 1000.52 | 1.250 |
| grace_hopper.jpg | **Base85N** | **54.10** | **199.18** | 1.250 |
| minduka_present.png | Base64 | 1257.66 | 1220.72 | 1.333 |
| minduka_present.png | Ascii85 | 407.31 | 595.12 | 1.250 |
| minduka_present.png | Z85 | 418.56 | 993.16 | 1.250 |
| minduka_present.png | **Base85N** | **60.04** | **196.89** | 1.250 |
| sql-wasm.wasm | Base64 | 1248.93 | 1200.22 | 1.333 |
| sql-wasm.wasm | Ascii85 | 401.98 | 584.30 | 1.247 |
| sql-wasm.wasm | Z85 | 412.34 | 902.65 | 1.250 |
| sql-wasm.wasm | **Base85N** | **54.27** | **183.20** | 1.246 |

Z85 rows for inputs whose length is not a multiple of 4 are measured over
the largest 4-byte-aligned prefix.

Reading the table:

- **Binary input, encode: ~20× slower than scalar Base64** (54–82 MB/s
  vs ~1240). Base85N pays for the per-window passthrough analysis even
  when the answer is always "use block mode".
- **Binary input, decode: ~6× slower** (183–203 MB/s). Decoding has no
  search to do; the gap is the per-group base-85 division work, which
  Ascii85 and Z85 also pay — they run at 580–1000 MB/s because their
  inner loop is simpler.
- **Text input is slower than binary, not faster** (33–48 MB/s encode,
  88–100 MB/s decode) even though it produces far less output. The mode
  decision is the cost, and it is paid where passthrough actually
  succeeds.
- **Ascii85 decode beats its own encode** (590 vs 400 MB/s) because
  encoding does five divisions per group while decoding does five
  multiply-accumulates.

If the workload is CPU-bound and the data is high-entropy binary,
Base85N costs roughly 20× the encode time to save 6.25 % of bytes over
Base64 — a bad trade. If the data is text-shaped and the constraint is
payload size, it saves 15–23 % and stays readable, and 34 MB/s per core
is far above most real request rates.

---

## Finding: the encoder is quadratic on escape-heavy input

The `escape-heavy 16 KiB` row above (0.05 MB/s) and the
`commonmark-spec.txt` row (0.22 MB/s) are the same bug, not a quirk of
one file.

### What happens

`encode()` loops. On each pass:

1. **Pass 1** scans forward from the current position to the end of the
   maximal *representable* run and ORs together the R-Set mask of every
   byte in it. The mask depends on the whole run, so this scan cannot
   stop early.
2. **Pass 2** re-walks that run under the now-fixed mask and stops at the
   4th consecutive byte that needs escaping (`MAX_CONSECUTIVE_ESCAPES`).
3. If the resulting candidate is shorter than 20 bytes, Dynamic
   Passthrough is rejected and the encoder emits **4 bytes** in block
   mode and loops.

When escapes are dense, step 2 gives up after 3 bytes while step 1 has
just scanned everything to the end of the run. Each iteration therefore
does O(remaining) work and advances 4 bytes: **O(n²)**.

### Evidence

Measured amplification — window bytes scanned by Pass 1 per input byte,
Python implementation:

| input | input size | Pass 1 iterations | window bytes scanned | scanned / input |
|---|---|---|---|---|
| countries.min.json | 772,294 B | 1 | 772,294 | **1.0×** |
| countries.json | 1,408,911 B | 5 | 1,408,903 | **1.0×** |
| commonmark-spec.txt | 202,827 B | 10,517 | 83,316,230 | **410.8×** |

Scaling, `b"~" * N` (every byte needs escaping), both implementations:

| N | Python encode | C encode | growth on doubling |
|---|---|---|---|
| 12,500 B | – | 0.2281 s | – |
| 25,000 B | – | 0.9175 s | ×4.02 |
| 50,000 B | – | 3.6409 s | ×3.97 |
| 100,000 B | – | 14.3039 s | ×3.93 |
| 2,000 B | 0.0684 s | – | – |
| 4,000 B | 0.2484 s | – | ×3.63 |
| 8,000 B | 0.9874 s | – | ×3.97 |
| 16,000 B | 3.8021 s | – | ×3.85 |
| 32,000 B | 15.0240 s | – | ×3.95 |

A clean ×4 per doubling in both languages. The C implementation needs
**14.3 seconds** for 100 kB; 1 MB would be roughly 25 minutes.

Reproducer:

```python
import base85n, time
t = time.perf_counter()
base85n.encode(b"~" * 32000)
print(time.perf_counter() - t)      # ~15 s
```

### Why it matters beyond this benchmark

- It is reachable from **ordinary content**, not just adversarial input.
  `commonmark-spec.txt` is plain Markdown, and the trigger is its code
  spans. The file contains 3,549 `>` characters; a `>` anywhere in a
  representable run sets the R-Set mask bit whose replacement character
  is `` ` ``, which makes every one of the file's 43,278 literal
  backticks an escaped byte. 1,303 of those are runs of four or more —
  each one aborts Pass 2 immediately after Pass 1 scanned the rest of
  the run. That is enough to drop the encoder to 0.22 MB/s. (The 87
  tildes in the file are incidental; backticks do essentially all of
  the damage.)
- It is an **availability concern for encoders**, which
  [SECURITY.md](../../SECURITY.md) currently does not cover — it warns
  about decoding untrusted input, while this is triggered by *encoding*
  attacker-influenced text. A few hundred kB of `~` is enough to occupy
  a core for minutes.
- It affects **every implementation**, because all five follow the
  specification's two-pass description literally. Confirmed here in C
  and Python; the Rust, Go and TypeScript encoders share the structure.

### Proposed fix

The fix is an implementation optimization only — **no specification
change, and byte-identical output**:

Pass 1's result for the run starting at position `i+k` is a *suffix* of
its result for the run starting at `i`. So for each maximal
representable run, scan it **once**, precompute suffix R-Set masks, and
have subsequent iterations inside that run read the mask in O(1) instead
of rescanning. Pass 2 already costs only what it consumes. That makes
encoding linear while leaving every emitted byte exactly as it is today —
the shared test vectors would verify precisely that.

This has not been implemented. It touches the encoder in all five
languages, which is a larger change than a benchmark should make on its
own.
