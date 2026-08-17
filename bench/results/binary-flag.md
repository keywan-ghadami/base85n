# Would a `--binary` encoder flag pay for itself?

A proposed `--binary` encoder mode would tell the encoder that its input is
binary, so it could skip the Dynamic Passthrough machinery that binary input
mostly does not use. The rule set for it was:

> `--binary` stays an official encoder option only if it delivers a
> significant encode speedup on representative binary data. Significant
> means 4 %.

**The flag was declined, and it has since stopped clearing its own bar on
the inputs it was aimed at.** When first measured it delivered +31 % to
+71 % on every binary input. Two rounds of making the *conforming* encoder
cheaper — both found by this benchmark, neither changing an output
character — have since taken that to **−9 % on random bytes, −12 % on JPEG
and −15 % on PNG**: on the purest binary there is, the encoder that obeys the
specification is now faster than the dialect that skips a step of it. What
is left of the flag's advantage is on files where Dynamic Passthrough does
real work, and there it is paid for in output size.

Reproduce with `make -C bench/speed binary-flag`. Measured on 2026-08-17,
Intel Xeon @ 2.80 GHz (KVM, 4 cores), Ubuntu 24.04, gcc 13.3.0 `-O2`, over
the corpus in [../README.md](../README.md).

---

## Summary

**1. None of the flag's case was about the flag.** Against the encoder as it
stood when the flag was proposed, `--binary` scored +40 % to +314 % on
binary. All of that was the conforming encoder leaving work on the floor, in
the lookahead that carries every high-entropy encode, and it came back in two
steps that changed no output character:

| | random | JPEG | PNG | WASM | TTF | ELF | tar |
|---|---|---|---|---|---|---|---|
| `narrow-gate`, as proposed against | 329 | 352 | 664 | 308 | 354 | 320 | 484 |
| `word-gate`, after the gate widened | 839 | 842 | 819 | 585 | 538 | 362 | 487 |
| `default`, after the window and scan gates | **1263** | **1291** | **1270** | **642** | **590** | **386** | **491** |
| `--binary`, unchanged throughout | 1143 | 1133 | 1083 | 798 | 807 | 472 | 806 |

MiB/s. The first step was one mispredicted branch: the lookahead asked
whether a passthrough segment might start here by reading one byte from a
table, which on high-entropy input is a coin flip resolved once per four
bytes of the file. The second settles two 4-byte groups per word instead of
one, and applies the same idea to the passthrough scan in the main loop.
Three of the seven rows have now passed `--binary` outright.

**2. `--binary` is not a second API for the same functionality — it is a
second dialect.** Specification 6.5 rule 2 requires the longest DP prefix at
every decision point the loop reaches, and 11.3 states there is "no second
canonical form and nothing to configure". An encoder that skips DP emits a
stream that decodes correctly but that no conforming encoder would produce.
The four implementations in this repository are held byte-identical by the
shared test vectors; a mode that opts out of a mandatory step is a second
answer to "what is the encoding of these bytes", which the format does not
have.

**3. It costs output size on exactly the files it targets.** On most binary
the size cost is nil, because DP was finding nothing anyway — 0.0 % on JPEG,
PNG and random bytes, 0.2–0.4 % on TrueType, ELF and WebAssembly. But an
uncompressed tar pays **+17.7 %**, because a container full of ASCII path
headers is binary by file type and text by content, and that is the normal
shape of a binary container. Pointed at text — which is what a flag named
`--binary` invites when someone guesses wrong — the cost is +5 % to +25 %.

Where the speed is still real it is bought with non-canonical output and,
on the inputs where DP was earning its keep, with size. The same measurement
found +284 % available on random binary with none of that, which is how the
flag came to be behind on three rows. The rest is reachable the same way:
make the conforming encoder cheaper. `binary-nofill` at 2 762–2 873 MiB/s
says how much is still there.

---

## What was measured

The proposal names two things a binary mode would do differently, and a
single `default` vs `--binary` comparison confounds them. So the encoder's
two optional steps were varied independently:

