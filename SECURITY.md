# Security Policy

## Reporting a vulnerability

Report suspected security problems by email to **keywan.ghadami@gmail.com**.

Please include enough detail to reproduce: the affected implementation
(`rust/`, `go/`, `typescript/`, `c/`, `python/`), the commit or release, the
input that triggers the problem (hex or base64 is fine), and what you observed.

This is a small, unfunded project maintained by one person. There is no paid
bug bounty and no guaranteed response time. Expect a first reply within about
two weeks. Please do not open a public issue for a memory-safety or
input-validation problem until it has been fixed, or until you have concluded
that it will not be.

## Scope and threat model

Base85N is an **encoding** — it represents arbitrary data as text so that the
result can be embedded in a text-based format. It is not encryption, not a MAC,
not a checksum, and not obfuscation. Encoded output is fully reversible by anyone.
Nothing in this project provides confidentiality, integrity, or authenticity —
if you need those, apply them separately, and apply them to the *bytes*, not to
the encoded text.

The largest security-relevant surface is the **decoder**, because a decoder
processes data that your system did not produce. The **encoder** is a smaller
but real surface too, because encoders are routinely handed text the encoding
system did not author.

### ⚠️ Decoding data from untrusted sources

**Decoding attacker-controlled strings is the risky operation in this
project.** A Base85N decoder parses a length-prefixed format: a DP signal
declares how many characters follow, and a Fill signal declares how many bytes
to *produce* without reading any. Both lengths are attacker-controlled, and
both are classic sources of out-of-bounds reads, integer overflow,
over-allocation and infinite loops — particularly in the C implementation,
which does its own memory management.

If you decode input that came from a network peer, a file upload, a URL
parameter, a database field populated by users, or any other source you do not
control:

- Treat the whole operation as parsing hostile input.
- Bound the input size before decoding — but note that since v0.4.0 the
  decoders no longer allocate strictly in proportion to their input: a Fill
  signal expands five characters into up to 2048 bytes, so the bound to apply
  is the input size times about 410. That ratio is capped by the format
  (spec Sections 7.4 and 13); it is not unbounded, but it is not 1:1 either.
- Handle the error path. Every implementation reports the Section 10 error
  conditions explicitly (`DecodeError` / `error` / `Base85NDecodeError` /
  `base85n_status`); do not ignore it, and do not treat a failed decode as an
  empty result.
- Treat the *decoded output* as untrusted binary, not as text. It may contain
  NUL bytes, control characters, and invalid UTF-8. Escape it for whatever
  context it lands in (HTML, SQL, shell, filesystem paths).
- Prefer a memory-safe implementation (Rust, Go, TypeScript, or the Python
  bindings, which are the Rust one) over the C one when the input is untrusted
  and you have the choice. **From a language that is not one of those, that
  choice is still available** — see below.
- In C, remember that `base85n_encode`/`base85n_decode` hand you `malloc`'d
  buffers you must `free()`, and that the decoded buffer is **not**
  NUL-terminated.

### Recommended: bind the Rust build, not the C one

If your language is not one of the ones here, the usual route is to link the C
library through an FFI. **Link the Rust build instead.** The Rust crate exports
the same C ABI:

- Header: [`rust/include/base85n.h`](rust/include/base85n.h), declaring the same
  four functions with the same names and the same status-code *values* as
  [`c/include/base85n.h`](c/include/base85n.h).
- Artifacts: `cd rust && cargo build --release` produces `libbase85n.so`
  (`.dylib`/`.dll`) and `libbase85n.a`.
- Ownership: unchanged. Output buffers come from `malloc()` and are released by
  the caller with `free()`, so existing bindings do not need to learn a
  library-specific deallocator.

The two libraries are interchangeable at the ABI level, and that is tested
rather than asserted: `rust/capi/run.sh` compiles one C program against *both*
headers and links it against the Rust static library, and CI runs it on every
push.

