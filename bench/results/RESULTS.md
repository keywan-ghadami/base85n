# Base85N benchmark results

Base85N against Base64, Ascii85 (Adobe/btoa), Z85 (ZeroMQ RFC 32) and
RFC 1924 Base85, on encoded size and throughput. How it is measured and how
to reproduce it: [../README.md](../README.md).

**What this comparison is for.** Base85N exists to replace Base64 where Base64
is used to embed data in a text-based format and the size or the cleanliness of
the result matters. Comparing it against the other Base85 variants is not that
use case — it answers the question that follows immediately from it: *if Base64
costs too many characters, why not simply use one of the Base85s that already
exist?* The tables below answer it by charging every codec for the escaping its
alphabet forces in the place the output actually goes.

Measured against specification v0.5.0, over a 6.52 MB corpus of 13 real files.
Size was measured on 2026-08-16 and does not depend on the machine. Throughput
was re-measured on 2026-08-17 after the C implementation was optimised — Intel
Xeon, Ubuntu 24.04, gcc 13.3.0 `-O2` — and does depend on the machine, which is
why each table carries the three other codecs measured beside Base85N on the
same silicon, why the before-and-after table builds and times both
implementations in one run, and why the comparison between *specification*
versions further down uses instruction counts instead.

**The size tables are ordered by where encoded text actually goes.** A payload
in a JSON field, an HTML attribute or an XML element is the normal case; a
payload sitting raw in a file or on a socket is the exception. The raw table is
still here, at the end of the size section, as the reference it is — but
reading it first overstates every fixed-ratio codec, because it charges none of
them for the escaping their alphabets force.

---

## Summary

**Base85N is the smallest of the five in every embedding but one** — 1.007
characters per input byte across the corpus in JSON, in HTML and in XML, where
Base64 costs 1.333 and the other Base85s cost 1.250 to 1.486. Raw, it is 1.007
against Base64's 1.333 and 1.250 for Z85 and RFC 1924. Nothing else here comes
near 1.0: Dynamic Passthrough spends one character per byte, and a Fill signal
spends five characters on a run that would otherwise cost hundreds.

**The alphabet is free in three of the four embeddings.** Base85N's output
contains no `"`, `\`, `&`, `<` or `>`, so JSON, HTML and XML take it verbatim
and its number does not move between those tables. Every other codec's does:
Ascii85 goes from 1.188 raw to 1.486 in an HTML attribute, and Z85 and RFC 1924
both land above Base64 there.

**The exception is a URL query string, and it is not close.** Percent-encoded
to RFC 3986's unreserved set, Base85N costs 1.463 against Base64's 1.354 — 8 %
worse. `#`, `%`, `+`, `?` and `&` are in Alphabet-N precisely because they are
free in JSON and XML, and they are exactly what a URL encoder charges three
characters for. Use Base64url in a query string.

**On structured text it encodes to less than the input.** Pretty-printed JSON
lands at **0.935** and the CommonMark specification at **0.859**. Against the
best other Base85 that is 25 % and 31 % smaller.

**Block-padded and zero-padded binaries are the other big win.** An
uncompressed tar encodes at **0.767** and a zero-padded ELF at **0.965**, where
every fixed-ratio Base85 pays 1.250 and Ascii85 — the codec that used to hold
that row — pays 1.026.

**Speed is no longer the flat cost it was.** Base85N **decodes fastest of the
four on 13 of the 16 inputs**, and on encoding it now beats the other two
Base85s on 12 of them — not only on the structured text it exists for (+20 %
on CSS, +35 % on Python, +26 % on minified JSON, +66 % on the synthetic text)
but on high-entropy binary, where it used to draw with them and is now +207 %
on random bytes, +205 % on JPEG, +187 % on PNG and +50 % on WebAssembly.
Base64 still encodes fastest of the four everywhere, its 6→8-bit repack being
a different amount of work altogether, but on those binary rows its lead is
down from about 3× to **9 %**.

Binary encoding moved in two steps, neither of which changed an output
character. First the lookahead that skips over block-mode runs was gating its
passthrough test on a single byte — a branch nothing can predict on
high-entropy input, taken once per four bytes of the whole file. Asking for
eight instead turns away no position the one-byte test would have accepted,
and was +195 % on random bytes. Then the same loop was made to settle two
4-byte groups per word rather than one, and the passthrough scan in the main
loop was gated the same way, which is a further +50 % on random bytes, +53 %
on JPEG and +7 % to +10 % on the object files. Both were found while
measuring a proposed `--binary` encoder flag, which is written up in
[binary-flag.md](binary-flag.md) — and the second one closed that flag's case
entirely on the inputs it was aimed at.

