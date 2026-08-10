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

### Short protocol fields — encoded characters

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

### Cost of carrying the output inside JSON and XML

Expansion ratio over the whole corpus once the encoded text is placed
in a JSON string literal or in XML character data, i.e. what the
alphabet actually costs in the contexts encoded payloads travel in.

| codec | raw | vs Base85N | inside JSON | vs Base85N | inside XML | vs Base85N |
|---|---|---|---|---|---|---|
| Base64 | 1.3333 | +15.9 % | 1.3333 | +15.9 % | 1.3333 | +15.9 % |
| Ascii85 | 1.1996 | +4.3 % | 1.2283 | +6.8 % | 1.4171 | +23.2 % |
| Z85 | 1.2500 | +8.7 % | 1.2500 | +8.7 % | 1.3662 | +18.8 % |
| Base85 (RFC 1924) | 1.2500 | +8.7 % | 1.2500 | +8.7 % | 1.3530 | +17.6 % |
| Base85N | 1.1503 | — | 1.1503 | — | 1.1503 | — |

"vs Base85N" is how much larger that codec's output is than Base85N's for the same corpus, in that context.

### Corpus totals

| codec | total encoded | ratio | vs Base64 |
|---|---|---|---|
| Base64 | 6,591,204 chars | 1.3333 | +0.00 % |
| Ascii85 | 5,930,050 chars | 1.1996 | +10.03 % |
| Z85 | 6,179,260 chars | 1.2500 | +6.25 % |
| Base85 (RFC 1924) | 6,179,250 chars | 1.2500 | +6.25 % |
| Base85N | 5,686,506 chars | 1.1503 | +13.73 % |

Total input: 4,943,398 bytes across 8 files.
