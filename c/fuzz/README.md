# Fuzzing

Three libFuzzer targets, all built with AddressSanitizer and
UndefinedBehaviorSanitizer, because a fuzzer without them finds only the
bugs that happen to crash and the ones worth finding here do not.

| target | what it asserts |
|---|---|
| `fuzz_roundtrip` | encoding never fails on input content, its output is Alphabet-N only, and decoding it returns the input bytes |
| `fuzz_decode` | the decoder terminates on arbitrary input, respects the format's ~410:1 expansion bound, and reaches a fixed point on anything it accepts |
| `fuzz_differential` | the C and Rust implementations agree, character for character on encoding and status-code for status-code on decoding |

```sh
make          # build the two single-implementation targets
make run      # both, SECS=60 each (SECS=... to change)
make differential ; make run-differential   # needs cargo
make corpus   # seeds only
```

`make run SECS=120` is what CI runs. It is a regression check, not a
campaign: two minutes against a seeded corpus finds a broken property, not
a new bug. A campaign is the same binary with a large `-max_total_time` or
none, and a corpus directory that is kept.

## The differential target

The Rust crate exports the same C ABI as the C implementation — same
function names, same status codes, same ownership rules — which is what
makes an in-process comparison possible. It is also why the two cannot be
linked as they are, so the build renames the Rust symbols to `rs_*` with
`objcopy` first.

This target is the one that pays. The format admits exactly one encoding
of any input, and until it existed that was checked only against the
shared test vectors and a generated differential corpus — both fixed sets,
both written by someone who had to think of the case first. Its first
runs found:

- a lone trailing character outside Alphabet-N reported as
  `INVALID_FINAL_BLOCK` by C and Go and as `INVALID_CHARACTER` by
  TypeScript — with Rust doing *both*, depending on whether the byte was
  ASCII;
- the Rust C ABI reading its input as UTF-8, which rejected a stray byte
  before the end-of-stream check the specification orders ahead of it, and
  which counted one character where a well-formed multi-byte sequence is
  two, three or four bytes to every other implementation.

Both are fixed, and both are now pinned as `error_precedence` vectors in
`testvectors/`. See `spec/history/lessons.md`.

## Seeds

`seed_corpus.py` writes the shared test vectors, the adversarial vectors
and a set of generated shapes that sit on the thresholds the encoder
branches on. A fuzzer that starts from nothing spends its first hours
rediscovering that input is bytes.

## What is not covered

Go and TypeScript are not in the differential target: neither links into a
libFuzzer process without more machinery than the result would justify.
They are covered by the shared vectors and by
`tools/gen_differential_cases.py`. A corpus the fuzzer has grown can be
replayed through those runners, which is the cheap way to extend a finding
to all four.

There is no OSS-Fuzz integration and no continuous campaign; see
`SECURITY.md`.