Against the release before the 0.5.0 optimisation work, on one core and the
same machine: **encoding is 1.2× to 3.0× faster and decoding 1.0× to 1.3×**,
with byte-for-byte identical output. Cores are still the answer to the
largest encodes, and the per-core figure is now a much better starting point
for them.

---

## Size

### Inside a JSON string literal — expansion ratio (characters per input byte)

The commonest destination of an encoded payload, and therefore
the first table: what each codec costs per file once its output is
placed in a JSON string literal, `"` and `\` escaped. Last row is
the whole corpus.

| sample | input | Base64 | Ascii85 | Z85 | Base85 (RFC 1924) | Base85N | vs Base64 | vs best other Base85 |
|---|---|---|---|---|---|---|---|---|
| sql-wasm.wasm | 659,730 B | 1.333 | 1.291 | 1.250 | 1.250 | **1.239** | -7.1 % | -0.9 % |
| _cffi_backend.so | 1,068,624 B | 1.333 | 1.070 | 1.250 | 1.250 | **0.965** | -27.6 % | -9.8 % |
| DejaVuSans.ttf | 756,072 B | 1.333 | 1.287 | 1.250 | 1.250 | **1.232** | -7.6 % | -1.4 % |
| requests-2.32.3.tar | 655,360 B | 1.333 | 1.027 | 1.250 | 1.250 | **0.767** | -42.5 % | -25.3 % |
| countries.json | 1,408,911 B | 1.333 | 1.257 | 1.250 | 1.250 | **0.935** | -29.9 % | -25.2 % |
| countries.min.json | 772,294 B | 1.333 | 1.268 | 1.250 | 1.250 | **1.003** | -24.8 % | -19.8 % |
| lodash.js | 544,098 B | 1.333 | 1.265 | 1.250 | 1.250 | **1.004** | -24.7 % | -19.6 % |
| bootstrap.css | 281,046 B | 1.333 | 1.267 | 1.250 | 1.250 | **1.003** | -24.7 % | -19.7 % |
| requests-models.py | 35,418 B | 1.333 | 1.264 | 1.250 | 1.250 | **0.973** | -27.1 % | -22.2 % |
| commonmark-spec.txt | 202,827 B | 1.333 | 1.263 | 1.250 | 1.250 | **0.859** | -35.6 % | -31.3 % |
| requests-history.md | 60,368 B | 1.333 | 1.264 | 1.250 | 1.250 | **0.979** | -26.6 % | -21.7 % |
| grace_hopper.jpg | 61,306 B | 1.333 | 1.281 | 1.250 | 1.250 | **1.249** | -6.4 % | -0.1 % |
| minduka_present.png | 13,634 B | 1.333 | 1.281 | **1.250** | **1.250** | **1.250** | -6.3 % | same |
| whole corpus | 6,519,688 B | 1.333 | 1.213 | 1.250 | 1.250 | **1.007** | -24.5 % | -17.0 % |

**Bold** marks the smallest output in that row; on a tie every codec that reaches it is marked. The two delta columns are Base85N's size difference — **negative is a saving**, positive means Base85N is larger.

### Inside an HTML attribute — expansion ratio (characters per input byte)

A double-quoted attribute value — `data-*`, `value=`, an inline
`src` — with `&`, `<`, `>` and `"` escaped. Last row is the whole
corpus.

| sample | input | Base64 | Ascii85 | Z85 | Base85 (RFC 1924) | Base85N | vs Base64 | vs best other Base85 |
|---|---|---|---|---|---|---|---|---|
| sql-wasm.wasm | 659,730 B | 1.333 | 1.573 | 1.394 | 1.365 | **1.239** | -7.1 % | -9.3 % |
| _cffi_backend.so | 1,068,624 B | 1.333 | 1.368 | 1.339 | 1.334 | **0.965** | -27.6 % | -27.6 % |
| DejaVuSans.ttf | 756,072 B | 1.333 | 1.593 | 1.363 | 1.363 | **1.232** | -7.6 % | -9.6 % |
| requests-2.32.3.tar | 655,360 B | 1.333 | 1.206 | 1.320 | 1.333 | **0.767** | -42.5 % | -36.4 % |
| countries.json | 1,408,911 B | 1.333 | 1.638 | 1.375 | 1.341 | **0.935** | -29.9 % | -30.3 % |
| countries.min.json | 772,294 B | 1.333 | 1.441 | 1.372 | 1.380 | **1.003** | -24.8 % | -27.0 % |
| lodash.js | 544,098 B | 1.333 | 1.499 | 1.331 | 1.352 | **1.004** | -24.7 % | -24.5 % |
| bootstrap.css | 281,046 B | 1.333 | 1.491 | 1.338 | 1.359 | **1.003** | -24.7 % | -25.0 % |
| requests-models.py | 35,418 B | 1.333 | 1.555 | 1.335 | 1.340 | **0.973** | -27.1 % | -27.2 % |
| commonmark-spec.txt | 202,827 B | 1.333 | 1.415 | 1.333 | 1.343 | **0.859** | -35.6 % | -35.6 % |
| requests-history.md | 60,368 B | 1.333 | 1.434 | 1.346 | 1.384 | **0.979** | -26.6 % | -27.3 % |
| grace_hopper.jpg | 61,306 B | 1.333 | 1.479 | 1.399 | 1.395 | **1.249** | -6.4 % | -10.5 % |
| minduka_present.png | 13,634 B | 1.333 | 1.478 | 1.391 | 1.404 | **1.250** | -6.3 % | -10.2 % |
| whole corpus | 6,519,688 B | 1.333 | 1.486 | 1.357 | 1.351 | **1.007** | -24.5 % | -25.5 % |