What you get for the swap is where it counts. The decoder is the part that
parses data your system did not produce, and in the Rust build every entry
point below the four `extern "C"` functions is safe Rust: out-of-bounds reads,
pointer arithmetic overflow and use of an uninitialised length are not
reachable there for *any* input, rather than being absent as far as review and
sanitizers have looked. The C implementation remains supported, tested, and run
under ASan/UBSan in CI — but it is a hand-written parser, and this project has
had no independent security review (see below).

What it costs: nothing worth weighing. The Rust decoder is *faster* than the C
one on every input measured — half again as fast on high-entropy data — and its
encoder is at parity except on random bytes, where C is about 15 % ahead. The
numbers, and the harness that produces them, are in
[`rust/README.md` § How it compares to the C implementation](rust/README.md#how-it-compares-to-the-c-implementation).

Two differences to account for in a binding:

- A panic cannot unwind into your frames: an `extern "C"` function aborts the
  process instead. No panic is expected — encoding is total, and decoding
  reports its error conditions as status codes.
- An allocation failure inside the Rust codec aborts rather than returning
  `BASE85N_ERR_ALLOC`, because that is Rust's global policy for its allocator.
  `BASE85N_ERR_ALLOC` is still returned when the caller-owned output buffer
  cannot be allocated.

Where you have a native memory-safe implementation available — Go, Python,
TypeScript, or Rust itself — use that; it is simpler than any FFI. This section
is for the case where you would otherwise have reached for the C library.

### ⚠️ Where encoded output is *not* safe to paste unquoted

Alphabet-N excludes every character JSON, XML and HTML would force an escape for
(`"`, `'`, `\`, `<`, `>`, `&`, and all whitespace and control characters), which
is what lets encoded output go into those containers with no second escaping
layer. It does **not** exclude every character with syntactic meaning
everywhere — `` ` ``, `$`, `{`, `=`, `%`, `*`, `[` and `#` are all in the
alphabet, precisely because those three formats leave them alone.

Four containers are therefore unsafe for raw encoded output, and
[README § Embedding](README.md#embedding-where-the-output-can-be-pasted-verbatim)
tabulates all of them against real parsers:

- **A JavaScript template literal.** A backtick ends the literal and `${` starts
  an interpolation. Both occur in ordinary output — a backtick about one
  character in 85 — so this fails intermittently, which is the worst way for it
  to fail. Emit `'…'` or `"…"` instead; both are safe.
- **An unquoted HTML attribute.** HTML5 forbids `` ` `` and `=` there, and both
  are in the alphabet. Quote the attribute.
- **A plain (unquoted) YAML scalar.** Output can begin with `%`, `{`, `[`, `:`,
  `-`, `?`, `!`, `*` or `@`, all of which are YAML indicators. Quote the scalar.
- **A double-quoted or unquoted shell word.** `` ` `` and `$` substitute, and
  `*`, `?`, `~`, `{` glob. Single quotes are safe.

Two further cases are about the *consumer* rather than the syntax: a CSV field
starting `=`, `+`, `-` or `@` is a formula to a spreadsheet
([CSV injection](https://owasp.org/www-community/attacks/CSV_Injection)), and a
URL query string percent-encodes 19 of the 85 characters (use Base64url).

The general rule is unchanged and applies to every encoding, Base64 included:
encoding removes the need for a second *encoding* step, and it is never a
substitute for escaping a value for the context it is interpolated into.

### ⚠️ Encoding text you did not author

Encoding is the safe direction in the sense that it cannot fail on content —
every byte is representable. It is not free of denial-of-service risk.

The encoder searches for a Dynamic Passthrough prefix before it can decide
which mode to use, and the block-mode fallback consumes only four bytes. Under
the v0.1.0 procedure — which scanned to the end of a representable run on every
iteration of the encoding loop — that is **quadratic in the length of such a
run**, and it is reachable from ordinary content, not just from crafted input.

This was not hypothetical. Benchmarking in August 2026 found every one of the
implementations affected. The CommonMark specification — plain Markdown —
encoded at 0.22 MB/s, because under that version's rules a single `>` anywhere
in a run made every backtick in that run an escaped byte. A 100 kB buffer of `~` characters took
14.3 seconds in optimised C, and the cost quadrupled with every doubling of
length, so a megabyte would have taken roughly 25 minutes on one core.

What this means for you:

- **It is fixed here.** Specification v0.2.0 Section 6.6 makes linear-time
  encoding a normative requirement, and every implementation satisfies it with
  byte-identical output. Every language's test suite has a regression test
  asserting sub-quadratic growth.
- **v0.3.0 removed the shape that caused it**, and every version since keeps it
  removed.
  Prefix identification is bounded at `MAX_DP_ANALYSIS_BYTES` (2048) and is a
  single forward scan, so a non-conforming encoder is slow by a constant factor
  rather than quadratic. Section 6.6 remains normative, because a factor of
  2048 reached by ordinary binary input is still a denial-of-service surface.
- **If you write your own encoder, read Section 6.6 before you start.**
- **If you vendored an implementation from before 2026-08-10, update it.** An
  attacker who can place a few hundred kilobytes of the wrong-shaped text into
  a field you encode can occupy a CPU core for minutes.
- Bound the size of text you encode on behalf of untrusted parties, as you would
  for any other input-proportional work.

## Measures already taken

These are the assurance measures that are actually in place today, not
aspirations:

**Specification level**

- Dynamic Passthrough has no escape mechanism. A segment's signal names the R-Set characters it contains and the donor profile
  that supplies their stand-ins (spec Section 4.3); the derived substitution is
  injective, so a character has exactly one meaning inside a segment and nothing
  needs escaping. This removes, rather than manages, the order-dependency that
  v0.2.0's two-pass procedure existed to contain and the dangling-escape error
  that its segmentation rule existed to avoid.
- Fill is the one construct whose output is not bounded by its input, and it is
  bounded explicitly: one signal expands to at most `MAX_FILL_BYTES` (2048)
  bytes in the solid variant and 34 in the tail variant, which caps the
  format's decompression ratio at about 410:1 (spec Sections 7.4 and 13). A
  decoder that sizes its output buffer from the input length alone is wrong and
  must grow it per signal instead. As of v0.5.0 that bound is fuzzed: 60,000
  randomised round trips and 60,000 arbitrary Alphabet-N strings under
  AddressSanitizer and UndefinedBehaviorSanitizer, which closes an item this
  file and spec Section 14.4 both carried as open.
- A final block must be the canonical encoding of the bytes it decodes to (spec
  Section 7.5), so a byte string has exactly one encoding and a decoder cannot
  be fed two different strings that mean the same thing.
- A candidate prefix is bounded at `MAX_DP_ANALYSIS_BYTES`, so it always fits a
  single signal and no segment-splitting rule is needed (spec Section 6.1).
- Profile selection is specified down to its tie-break — longest prefix wins,
  smallest viable profile identifier breaks a tie — so two conforming encoders
  cannot disagree on the output for the same input (spec Sections 6.2 and 6.5).
- Partial-block padding is deferred to the genuinely final block, so a decoder
  can never mistake a padded non-final remainder for the start of the next group
  (spec Section 6.1, step 2.b, and Section 6.2).
- All decoder error conditions are enumerated normatively (spec Section 10), and
  Section 13 states the decoder's *and* the encoder's security obligations.
- Linear-time encoding is a normative requirement (spec Section 6.6, new in
  v0.2.0), added after benchmarking found the naive reading of Section 6.1 to be
  quadratic in every implementation. The section states the bound, explains
  why the obvious implementation misses it, and describes a technique that meets
  it.

**Implementation level**

- Four independent implementations (Rust, Go, TypeScript, C) of the same
  specification, cross-checked against one shared set of golden vectors — a
  divergence in any one of them shows up as a test failure rather than as
  silently different output. The Python package is bindings to the Rust one, so
  it is not a fifth: what it adds is reach, not independent evidence.
- Every implementation reports decode failures through an explicit typed error
  rather than by returning partial or garbage output.
- The C implementation is built with `-std=c11 -Wall -Wextra -Werror`, with no
  warnings suppressed, and its test binary is built and run under
  AddressSanitizer and UndefinedBehaviorSanitizer
  (`-fsanitize=address,undefined -fno-sanitize-recover=all`) whenever the
  toolchain supports it.
- Go code is checked with `go vet`; Rust with `cargo clippy -D warnings`;
  TypeScript is compiled with `strict` type checking.
- The Rust crate exports the C ABI as well (`rust/src/ffi.rs`), so a caller in
  any FFI-capable language can have C's calling convention with a
  bounds-checked parser behind it. Its `unsafe` is confined to that one file —
  four pointer-validating entry points and one `malloc`-and-copy helper — while
  the encoder and decoder contain none. A C program is compiled against both
  that header and the C implementation's, linked against the Rust library, and
  run in CI (`rust/capi/run.sh`).
- No implementation has any runtime third-party dependency.

**Test level**

- Shared golden encode/decode vectors
  ([`testvectors/vectors.json`](testvectors/vectors.json)) verified by every
  test suite, the Python bindings included.
- A shared **adversarial** vector set
  ([`testvectors/adversarial_vectors.json`](testvectors/adversarial_vectors.json))
  aimed specifically at decoding hostile input. Each entry either must be
  rejected with a specific error code and must not crash, or is a spec-legal
  input that no conforming encoder would ever emit. It covers:
  - multi-byte Unicode placed where "character position" can diverge from a
    language's actual storage unit (UTF-8 byte / UTF-16 code unit / codepoint),
    which is where misindexing and crashes tend to live;
  - the three signal ranges of spec Section 9 from both sides: the last DP
    signal, the first and last Fill signal, and values in
    `FUTURE_SIGNAL_SPACE` that must be rejected;
  - signals declaring more data than remains in the stream;
  - every one of the eight profile identifiers over the same segment data, and
    partial masks, so a decoder that ignores the profile field or derives the
    donors in the wrong order is caught;
  - Fill signals that expand to the 2048-byte maximum, and back-to-back;
  - non-canonical final blocks — trailing groups that `#`-pad to the right
    bytes but are not the encoding of them;
  - both length fields' bias of one, at both ends of their range.
- Seeded randomized round-trip property tests (`decode(encode(x)) == x`) over
  mixed byte content and a wide range of lengths, in every language.
- Explicit boundary tests: empty input, 1–4 byte inputs, the
  `MIN_PASSTHROUGH_BYTES` (20) boundary, the `MAX_DP_ANALYSIS_BYTES` (2048)
  window boundary and multi-segment output beyond it, the Fill thresholds and
  the Fill cap, every R-Set character carried in a segment, every donor
  character appearing literally, and all 256 byte values.
- Malformed-input tests in every language asserting that `decode` returns/raises
  an error and never panics, aborts, or returns garbage.
- Complexity regression tests in every language, asserting both a wall-clock
  ceiling and sub-quadratic growth when encoding input on which no alphabet
  reaches `MIN_PASSTHROUGH_BYTES` — the worst case for the per-iteration rescan
  that Section 6.6 forbids.
- A [benchmark suite](bench/README.md) that measures Base85N against Base64,
  Ascii85, Z85 and RFC 1924 Base85 on real files. It is an assurance measure as
  much as a marketing one: it is what surfaced the quadratic encoder, and every
  measurement it reports is round-trip verified, with its C harness also run
  under ASan/UBSan.
- All of the above run in CI on every push and pull request, across every
  language toolchain in the repository.

## Measures still outstanding

Known gaps. The specification is final — the wire format is frozen and the
feature set is closed — but the project is not 1.0.0, and this list is why.
None of these is waiting on a format decision; all of them are work:

- **No independent security review yet.** Nobody outside the project has
  reviewed either the specification or the implementations. This is the gap the
  project would most like closed, and
  [reviews are actively invited](README.md#reviews-wanted--this-is-the-most-useful-thing-you-can-contribute)
  — of the decoder's parsing in particular.
- **No continuous fuzzing campaign, and no OSS-Fuzz.** There are now three
  libFuzzer targets under `c/fuzz/`, all built with ASan and UBSan — encoder
  round trip, decoder robustness against arbitrary input, and C against Rust
  in one process — and CI runs each for two minutes on every push. Two
  minutes is a regression check, not a campaign: it re-covers a seeded
  corpus, it does not explore. Nothing runs for hours, nothing keeps a corpus
  between runs, and there is no OSS-Fuzz integration. Go and TypeScript have
  no fuzzing harness of their own.
- **Differential fuzzing covers two of the four implementations.**
  `c/fuzz/fuzz_differential.c` compares C and Rust on generated input,
  because the Rust crate's C ABI lets both be linked into one process. Go and
  TypeScript are still covered only by the shared vectors and by the
  generated differential corpus (`tools/gen_differential_cases.py`, 6,146
  cases chosen for the encoder's branch boundaries), which are fixed sets run
  by hand.

  This gap was not theoretical. Within seconds of first running, the
  differential target found that the four implementations disagreed about
  which error a lone trailing character outside Alphabet-N is, and that the
  Rust C ABI read its input as UTF-8 — reporting an invalid character ahead
  of the end-of-stream check the specification orders first, and counting one
  character where a multi-byte sequence is several bytes to everyone else.
  Both are fixed and pinned as `error_precedence` vectors; neither affected
  the decoding of any valid stream. See `spec/history/lessons.md`.
- **Sanitizer coverage is partial.** ASan/UBSan cover the C test suite and all
  three fuzz targets, so fuzzed input does reach a sanitizer. There is still
  no MemorySanitizer or Valgrind run, and no sanitizer coverage of the Go or
  TypeScript implementations.
- **No formal proof or model check** of the prefix-identification procedure or
  of decoder round-trip totality.
- **No memory/CPU bound enforced by the libraries themselves.** Input-size
  limiting is left entirely to the caller.
- **The parallel encoder is new.** `encode_parallel` (Rust, and Python's
  `threads=` argument) is asserted equal to the sequential encoder by the test
  suite at several thread counts and seam positions, and it found a real defect
  in the sequential encoder's lookahead while being written. It has not been
  fuzzed, and it is the only code in the project that runs input through more
  than one thread.
- **No signed releases.** There are no signed tags, no published checksums, and
  no reproducible-build attestation, so a package you install cannot yet be
  verified cryptographically against this repository.
- **No CVE/advisory process** beyond the email contact above.
- **Not constant-time.** No implementation attempts side-channel resistance, and
  none should be used on secret-dependent data where timing or length is
  observable.

## What you, as a user, should do

**Check where the code came from, and that it is unmodified.**

- The canonical source is <https://github.com/keywan-ghadami/base85n>, and it is
  what any published package is built from. If you install a crate, npm package,
  PyPI package or Go module named "base85n", check that it points back here
  before you depend on it.
- Pin to a specific commit SHA rather than to a branch name. Branches move;
  a SHA does not.
  ```sh
  # Go
  go get github.com/keywan-ghadami/base85n/go@<commit-sha>
  # Rust (Cargo.toml)
  base85n = { git = "https://github.com/keywan-ghadami/base85n", rev = "<commit-sha>" }
  ```
- If you vendor the code, record the upstream commit SHA next to it and diff
  against upstream before each update, rather than pulling blind.
- Verify integrity after fetching, e.g. `git fsck`, and confirm that
  `git log --oneline -1` matches the SHA you intended.
- Because this project has no signed releases yet, you cannot verify
  authenticity cryptographically. If your supply-chain policy requires signed
  artifacts, that is a gap to account for.
- Review the diff yourself before upgrading. The code is small enough to read,
  which is a deliberate property — and if you do read it, the project would like
  to hear what you found: see
  [how this was built, and where review is welcome](README.md#how-this-was-built--and-where-review-is-welcome).

**Use it defensively.**

- Never decode untrusted input without a size bound and without handling the
  error path (see the ⚠️ section above).
- Run your own tests against your own inputs; do not treat this project's test
  suite as a substitute for validating your integration.
- Do not use Base85N as a security control. It is not one.
- Keep sanitizers on in your own CI if you link the C library.
- Watch this repository for changes if you depend on it; there is no separate
  advisory feed.
