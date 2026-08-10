### Corpus files — expansion ratio (encoded chars per input byte)

| sample | input | Base64 | Ascii85 | Z85 | Base85 (RFC 1924) | Base85N | Base85N vs Base64 |
|---|---|---|---|---|---|---|---|
| sql-wasm.wasm | 659,730 B | 1.333 | 1.247 | n/a | 1.250 | 1.246 | +6.5 % |
| _cffi_backend.so | 1,068,624 B | 1.333 | 1.026 | 1.250 | 1.250 | 1.246 | +6.5 % |
| DejaVuSans.ttf | 756,072 B | 1.333 | 1.240 | 1.250 | 1.250 | 1.248 | +6.4 % |
| countries.json | 1,408,911 B | 1.333 | 1.250 | n/a | 1.250 | 1.033 | +22.6 % |
| countries.min.json | 772,294 B | 1.333 | 1.250 | n/a | 1.250 | 1.053 | +21.0 % |
| commonmark-spec.txt | 202,827 B | 1.333 | 1.250 | n/a | 1.250 | 1.123 | +15.8 % |
| grace_hopper.jpg | 61,306 B | 1.333 | 1.250 | n/a | 1.250 | 1.250 | +6.3 % |
| minduka_present.png | 13,634 B | 1.333 | 1.250 | n/a | 1.250 | 1.250 | +6.3 % |

### Short protocol fields — encoded characters

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

### Corpus totals

| codec | total encoded | ratio | vs Base64 |
|---|---|---|---|
| Base64 | 6,591,204 chars | 1.3333 | +0.00 % |
| Ascii85 | 5,930,050 chars | 1.1996 | +10.03 % |
| Z85 | (not applicable to 6 sample(s)) | - | - |
| Base85 (RFC 1924) | 6,179,250 chars | 1.2500 | +6.25 % |
| Base85N | 5,686,506 chars | 1.1503 | +13.73 % |

Total input: 4,943,398 bytes across 8 files.