**Bold** marks the smallest output in that row; on a tie every codec that reaches it is marked. The two delta columns are Base85N's size difference — **negative is a saving**, positive means Base85N is larger.

### Inside XML character data — expansion ratio (characters per input byte)

The same per file inside XML character data, with `&`, `<` and
`>` escaped. Last row is the whole corpus.

| sample | input | Base64 | Ascii85 | Z85 | Base85 (RFC 1924) | Base85N | vs Base64 | vs best other Base85 |
|---|---|---|---|---|---|---|---|---|
| sql-wasm.wasm | 659,730 B | 1.333 | 1.395 | 1.394 | 1.365 | **1.239** | -7.1 % | -9.3 % |
| _cffi_backend.so | 1,068,624 B | 1.333 | 1.185 | 1.339 | 1.334 | **0.965** | -27.6 % | -18.5 % |
| DejaVuSans.ttf | 756,072 B | 1.333 | 1.410 | 1.363 | 1.363 | **1.232** | -7.6 % | -9.6 % |
| requests-2.32.3.tar | 655,360 B | 1.333 | 1.174 | 1.320 | 1.333 | **0.767** | -42.5 % | -34.7 % |
| countries.json | 1,408,911 B | 1.333 | 1.619 | 1.375 | 1.341 | **0.935** | -29.9 % | -30.3 % |
| countries.min.json | 772,294 B | 1.333 | 1.408 | 1.372 | 1.380 | **1.003** | -24.8 % | -27.0 % |
| lodash.js | 544,098 B | 1.333 | 1.471 | 1.331 | 1.352 | **1.004** | -24.7 % | -24.5 % |
| bootstrap.css | 281,046 B | 1.333 | 1.442 | 1.338 | 1.359 | **1.003** | -24.7 % | -25.0 % |
| requests-models.py | 35,418 B | 1.333 | 1.516 | 1.335 | 1.340 | **0.973** | -27.1 % | -27.2 % |
| commonmark-spec.txt | 202,827 B | 1.333 | 1.379 | 1.333 | 1.343 | **0.859** | -35.6 % | -35.6 % |
| requests-history.md | 60,368 B | 1.333 | 1.390 | 1.346 | 1.384 | **0.979** | -26.6 % | -27.3 % |
| grace_hopper.jpg | 61,306 B | 1.333 | 1.398 | 1.399 | 1.395 | **1.249** | -6.4 % | -10.5 % |
| minduka_present.png | 13,634 B | 1.333 | 1.399 | 1.391 | 1.404 | **1.250** | -6.3 % | -10.2 % |
| whole corpus | 6,519,688 B | 1.333 | 1.399 | 1.357 | 1.351 | **1.007** | -24.5 % | -25.5 % |

**Bold** marks the smallest output in that row; on a tie every codec that reaches it is marked. The two delta columns are Base85N's size difference — **negative is a saving**, positive means Base85N is larger.

### As a URL query value — expansion ratio (characters per input byte)

Percent-encoded down to RFC 3986's unreserved set, which is what
every library's default does. **This is the embedding Base85N is
worst at**, and it is worst at it by design: `#`, `%`, `+`, `?` and
`&` are in Alphabet-N because they are safe in JSON and XML, and
they are exactly the characters a URL encoder charges three
characters for. Base64url — not measured here — is the right tool
for a query string. Last row is the whole corpus.

An HTTP header value needs no table: RFC 9110 admits any visible
ASCII, so all five codecs cost their raw length there.