| variant | DP (steps 2–3) | Fill + zero run (step 1) | conforming |
|---|---|---|---|
| `default` | yes | yes | yes — this is the shipped encoder |
| `binary` | no | yes | no |
| `default-nofill` | yes | no | no |
| `binary-nofill` | no | no | no |
| `narrow-gate` | yes | yes | yes — the encoder as it was when the flag was proposed |
| `gate4-only` | yes | yes | yes — the first fix, half of it |
| `word-gate` | yes | yes | yes — after the first fix, before the second |

This covers the four rows the proposal asked for. Its `default` and
`--binary` are the first two; `default + zero run internally` is what the
shipped `default` already is, since Fill and the zero run are not optional
in a conforming encoder; and `--binary without zero run / Fill` is
`binary-nofill`. The two extra rows are the attribution the four could not
give on their own.

Only the rows marked conforming are Base85N encoders. The other three are
dialects built to be measured: their output decodes, because the decoder
accepts every construct wherever it appears, but the specification admits
exactly one encoding of any input and these are not it. Each is still round
tripped before it is timed, and each conforming row is additionally checked
character-for-character against `base85n_encode()` — over the corpus, and
over 30 000 generated cases shaped like the boundaries the encoder's
branches turn on (`make -C bench/speed binary-flag-selftest`).

### How a 4 % threshold is measured on a virtual machine

The run-to-run spread on a shared machine is the same order as some of the
differences here, so the harness does two things about it. Rounds are
**interleaved** — every variant is timed once, then all of them again, 15
times — so drift over the run is spread across all variants rather than
charged to whichever went first. And every comparison is **paired**: the
ratio to `default` is formed within each round, from timings taken
milliseconds apart, and the reported figure is the median of the 15
per-round ratios. A machine that is 10 % slower for one round makes both
halves of that round's ratio 10 % slower and leaves the ratio alone. The
range the 15 rounds spanned is printed beside every figure; a verdict is
only claimed when that whole range sits on one side of the threshold.

Instruction counts under callgrind are the control, since they do not depend
on the machine at all (`bench/speed/binary_flag_instructions.sh`). For the
flag they agree closely: on WebAssembly `binary` executes 1.50× fewer
instructions where the timings say +48 %, on the tar 1.71× against +71 %,
and on ELF 1.22× against +31 %. `--binary`'s speed is less work, and both
measurements put it at the same size.

| input | `default` | `binary` | `default-nofill` | `binary-nofill` | `narrow-gate` |
|---|---|---|---|---|---|
| sql-wasm.wasm | 9 699 693 | 6 486 258 | 8 401 795 | 3 546 877 | 9 430 195 |
| requests-2.32.3.tar | 10 998 222 | 6 438 243 | 12 318 745 | 3 523 342 | 10 999 725 |
| _cffi_backend.so | 23 944 887 | 19 656 379 | 13 845 155 | 5 744 622 | 23 631 719 |

The one place the two measurements *disagree* is the gate fix, and the
disagreement is the finding. `narrow-gate` executes **3 % fewer**
instructions than `default` on WebAssembly and 1 % fewer on ELF, while
running 96 % and 11 % slower. Fewer instructions and much less throughput is
the signature of a branch nothing can predict, which is exactly what the
narrow gate was; see below.

---

## Binary: the inputs the flag is for

Encode throughput in MiB/s of input, median of 15 interleaved rounds.

| input | `default` | `--binary` | `word-gate` | `narrow-gate` | `binary-nofill` |
|---|---|---|---|---|---|
| synthetic random 1 MiB | 1263 | 1143 | 839 | 329 | 2809 |
| grace_hopper.jpg | 1291 | 1133 | 842 | 352 | 2873 |
| minduka_present.png | 1270 | 1083 | 819 | 664 | 2832 |
| sql-wasm.wasm | 642 | 798 | 585 | 308 | 2779 |
| DejaVuSans.ttf | 590 | 807 | 538 | 354 | 2762 |
| _cffi_backend.so | 386 | 472 | 362 | 320 | 2819 |
| requests-2.32.3.tar | 491 | 806 | 487 | 484 | 2837 |

