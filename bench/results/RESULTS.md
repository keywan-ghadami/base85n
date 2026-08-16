# Base85N benchmark results

Base85N against Base64, Ascii85 (Adobe/btoa), Z85 (ZeroMQ RFC 32) and
RFC 1924 Base85, on encoded size and throughput. How it is measured and how
to reproduce it: [../README.md](../README.md).

Measured against specification v0.5.0. Size and throughput both measured on an
Intel Xeon, Ubuntu 24.04, gcc 13.3.0 `-O2`, 2026-08-16, over a 6.52 MB corpus
of 13 real files. Size does not depend on the machine; throughput does, which
is why each table carries the three other codecs measured beside Base85N on the
same silicon, and why the version-over-version comparison further down uses
instruction counts instead.

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

**Speed is the cost, and it is the encoder's.** Base85N is the slowest encoder
of the four on 14 of the 16 inputs: Base64 encodes 3–6× faster. Decoding is
competitive, fastest of the four on six rows. The answer to the encoder is
cores rather than micro-optimisation — see *Encoding on several cores* below —
but on one core it is what it is.

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
| synthetic random 1 MiB | **1221** | 399 | 375 | 355 | -71 % | -11 % |
| synthetic text 1 MiB | **1239** | 405 | 380 | 307 | -75 % | -24 % |
| scan-heavy 1MiB | **1248** | 401 | 382 | 431 | -65 % | +7 % |
| DejaVuSans.ttf | **1248** | 409 | 383 | 370 | -70 % | -9 % |
| _cffi_backend.so | **1243** | 470 | 369 | 250 | -80 % | -47 % |
| bootstrap.css | **1223** | 401 | 378 | 287 | -77 % | -28 % |
| commonmark-spec.txt | **1226** | 405 | 383 | 206 | -83 % | -49 % |
| countries.json | **1211** | 401 | 375 | 275 | -77 % | -31 % |
| countries.min.json | **1233** | 401 | 377 | 332 | -73 % | -17 % |
| grace_hopper.jpg | **1254** | 404 | 385 | 347 | -72 % | -14 % |
| lodash.js | **1238** | 405 | 379 | 227 | -82 % | -44 % |
| minduka_present.png | **1250** | 406 | 383 | 458 | -63 % | +13 % |
| requests-2.32.3.tar | **1244** | 501 | 381 | 275 | -78 % | -45 % |
| requests-history.md | **1265** | 406 | 385 | 243 | -81 % | -40 % |
| requests-models.py | **1262** | 409 | 384 | 214 | -83 % | -48 % |
| sql-wasm.wasm | **1227** | 398 | 367 | 309 | -75 % | -22 % |

**Bold** marks the fastest codec in that row. The two delta columns are how much faster Base85N is than that codec -- **positive is faster**, negative means Base85N is slower.

### Decode throughput (MB/s of original bytes)

| input | Base64 | Ascii85 | Z85 | Base85N | vs Base64 | vs best other Base85 |
|---|---|---|---|---|---|---|
| synthetic random 1 MiB | **1191** | 858 | 1040 | 916 | -23 % | -12 % |
| synthetic text 1 MiB | 1207 | 864 | 1045 | **1508** | +25 % | +44 % |
| scan-heavy 1MiB | **1196** | 868 | 1049 | 924 | -23 % | -12 % |
| DejaVuSans.ttf | **1214** | 878 | 1053 | 897 | -26 % | -15 % |
| _cffi_backend.so | **1181** | 893 | 1012 | 712 | -40 % | -30 % |
| bootstrap.css | 1187 | 855 | 1034 | **1503** | +27 % | +45 % |
| commonmark-spec.txt | **1192** | 880 | 1029 | 1139 | -4 % | +11 % |
| countries.json | **1179** | 857 | 1033 | 912 | -23 % | -12 % |
| countries.min.json | 1188 | 859 | 1034 | **1509** | +27 % | +46 % |
| grace_hopper.jpg | **1198** | 888 | 1087 | 912 | -24 % | -16 % |
| lodash.js | 1213 | 867 | 1044 | **1399** | +15 % | +34 % |
| minduka_present.png | **1218** | 873 | 1070 | 904 | -26 % | -15 % |
| requests-2.32.3.tar | 1201 | 1073 | 1050 | **1615** | +35 % | +51 % |
| requests-history.md | 1212 | 879 | 1076 | **1330** | +10 % | +24 % |
| requests-models.py | 1223 | 881 | 1061 | **1287** | +5 % | +21 % |
| sql-wasm.wasm | **1183** | 860 | 1008 | 884 | -25 % | -12 % |