| sample | input | Base64 | Ascii85 | Z85 | Base85 (RFC 1924) | Base85N | vs Base64 | vs best other Base85 |
|---|---|---|---|---|---|---|---|---|
| sql-wasm.wasm | 659,730 B | **1.351** | 2.188 | 1.759 | 1.738 | 1.693 | +25.3 % | -2.6 % |
| _cffi_backend.so | 1,068,624 B | **1.378** | 1.919 | 1.586 | 1.572 | 1.446 | +4.9 % | -8.0 % |
| DejaVuSans.ttf | 756,072 B | **1.424** | 2.203 | 1.770 | 1.751 | 1.738 | +22.0 % | -0.7 % |
| requests-2.32.3.tar | 655,360 B | 1.334 | 1.597 | 1.606 | 1.585 | **1.003** | -24.8 % | -36.7 % |
| countries.json | 1,408,911 B | **1.333** | 2.114 | 1.754 | 1.747 | 1.439 | +7.9 % | -17.7 % |
| countries.min.json | 772,294 B | **1.333** | 2.022 | 1.783 | 1.752 | 1.415 | +6.1 % | -19.2 % |
| lodash.js | 544,098 B | **1.335** | 2.043 | 1.716 | 1.689 | 1.692 | +26.8 % | +0.2 % |
| bootstrap.css | 281,046 B | 1.334 | 2.001 | 1.695 | 1.673 | **1.314** | -1.5 % | -21.4 % |
| requests-models.py | 35,418 B | 1.335 | 2.035 | 1.733 | 1.700 | **1.291** | -3.3 % | -24.1 % |
| commonmark-spec.txt | 202,827 B | 1.346 | 1.985 | 1.833 | 1.800 | **1.220** | -9.4 % | -32.2 % |
| requests-history.md | 60,368 B | 1.334 | 2.044 | 1.707 | 1.676 | **1.153** | -13.5 % | -31.2 % |
| grace_hopper.jpg | 61,306 B | **1.385** | 1.999 | 1.830 | 1.808 | 1.776 | +28.3 % | -1.8 % |
| minduka_present.png | 13,634 B | **1.426** | 2.007 | 1.866 | 1.840 | 1.810 | +26.9 % | -1.6 % |
| whole corpus | 6,519,688 B | **1.354** | 2.020 | 1.715 | 1.696 | 1.463 | +8.0 % | -13.7 % |

**Bold** marks the smallest output in that row; on a tie every codec that reaches it is marked. The two delta columns are Base85N's size difference — **negative is a saving**, positive means Base85N is larger.

### Raw, unembedded — expansion ratio (encoded chars per input byte)

The reference measurement: the encoded text on its own, in a
binary file or a socket, escaped by nobody.

| sample | input | Base64 | Ascii85 | Z85 | Base85 (RFC 1924) | Base85N | vs Base64 | vs best other Base85 |
|---|---|---|---|---|---|---|---|---|
| sql-wasm.wasm | 659,730 B | 1.333 | 1.247 | 1.250 | 1.250 | **1.239** | -7.1 % | -0.7 % |
| _cffi_backend.so | 1,068,624 B | 1.333 | 1.026 | 1.250 | 1.250 | **0.965** | -27.6 % | -5.9 % |
| DejaVuSans.ttf | 756,072 B | 1.333 | 1.240 | 1.250 | 1.250 | **1.232** | -7.6 % | -0.6 % |
| requests-2.32.3.tar | 655,360 B | 1.333 | 1.015 | 1.250 | 1.250 | **0.767** | -42.5 % | -24.4 % |
| countries.json | 1,408,911 B | 1.333 | 1.250 | 1.250 | 1.250 | **0.935** | -29.9 % | -25.2 % |
| countries.min.json | 772,294 B | 1.333 | 1.250 | 1.250 | 1.250 | **1.003** | -24.8 % | -19.8 % |
| lodash.js | 544,098 B | 1.333 | 1.250 | 1.250 | 1.250 | **1.004** | -24.7 % | -19.6 % |
| bootstrap.css | 281,046 B | 1.333 | 1.250 | 1.250 | 1.250 | **1.003** | -24.7 % | -19.7 % |
| requests-models.py | 35,418 B | 1.333 | 1.250 | 1.250 | 1.250 | **0.973** | -27.1 % | -22.2 % |
| commonmark-spec.txt | 202,827 B | 1.333 | 1.250 | 1.250 | 1.250 | **0.859** | -35.6 % | -31.3 % |
| requests-history.md | 60,368 B | 1.333 | 1.250 | 1.250 | 1.250 | **0.979** | -26.6 % | -21.7 % |
| grace_hopper.jpg | 61,306 B | 1.333 | 1.250 | 1.250 | 1.250 | **1.249** | -6.4 % | -0.1 % |
| minduka_present.png | 13,634 B | 1.333 | **1.250** | **1.250** | **1.250** | **1.250** | -6.3 % | same |

**Bold** marks the smallest output in that row; on a tie every codec that reaches it is marked. The two delta columns are Base85N's size difference — **negative is a saving**, positive means Base85N is larger.

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
| Base85N | 6,565,220 chars | 1.0070 | +24.48 % |

