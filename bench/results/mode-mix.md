**The core corpus**

| sample | input | ratio | block | DP | Fill (solid) | Fill (tail) |
|---|---|---|---|---|---|---|
| sql-wasm.wasm | 659,730 B | 1.239 | 95.8 % | 3.1 % | 0.3 % | 0.8 % |
| _cffi_backend.so | 1,068,624 B | 0.965 | 54.2 % | 2.8 % | 0.9 % | 42.1 % |
| DejaVuSans.ttf | 756,072 B | 1.232 | 96.7 % | 1.0 % | 0.7 % | 1.6 % |
| requests-2.32.3.tar | 655,360 B | 0.767 | 2.2 % | 71.3 % | 24.8 % | 1.8 % |
| countries.json | 1,408,911 B | 0.935 | 1.1 % | 82.4 % | 16.5 % | 0.0 % |
| countries.min.json | 772,294 B | 1.003 | 0.0 % | 100.0 % | 0.0 % | 0.0 % |
| lodash.js | 544,098 B | 1.004 | 0.0 % | 99.6 % | 0.4 % | 0.0 % |
| bootstrap.css | 281,046 B | 1.003 | 0.0 % | 100.0 % | 0.0 % | 0.0 % |
| requests-models.py | 35,418 B | 0.973 | 1.0 % | 92.9 % | 6.2 % | 0.0 % |
| commonmark-spec.txt | 202,827 B | 0.859 | 1.4 % | 78.2 % | 20.4 % | 0.0 % |
| requests-history.md | 60,368 B | 0.979 | 0.1 % | 95.2 % | 4.7 % | 0.0 % |
| grace_hopper.jpg | 61,306 B | 1.249 | 99.8 % | 0.1 % | 0.1 % | 0.0 % |
| minduka_present.png | 13,634 B | 1.250 | 99.9 % | 0.0 % | 0.0 % | 0.1 % |

**The Silesia corpus**

| sample | input | ratio | block | DP | Fill (solid) | Fill (tail) |
|---|---|---|---|---|---|---|
| dickens | 10,192,446 B | 1.003 | 0.0 % | 100.0 % | 0.0 % | 0.0 % |
| mozilla | 51,220,480 B | 1.102 | 73.6 % | 10.3 % | 4.3 % | 11.9 % |
| mr | 9,970,564 B | 0.908 | 72.0 % | 0.0 % | 26.8 % | 1.2 % |
| nci | 33,553,445 B | 1.002 | 0.0 % | 100.0 % | 0.0 % | 0.0 % |
| ooffice | 6,152,192 B | 1.202 | 90.7 % | 1.4 % | 2.8 % | 5.1 % |
| osdb | 10,085,684 B | 1.143 | 35.7 % | 61.5 % | 0.6 % | 2.2 % |
| reymont | 6,627,202 B | 1.004 | 0.6 % | 99.3 % | 0.0 % | 0.0 % |
| samba | 21,606,400 B | 0.947 | 7.4 % | 83.8 % | 8.1 % | 0.6 % |
| sao | 7,251,944 B | 1.248 | 99.4 % | 0.0 % | 0.0 % | 0.6 % |
| webster | 41,458,703 B | 1.023 | 0.1 % | 99.9 % | 0.0 % | 0.0 % |
| x-ray | 8,474,240 B | 1.249 | 98.7 % | 1.3 % | 0.0 % | 0.0 % |
| xml | 5,345,280 B | 1.000 | 0.0 % | 99.6 % | 0.4 % | 0.0 % |

Percentages are the share of the **input bytes** each construct carried. Block mode spends 1.25 characters per byte, DP spends 1.0 plus a 5-character signal per segment, and either Fill variant spends 5 characters however many bytes it covers — which is the only way a row's ratio gets below 1.0.