**Bold** marks the fastest codec in that row. The two delta columns are how much faster Base85N is than that codec -- **positive is faster**, negative means Base85N is slower.


**Encoding** is where Base85N pays for what it saves. Per 4-byte group it runs
a Fill scan, a prefix scan that tracks eight donor profiles at once, and — when
a segment is taken — a per-segment substitution table build. Base64 does a
6→8-bit repack with no division at all, and the other two Base85s do one
division chain per group and nothing else.

**Decoding** is close to the field: a block group is the same 5→4 conversion
every Base85 does, a DP segment is one table lookup per character, and a Fill
signal is a `memset`. Pretty-printed JSON used to be the outlier here, at half
the field's speed, because the decoder rebuilt a substitution table for every
short segment; raising the in-segment Fill threshold to 16 (spec section 14.3)
gave it 18 % fewer segments to build and it is now within the spread.

The scan-heavy row is a shape built to defeat the mode decision: 18
representable bytes then one that no alphabet can carry, repeated, so a
candidate never reaches MIN_PASSTHROUGH_BYTES and the encoder scans and falls
back on every group. It is in the corpus because it is the worst case for
prefix identification, not because it resembles real input.

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
| minified JSON | 1.00260 | 1.00260 |
| CommonMark specification | **0.85889** | 0.85907 |
| JavaScript source | **1.00282** | 1.00450 |
| CSS bundle | 1.00338 | 1.00338 |
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
| random | encode | 2,478,627 | 2,403,095 | **0.97** |
| random | decode | 2,350,647 | 2,350,647 | 1.00 |
| text | encode | 6,227,490 | 6,222,323 | 1.00 |
| text | decode | 1,806,733 | 1,811,025 | 1.00 |
| mixed | encode | 5,816,658 | 5,742,351 | 0.99 |
| mixed | decode | 2,547,109 | 2,557,634 | 1.00 |
| `_cffi_backend.so` | encode | 6,531,636 | 9,141,035 | **1.40** |
| `_cffi_backend.so` | decode | 4,539,814 | 4,488,451 | 0.99 |
| `requests-2.32.3.tar` | encode | 10,549,972 | 10,238,190 | 0.97 |
| `requests-2.32.3.tar` | decode | 2,774,724 | 2,676,529 | 0.96 |

The decoder is unchanged: one comparison on a path only signals reach, then a
`memset` and two stores. The encoder is unchanged too *except* where the new
signal fires — 60,000 times on the ELF, which costs 40 % more instructions and
buys 14 % smaller output. On random input, where the signal can never fire, the
encoder is 3 % *faster* than it was: the lookahead's zero test is gated on a
single load, and the loop it sits in no longer re-checks its bounds at every
position.

---

## Where the alternatives are the better choice

The benchmark is equally explicit about this:

- **Base64url in a URL.** Percent-encoding charges three characters for every
  byte outside RFC 3986's unreserved set, and five of Alphabet-N's punctuation
  characters are in that penalty box. Over the corpus Base85N costs 1.463 in a
  query string against Base64's 1.354. This is the one embedding measured here
  where Base85N is not the smallest, and it is a design consequence, not an
  accident.
- **Speed on one core.** Base85N is the slowest encoder of the four on 14 of
  the 16 inputs. Several cores close that (above), but a single-threaded,
  CPU-bound encoder has nothing to gain here.
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