Total input: 6,519,688 bytes across 13 files.

### What the output looks like

The size table understates the practical difference on text, because
Base85N's output stays inspectable:

```
input    {"id":184223,"name":"Ada Lovelace","phone":"+493023125190"}      59 B
Base64   eyJpZCI6MTg0MjIzLCJuYW1lIjoiQWRhIExvdmVsYWNlIiwicGhvbmUiOiIrN...  80 chars
Base85N  %nVm.{^id^:184223?^name^:^Ada~Lovelace^?^phone^:^+493023125190^}  64 chars
```

`^` stands in for `"`, `?` for `,` and `~` for the space; the leading `%nVm.`
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
| synthetic random 1 MiB | **1508** | 446 | 442 | 1370 | -9 % | +207 % |
| synthetic text 1 MiB | **1475** | 447 | 447 | 757 | -49 % | +69 % |
| scan-heavy 1MiB | **1463** | 452 | 448 | 589 | -60 % | +30 % |
| DejaVuSans.ttf | **1514** | 449 | 445 | 639 | -58 % | +42 % |
| _cffi_backend.so | **1481** | 521 | 441 | 410 | -72 % | -21 % |
| bootstrap.css | **1534** | 457 | 450 | 559 | -64 % | +22 % |
| commonmark-spec.txt | **1535** | 456 | 453 | 455 | -70 % | -0 % |
| countries.json | **1504** | 443 | 443 | 477 | -68 % | +8 % |
| countries.min.json | **1466** | 447 | 445 | 575 | -61 % | +29 % |
| grace_hopper.jpg | **1538** | 459 | 456 | 1403 | -9 % | +205 % |
| lodash.js | **1537** | 448 | 451 | 421 | -73 % | -7 % |
| minduka_present.png | **1502** | 467 | 474 | 1363 | -9 % | +187 % |
| requests-2.32.3.tar | **1476** | 576 | 456 | 561 | -62 % | -3 % |
| requests-history.md | **1497** | 443 | 449 | 544 | -64 % | +21 % |
| requests-models.py | **1492** | 462 | 462 | 590 | -60 % | +28 % |
| sql-wasm.wasm | **1459** | 450 | 446 | 677 | -54 % | +50 % |

**Bold** marks the fastest codec in that row. The two delta columns are how much faster Base85N is than that codec -- **positive is faster**, negative means Base85N is slower.

### Decode throughput (MB/s of original bytes)

| input | Base64 | Ascii85 | Z85 | Base85N | vs Base64 | vs best other Base85 |
|---|---|---|---|---|---|---|
| synthetic random 1 MiB | 1367 | 890 | 1146 | **1641** | +20 % | +43 % |
| synthetic text 1 MiB | 1357 | 881 | 1119 | **1546** | +14 % | +38 % |
| scan-heavy 1MiB | 1375 | 870 | 1155 | **1633** | +19 % | +41 % |
| DejaVuSans.ttf | 1372 | 876 | 1109 | **1630** | +19 % | +47 % |
| _cffi_backend.so | **1334** | 898 | 1123 | 1072 | -20 % | -4 % |
| bootstrap.css | 1388 | 886 | 1151 | **1568** | +13 % | +36 % |
| commonmark-spec.txt | **1391** | 905 | 1150 | 1305 | -6 % | +13 % |
| countries.json | **1351** | 874 | 1132 | 1060 | -21 % | -6 % |
| countries.min.json | 1341 | 882 | 1145 | **1544** | +15 % | +35 % |
| grace_hopper.jpg | 1391 | 903 | 1151 | **1600** | +15 % | +39 % |
| lodash.js | 1388 | 895 | 1158 | **1470** | +6 % | +27 % |
| minduka_present.png | 1378 | 885 | 1156 | **1615** | +17 % | +40 % |
| requests-2.32.3.tar | 1388 | 1124 | 1159 | **1751** | +26 % | +51 % |
| requests-history.md | 1354 | 883 | 1144 | **1371** | +1 % | +20 % |
| requests-models.py | 1312 | 906 | 1147 | **1381** | +5 % | +20 % |
| sql-wasm.wasm | 1356 | 870 | 1130 | **1552** | +14 % | +37 % |

**Bold** marks the fastest codec in that row. The two delta columns are how much faster Base85N is than that codec -- **positive is faster**, negative means Base85N is slower.

**Encoding** is still where Base85N does the extra work, and it is still
slower than Base64, which does a 6→8-bit repack with no division at all. What
has changed is the comparison with the other two Base85s, which do one division
chain per group and nothing else. Base85N encodes faster than both of them on
the structured text it is designed for — JSON, CSS, Markdown, Python and the
CommonMark specification — and now also on high-entropy binary, which is the
newer result: random bytes, JPEG and PNG are all better than twice as fast as
Ascii85 and Z85, where they used to draw.

