### Corpus files — expansion ratio (encoded chars per input byte)

| sample | input | Base64 | Ascii85 | Z85 | Base85 (RFC 1924) | Base85N | vs Base64 | vs best other Base85 |
|---|---|---|---|---|---|---|---|---|
| sql-wasm.wasm | 659,730 B | 1.333 | **1.247** | 1.250 | 1.250 | 1.247 | -6.4 % | same |
| _cffi_backend.so | 1,068,624 B | 1.333 | **1.026** | 1.250 | 1.250 | 1.246 | -6.5 % | +21.5 % |
| DejaVuSans.ttf | 756,072 B | 1.333 | **1.240** | 1.250 | 1.250 | 1.248 | -6.4 % | +0.7 % |
| countries.json | 1,408,911 B | 1.333 | 1.250 | 1.250 | 1.250 | **1.005** | -24.6 % | -19.6 % |
| countries.min.json | 772,294 B | 1.333 | 1.250 | 1.250 | 1.250 | **1.005** | -24.6 % | -19.6 % |
| commonmark-spec.txt | 202,827 B | 1.333 | 1.250 | 1.250 | 1.250 | **1.020** | -23.5 % | -18.4 % |
| grace_hopper.jpg | 61,306 B | 1.333 | 1.250 | 1.250 | 1.250 | **1.250** | -6.3 % | same |
| minduka_present.png | 13,634 B | 1.333 | 1.250 | 1.250 | 1.250 | 1.250 | -6.3 % | same |

**Bold** marks the smallest output in that row; no bold means a tie. The two delta columns are Base85N's size difference — **negative is a saving**, positive means Base85N is larger.

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
| JSON record | 92 B | 124 | 115 | 115 | 115 | **97** | -21.8 % | -15.7 % |
| HTTP header block | 114 B | 152 | 143 | 145 | 143 | **119** | -21.7 % | -16.8 % |
| JWT (3 segments) | 155 B | 208 | 194 | 195 | 194 | **160** | -23.1 % | -17.5 % |
| log line | 93 B | 124 | 117 | 120 | 117 | **98** | -21.0 % | -16.2 % |
| SQL statement | 118 B | 160 | 148 | 150 | 148 | **123** | -23.1 % | -16.9 % |

**Bold** marks the smallest output in that row; no bold means a tie. The two delta columns are Base85N's size difference — **negative is a saving**, positive means Base85N is larger.

### Cost of carrying the output inside JSON and XML

Expansion ratio over the whole corpus once the encoded text is placed
in a JSON string literal or in XML character data, i.e. what the
alphabet actually costs in the contexts encoded payloads travel in.

| codec | raw | larger than Base85N | inside JSON | larger | inside XML | larger |
|---|---|---|---|---|---|---|
| Base64 | 1.3333 | +17.9 % | 1.3333 | +17.9 % | 1.3333 | +17.9 % |
| Ascii85 | 1.1996 | +6.1 % | 1.2283 | +8.6 % | 1.4171 | +25.3 % |
| Z85 | 1.2500 | +10.5 % | 1.2500 | +10.5 % | 1.3662 | +20.8 % |
| Base85 (RFC 1924) | 1.2500 | +10.5 % | 1.2500 | +10.5 % | 1.3530 | +19.6 % |
| Base85N | 1.1311 | — | 1.1311 | — | 1.1311 | — |

The "larger" columns are how much more that codec costs than Base85N for the same corpus, in that context.

### Corpus totals

| codec | total encoded | ratio | vs Base64 |
|---|---|---|---|
| Base64 | 6,591,204 chars | 1.3333 | +0.00 % |
| Ascii85 | 5,930,050 chars | 1.1996 | +10.03 % |
| Z85 | 6,179,260 chars | 1.2500 | +6.25 % |
| Base85 (RFC 1924) | 6,179,250 chars | 1.2500 | +6.25 % |
| Base85N | 5,591,669 chars | 1.1311 | +15.16 % |

Total input: 4,943,398 bytes across 8 files.
