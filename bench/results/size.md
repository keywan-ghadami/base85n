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

### Corpus totals

| codec | total encoded | ratio | vs Base64 |
|---|---|---|---|
| Base64 | 8,692,928 chars | 1.3333 | +0.00 % |
| Ascii85 | 7,746,174 chars | 1.1881 | +10.89 % |
| Z85 | 8,149,630 chars | 1.2500 | +6.25 % |
| Base85 (RFC 1924) | 8,149,614 chars | 1.2500 | +6.25 % |
| Base85N | 6,678,433 chars | 1.0243 | +23.17 % |

Total input: 6,519,688 bytes across 13 files.