Binary got there by not paying for a decision it was never going to take. On
such input the encoder is one long block-mode run, and everything between
decision points is skipped over in a single pass; what that pass cost was one
table lookup per four bytes, to ask whether a passthrough segment might start
there. A third of byte values are representable, so on binary that question is
a coin flip — a branch nothing can predict, asked 262,144 times per megabyte.
A passthrough segment needs twenty representable bytes in a row, so asking for
eight up front rules out no position the one-byte test would have kept, and it
is right about 499 times in 500. [binary-flag.md](binary-flag.md) has the
attribution.

The row where Base85N is still clearly behind is the zero-padded ELF. It
spends most of its length in constructs whose signal is emitted every few
bytes, so the signal arithmetic — not the scanning — sets the pace, and no
change to the lookahead reaches far into it: `_cffi_backend.so` has gained
18 % across both steps where random bytes gained 340 %.

**Decoding** is the fastest of the four on 13 of the 16 inputs, including
every binary one. A block group is the same 5→4 conversion every Base85 does, a
DP segment is one table lookup per character, and a Fill signal is a `memset`;
what was left was the bookkeeping around them, and the decoder no longer tests
its output buffer on constructs that cannot overrun it. Pretty-printed JSON is
the remaining laggard, as the one input dense enough in short segments for the
per-segment work to show.

The scan-heavy row is a shape built to defeat the mode decision: 18
representable bytes then one that no alphabet can carry, repeated, so a
candidate never reaches MIN_PASSTHROUGH_BYTES and the encoder scans and falls
back on every group. It is in the corpus because it is the worst case for
prefix identification, not because it resembles real input.

### Against the previous release

Same specification, same output, same machine. Two things moved, in two
measurements.

The encoder's **mode decision** went first: classifying a byte from one table
instead of three, retiring a repeated character with one bit test instead of a
branch that ordinary text cannot predict, measuring a run once instead of
counting it per byte, and keeping a segment's substitution table for the next
segment that asks for the same one. The decoder stopped testing its output
buffer on the constructs that provably cannot overrun it. Those two binaries
were built and timed in the same run, so the host's drift landed on both.

Then the **block-mode lookahead** of section 11.1, which is what the rest of
this table's binary rows are: it was gating its passthrough test on one byte,
an unpredictable branch resolved once per four bytes of every high-entropy
file. Widening that gate to eight bytes is measured separately and more
carefully — interleaved and paired, since some of its rows move by less than
this machine's spread — in [binary-flag.md](binary-flag.md). The left-hand
column below predates both changes; the right-hand column is the current
encoder.

| input | encode MB/s | | decode MB/s | |
|---|---|---|---|---|
| grace_hopper.jpg | 394 -> 1188 | 3.0x | 1377 -> 1582 | 1.1x |
| synthetic random 1 MiB | 380 -> 1129 | 3.0x | 1351 -> 1584 | 1.2x |
| synthetic text 1 MiB | 305 -> 748 | 2.5x | 1457 -> 1551 | 1.1x |
| sql-wasm.wasm | 334 -> 754 | 2.3x | 1276 -> 1616 | 1.3x |
| requests-models.py | 302 -> 620 | 2.1x | 1306 -> 1421 | 1.1x |
| commonmark-spec.txt | 225 -> 451 | 2.0x | 1183 -> 1293 | 1.1x |
| requests-history.md | 285 -> 560 | 2.0x | 1299 -> 1385 | 1.1x |
| requests-2.32.3.tar | 294 -> 551 | 1.9x | 1628 -> 1740 | 1.1x |
| bootstrap.css | 311 -> 548 | 1.8x | 1455 -> 1497 | 1.0x |
| lodash.js | 248 -> 422 | 1.7x | 1366 -> 1480 | 1.1x |
| DejaVuSans.ttf | 416 -> 685 | 1.6x | 1284 -> 1629 | 1.3x |
| countries.min.json | 359 -> 574 | 1.6x | 1446 -> 1528 | 1.1x |
| countries.json | 317 -> 496 | 1.6x | 927 -> 1067 | 1.2x |
| _cffi_backend.so | 298 -> 412 | 1.4x | 936 -> 1072 | 1.1x |
| scan-heavy 1MiB | 521 -> 714 | 1.4x | 1310 -> 1580 | 1.2x |
| minduka_present.png | 1003 -> 1185 | 1.2x | 1363 -> 1660 | 1.2x |

The right-hand column of each pair is the same run the two throughput tables
above were generated from, so the whole section is internally consistent.