And the decision, against the shipped encoder:

| input | `--binary` vs `default` | round range | verdict | size cost | `default` chars/B | `--binary` chars/B |
|---|---|---|---|---|---|---|
| synthetic random 1 MiB | −8.9 % | −11.7 … −2.4 % | **FAIL** | +0.0 % | 1.250 | 1.250 |
| grace_hopper.jpg | −12.0 % | −16.6 … −9.6 % | **FAIL** | +0.0 % | 1.249 | 1.249 |
| minduka_present.png | −14.8 % | −16.7 … +4.5 % | inconclusive | +0.0 % | 1.250 | 1.250 |
| sql-wasm.wasm | +24.5 % | +20.9 … +36.0 % | PASS | +0.3 % | 1.239 | 1.243 |
| DejaVuSans.ttf | +37.2 % | +34.8 … +40.1 % | PASS | +0.2 % | 1.232 | 1.234 |
| _cffi_backend.so | +21.6 % | +18.7 … +28.2 % | PASS | +0.4 % | 0.965 | 0.969 |
| requests-2.32.3.tar | +64.2 % | +57.1 … +99.1 % | PASS | **+17.7 %** | 0.767 | 0.903 |

**`binary-nofill` is the row that says where the speed comes from.** With
both optional steps gone the encoder is one block-mode run over the whole
input and reaches 2 808–2 898 MiB/s on every binary input, whatever its
shape. So neither optional step is doing arithmetic that binary data needs —
the entire cost of both is *looking* for constructs that are not there. That
is also why `--binary` and `default` produce the same 1.250 chars/B on JPEG,
PNG and random bytes while differing 2.5-fold in speed: the flag is not
choosing differently, it is declining to look.