Every file in the corpus encodes to exactly the same bytes as before, at every
step of the way. The implementations were run against each other over
randomised inputs and over synthetic signal streams under AddressSanitizer and
UndefinedBehaviorSanitizer; the lookahead change is additionally held to
character-for-character equality with the encoder it replaced, over the corpus
and over 30,000 generated cases (`make -C bench/speed binary-flag-selftest`).

---

## Encoding on several cores

The encoder is the slow direction, and it is the one that parallelises. Signals
carry their own mask, profile, value, length and order, and segment boundaries
are decided by the data rather than by where an encoder started, so encoders
begun at different offsets converge and their output can be spliced — spec
section 11.3 states the procedure. `encode_parallel` in the Rust crate does it,
and Python's `encode(data, threads=...)` exposes it.

There is no chunk-size parameter and no second canonical form: every thread
count returns the same string, which the test suite asserts rather than
assumes.

On this 4-core machine, 16 MiB per run, best of three:

| input | 1 thread | 2 | 4 | 8 |
|---|---|---|---|---|
| mixed synthetic | 191 MB/s | 372 (1.95×) | 514 (2.70×) | 648 (3.40×) |
| `_cffi_backend.so` ×16 | 218 MB/s | 392 (1.80×) | 548 (2.52×) | 664 (3.05×) |
| `countries.json` ×12 | 174 MB/s | 313 (1.80×) | 452 (2.60×) | 376 (2.16×) |

What bounds it is the convergence distance — how far a worker has to encode
before its chain of decisions rejoins the one before it. Measured at 3,900
random boundaries across the corpus: median 1,078 bytes, mean 9,892, 95th
percentile 50,604, maximum 226,279. Block mode does **not** resynchronise after
four bytes, which is the intuition that fails here: two encoders inside a
block-mode run stay four-byte-out-of-phase until some construct realigns them,
so a file that is one long passthrough stream (`bootstrap.css`, median 21,497)
or high-entropy binary (`_cffi_backend.so`, mean 32,377) can run a long way
first. Chunks are a megabyte at minimum for that reason, and anything smaller
is encoded on the calling thread.

---

## Against specification v0.4.0

Version 0.5.0 gives Fill a second variant: the same five characters carry a
short zero run *and* the two bytes beside it, so a run that ends one or two
bytes short of a group boundary no longer hands them back to block mode. In the
same version `MIN_FILL_IN_SEGMENT_BYTES` moves from 11 to 16, which trades 1 %
of ratio on text for more readable segments and a faster decoder. Size is the
comparison that matters, and it is machine-independent — the same 13 files
through both versions of the C library:

| | v0.4.0 | v0.5.0 |
|---|---|---|
| whole corpus | 1.02435 | **1.00698** |
| zero-padded ELF | 1.12618 | **0.96541** |
| uncompressed tar | 0.76266 | **0.76722** |
| WebAssembly module | 1.24222 | **1.23877** |
| TrueType font | 1.23724 | **1.23199** |
| JPEG photograph | 1.24867 | **1.24852** |
| PNG image | 1.25004 | **1.24967** |
| pretty-printed JSON | **0.89239** | 0.93533 |
| minified JSON | **1.00260** | **1.00260** |
| CommonMark specification | **0.85889** | 0.85907 |
| JavaScript source | **1.00282** | 1.00450 |
| CSS bundle | **1.00338** | **1.00338** |
| Python module | **0.96166** | 0.97256 |
| Markdown changelog | **0.97905** | 0.97914 |

Read that as two changes with opposite signs. The tail variant takes the
binary rows, and the ELF by 14 %: it is now smaller than Ascii85's 1.026, which
was the one row in this corpus where another codec was ahead. The threshold
change gives back 1 to 4 % on the text rows, which is what buys the decode
speed and the readability above.

For throughput, the comparison is in instructions executed rather than in
MB/s, for the reason `instructions/run.sh` documents. 200 kB per row for the
synthetic inputs, 400 kB for the two files, C implementation both sides:

| input | phase | v0.4.0 | v0.5.0 | ratio |
|---|---|---|---|---|
| random | encode | 2,478,627 | **2,403,095** | 0.97 |
| random | decode | **2,350,647** | **2,350,647** | 1.00 |
| text | encode | 6,227,490 | **6,222,323** | 1.00 |
| text | decode | **1,806,733** | 1,811,025 | 1.00 |
| mixed | encode | 5,816,658 | **5,742,351** | 0.99 |
| mixed | decode | **2,547,109** | 2,557,634 | 1.00 |
| `_cffi_backend.so` | encode | **6,531,636** | 9,141,035 | 1.40 |
| `_cffi_backend.so` | decode | 4,539,814 | **4,488,451** | 0.99 |
| `requests-2.32.3.tar` | encode | 10,549,972 | **10,238,190** | 0.97 |
| `requests-2.32.3.tar` | decode | 2,774,724 | **2,676,529** | 0.96 |