`default-nofill` is the mirror image and it separates the two steps
cleanly. On WebAssembly and ELF, dropping Fill is worth more than dropping
DP (1110 and 1074 MiB/s against `--binary`'s 1053 and 500), because those
files are full of zero padding and the zero-run scan keeps finding real
work. On the tar it is worth less than nothing (507 against 530). A flag
that bundles "skip DP" with "keep Fill" is therefore not even the right
bundle for the files it names: on ELF, most of what a binary mode could
save is in the step the proposal keeps.

## Text: what the flag costs when it is pointed at the wrong thing

| input | `--binary` vs `default` | size cost | `default` chars/B | `--binary` chars/B |
|---|---|---|---|---|
| synthetic text 1 MiB | +128.4 % | +24.7 % | 1.002 | 1.250 |
| bootstrap.css | +145.7 % | +24.5 % | 1.003 | 1.249 |
| commonmark-spec.txt | +168.1 % | +20.5 % | 0.859 | 1.035 |
| countries.json | +62.2 % | +4.7 % | 0.935 | 0.979 |
| countries.min.json | +147.7 % | +24.7 % | 1.003 | 1.250 |
| lodash.js | +81.4 % | +21.5 % | 1.004 | 1.220 |
| requests-history.md | +195.3 % | +23.3 % | 0.979 | 1.208 |
| requests-models.py | +102.1 % | +15.2 % | 0.973 | 1.121 |

The flag is fastest exactly where it is most wrong. `--binary` is nearly
three times faster on a Markdown changelog than the conforming encoder, and
gives back 23 % of the output size to get there — it is throwing away the
whole feature the format exists for. Nothing in a flag stops a caller from
reaching for it because their data is "mostly binary".

---

## The finding the benchmark was built to produce

The attribution rows exist to answer "where does the speedup come from", and
the answer was not what the proposal assumed. It is not the Dynamic
Passthrough scan. On random binary the scan runs about twice for a whole
megabyte, because the lookahead of specification 11.1 skips over everything
between decision points in one pass.

The cost was in that lookahead. It tested one byte against the
`REPRESENTABLE` table to decide whether a DP segment might start, once per
four bytes of input, over the entire file. About a third of byte values are
representable, so on high-entropy input that test is a coin flip — a branch
nothing can predict, resolved 262 144 times per megabyte.

A DP segment needs `MIN_PASSTHROUGH_BYTES` representable bytes in a row, so
asking for more than one up front turns away no position the one-byte test
would have accepted. Asking for eight, as one word compared against a range
rather than eight table lookups, is taken about one time in five hundred on
binary and always on text:

| input | before | after | | | input | before | after | |
|---|---|---|---|---|---|---|---|---|
| synthetic random 1 MiB | 388 | **1145** | +195 % | | commonmark-spec.txt | 436 | 437 | +0 % |
| grace_hopper.jpg | 495 | **1176** | +137 % | | bootstrap.css | 531 | 530 | −0 % |
| sql-wasm.wasm | 363 | **711** | +96 % | | countries.json | 459 | 464 | +1 % |
| DejaVuSans.ttf | 423 | **653** | +55 % | | lodash.js | 406 | 405 | −0 % |
| scan-heavy 1 MiB | 581 | 647 | +11 % | | requests-2.32.3.tar | 529 | 530 | +0 % |
| _cffi_backend.so | 344 | 380 | +11 % | | requests-models.py | 552 | 559 | +1 % |
| minduka_present.png | 1116 | 1150 | +3 % | | synthetic text 1 MiB | 733 | 733 | −0 % |

Every character of output is unchanged — this is the same encoder finding
the same decision points by a cheaper route, which is what specification
11.1 licenses a skip to do. Text is untouched, because on text the gate
clears immediately either way. The one input that could have been hurt is
`scan-heavy`, the adversarial buffer that starts a DP candidate every 19
bytes and never completes one; it gains 11 %.

The instruction counts confirm the diagnosis by contradicting the timings:
the *wider* gate executes 3 % **more** instructions on WebAssembly and wins
96 % of throughput. It is not doing less work. It is doing slightly more
work in a way the branch predictor can follow, which is why this is the one
number in the repository that instruction counting gets backwards — as
`bench/README.md` warns it will, since callgrind charges a mispredicted
branch and a predicted one the same.

`gate4-only` — the four-byte version of the same gate, from the table
instead of a word comparison — gets most of it (988 on random binary, 665
on WebAssembly) and is kept in the harness because the gap between the two
is the part that is specifically about the word comparison rather than
about asking for more than one byte.

Both changes are shipped. It is why the `default` column above is neither
the `narrow-gate` nor the `word-gate` column.

---

## Verdict

Against the rule as written, at the time it was applied: **PASS**, on all
seven binary inputs. Against the same rule today, after two rounds of making
the conforming encoder cheaper: **FAIL on the three inputs with no
passthrough in them at all**, and PASS only where the flag is buying its
speed by declining to encode as well as it could.

Against the question the rule was standing in for — is `--binary` worth a
permanent second encoder — the recommendation is **no**:

- It emits non-conforming output. This is not a technicality that could be
  waived in the specification: 11.3's parallel encoder, the shared test
  vectors, and the byte-identity of the four implementations all rest on
  there being one encoding per input.
- On a binary container with text in it, which is most of them, it trades
  size for speed rather than getting speed for free — 17.7 % on the tar.
- It is not the right bundle even on its own terms: on ELF and WebAssembly,
  `default-nofill` beats it, so most of the available saving is in the step
  the proposal keeps.
- Two thirds of its measured advantage was a fixable branch, and that fix
  cost no output, no API and no flag.

If the remaining 31–71 % is wanted, the precedent is the gate fix: the
lookahead is still spending two fill tests and a word comparison per four
bytes on data that will turn out to be one long block-mode run, and
`binary-nofill`'s 2 808–2 898 MiB/s says what is still on the table. That is
a conforming encoder getting faster, which needs no decision rule at all.