Fewer instructions is better, so the marked column is the cheaper one; the
ratio is v0.5.0 over v0.4.0.

The decoder is unchanged: one comparison on a path only signals reach, then a
`memset` and two stores. The encoder is unchanged too *except* where the new
signal fires — 60,000 times on the ELF, which costs 40 % more instructions and
buys 14 % smaller output. On random input, where the signal can never fire, the
encoder is 3 % *faster* than it was: the lookahead's zero test is gated on a
single load, and the loop it sits in no longer re-checks its bounds at every
position.

Both columns predate the lookahead's wider gate, and deliberately: that change
makes the encoder execute slightly *more* instructions and run up to 195 %
faster, so counting instructions measures it backwards. It is a branch
prediction result and it belongs with the timings, in
[binary-flag.md](binary-flag.md), which reports both and says where they
disagree.

---

## Where the alternatives are the better choice

The benchmark is equally explicit about this:

- **Base64url in a URL.** Percent-encoding charges three characters for every
  byte outside RFC 3986's unreserved set, and five of Alphabet-N's punctuation
  characters are in that penalty box. Over the corpus Base85N costs 1.463 in a
  query string against Base64's 1.354. This is the one embedding measured here
  where Base85N is not the smallest, and it is a design consequence, not an
  accident.
- **Raw encode speed against Base64.** Base64 encodes 2–4× faster than any
  Base85 measured here, Base85N included. Against the other two Base85s
  Base85N now wins the structured-text rows and trails on the zero-padded ELF
  and the WebAssembly module, where a signal is emitted every few bytes. If
  encode throughput against Base64 is the binding constraint and size is not,
  Base64 is the answer; several cores (above) close part of the gap.
- **Z85 for addressable data.** Its fixed 4→5 mapping means a byte offset
  converts to a character offset by arithmetic, so random access and seeking
  are trivial. Base85N's output length is data-dependent, so none of that is
  possible — the parallel encoder gets around it for encoding only, and by
  speculation rather than by arithmetic.
- **Maturity.** Ascii85 is in PDF and PostScript, Z85 is a ZeroMQ standard,
  RFC 1924 Base85 ships in Python's standard library. Base85N is a 0.x draft
  whose wire format has changed in every version so far, including this one.

---

## What benchmarking changed

Each of these was found by measuring, not by reading:

**The tail variant's length field.** The draft that proposed it encoded the
length in steps of four, so that every signal consumed a whole number of
4-byte groups. Measured, that was worth 0.9 % of the corpus. Encoding the
length directly — the same 22 bits, one field laid out differently — was worth
2.7 %, because a step-of-four length leaves 0 to 3 zeros behind for block mode
on every run it fires on. The construct was nearly discarded on the strength of
the first number.

**`MIN_FILL_IN_SEGMENT_BYTES`, on four axes instead of one.** 11 is the ratio
optimum and 0.4.0 chose it there. Ratio is flat from 13 to 16, and at 16 the
corpus carries 370,000 more bytes inside readable passthrough segments, the
decoder builds 4,100 fewer substitution tables, and `countries.json` — which
was last of four codecs on decode throughput by a factor of two — comes back
into the field. That cost 1 % of ratio on text.

**Convergence distance is not a few hundred bytes.** The parallel encoder was
designed on the assumption that block mode resynchronises within four bytes.
It does not: two encoders in a block-mode run stay out of phase until a
construct realigns them, and the 95th percentile of the distance is 50 KB.
The chunk minimum is a megabyte because of that measurement, not by taste.

**Bounding a lookahead by truncating its input is wrong.** Bounding the
parallel workers' skip by handing `next_decision_point` a shortened slice made
its own tests fail near the bound, so a worker could skip *over* a decision
point rather than merely stop early at one — a real divergence from the
sequential output, found by the test that compares them. The bound now limits
which positions are tested, and the tests still read the whole input.

**Fill's threshold inside a segment (v0.4.0).** Spec section 6.5 says a run of
`MIN_FILL_BYTES` is a Fill segment. Applied literally inside passthrough text
that is a loss: breaking a DP segment for a run costs the Fill signal *and* the
signal that resumes passthrough, so a run of 5 spends 10 characters to save 5.

**The decoder's output buffer (v0.4.0).** Fill is the first construct whose
output is not bounded by its input, so the decoder has to be able to grow its
buffer. The obvious version — pass the buffer by pointer and check on every
group — cost 27 % on binary decode, because the pointer could no longer stay in
a register. Keeping a local copy and refreshing it only where the buffer
actually moves brought that back.
