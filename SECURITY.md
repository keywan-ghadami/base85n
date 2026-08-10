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

Base85N is a binary-to-text **encoding**. It is not encryption, not a MAC, not
a checksum, and not obfuscation. Encoded output is fully reversible by anyone.
Nothing in this project provides confidentiality, integrity, or authenticity —
if you need those, apply them separately, and apply them to the *bytes*, not to
the encoded text.

The security-relevant surface is the **decoder**, because a decoder processes
data that your system did not produce.

### ⚠️ Decoding data from untrusted sources

**Decoding attacker-controlled strings is the risky operation in this
project.** A Base85N decoder parses a length-prefixed, escape-bearing format:
a DP signal declares how many characters follow, and escape sequences consume a
following character. Both are attacker-controlled, and both are classic sources
of out-of-bounds reads, integer overflow, over-allocation, and infinite loops —
particularly in the C implementation, which does its own memory management.

If you decode input that came from a network peer, a file upload, a URL
parameter, a database field populated by users, or any other source you do not
control:

- Treat the whole operation as parsing hostile input.
- Bound the input size before decoding. The decoders here allocate in
  proportion to their input.
- Handle the error path. Every implementation reports the Section 10 error
  conditions explicitly (`DecodeError` / `error` / `Base85NDecodeError` /
  `base85n_status`); do not ignore it, and do not treat a failed decode as an
  empty result.
- Treat the *decoded output* as untrusted binary, not as text. It may contain
  NUL bytes, control characters, and invalid UTF-8. Escape it for whatever
  context it lands in (HTML, SQL, shell, filesystem paths).
- Prefer a memory-safe implementation (Rust, Go, Python, TypeScript) over the C
  one when the input is untrusted and you have the choice.
- In C, remember that `base85n_encode`/`base85n_decode` hand you `malloc`'d
  buffers you must `free()`, and that the decoded buffer is **not**
  NUL-terminated.

## Measures already taken

These are the assurance measures that are actually in place today, not
aspirations:

**Specification level**

- The DP encoding procedure was reworked into an explicit two-pass algorithm
  (spec Section 6.1) precisely to remove an order-dependency in which a mask bit
  set late in a scan could retroactively change how an earlier byte had been
  encoded.
- Segment boundaries are specified so they can never fall inside a two-character
  escape pair, which would otherwise produce a dangling-escape error as an
  artifact of segmentation (spec Section 6.1, step 1.d).
- Partial-block padding is deferred to the genuinely final block, so a decoder
  can never mistake a padded non-final remainder for the start of the next group
  (spec Section 6.1, step 2.b, and Section 6.2).
- All decoder error conditions are enumerated normatively (spec Section 10), and
  Section 13 states the decoder's security obligations.

**Implementation level**

- Five independent implementations (Rust, Go, TypeScript, C, Python) of the same
  specification, cross-checked against one shared set of golden vectors — a
  divergence in any one of them shows up as a test failure rather than as
  silently different output.
- Every implementation reports decode failures through an explicit typed error
  rather than by returning partial or garbage output.
- The C implementation is built with `-std=c11 -Wall -Wextra -Werror`, with no
  warnings suppressed, and its test binary is built and run under
  AddressSanitizer and UndefinedBehaviorSanitizer
  (`-fsanitize=address,undefined -fno-sanitize-recover=all`) whenever the
  toolchain supports it.
- Go code is checked with `go vet`; Rust with `cargo clippy -D warnings`;
  TypeScript is compiled with `strict` type checking.
- No implementation has any runtime third-party dependency.

**Test level**

- Shared golden encode/decode vectors
  ([`testvectors/vectors.json`](testvectors/vectors.json)) verified by all five
  test suites.
- A shared **adversarial** vector set
  ([`testvectors/adversarial_vectors.json`](testvectors/adversarial_vectors.json))
  aimed specifically at decoding hostile input. Each entry either must be
  rejected with a specific error code and must not crash, or is a spec-legal
  input that no conforming encoder would ever emit. It covers:
  - multi-byte Unicode placed where "character position" can diverge from a
    language's actual storage unit (UTF-8 byte / UTF-16 code unit / codepoint),
    which is where misindexing and crashes tend to live;
  - zero-length DP signals;
  - reserved and out-of-range signal payloads, plus the adjacent still-valid
    boundary value;
  - signals declaring more data than remains in the stream;
  - dangling escapes and escape resolution that must stay inside its segment.
- Seeded randomized round-trip property tests (`decode(encode(x)) == x`) over
  mixed byte content and a wide range of lengths, in every language.
- Explicit boundary tests: empty input, 1–4 byte inputs, the
  `MIN_PASSTHROUGH_BYTES` (20) boundary, multi-segment DP output beyond
  `MAX_DP_OUTPUT_CHARS_PER_SIGNAL` (511), the `MAX_CONSECUTIVE_ESCAPES`
  termination heuristic, and all 256 byte values.
- Malformed-input tests in every language asserting that `decode` returns/raises
  an error and never panics, aborts, or returns garbage.
- All of the above run in CI on every push and pull request, across five
  language toolchains.

## Measures still outstanding

Known gaps. These are the reasons this project is a 0.x draft:

- **No independent security review.** Nobody outside the project has audited
  either the specification or the implementations.
- **No fuzzing.** There is no continuous fuzzing harness
  (libFuzzer/AFL++/`cargo-fuzz`/`go-fuzz`/Atheris) for any implementation, and
  no OSS-Fuzz integration. The adversarial vectors are hand-picked, not
  generated; they cover the failure modes that were anticipated, which is by
  definition not the same as the ones that exist.
- **No differential fuzzing between implementations.** The five implementations
  are cross-checked only against fixed vectors and per-language random tests,
  not against each other on the same randomized corpus.
- **Sanitizer coverage is partial.** ASan/UBSan cover the C test suite; there is
  no MemorySanitizer or Valgrind run, and no sanitizer coverage driven by
  fuzzed input.
- **No formal proof or model check** of the Pass 1 / Pass 2 encoding procedure
  or of decoder round-trip totality.
- **No memory/CPU bound enforced by the libraries themselves.** Input-size
  limiting is left entirely to the caller.
- **No signed releases.** There are no signed tags, no published checksums, no
  reproducible-build attestation, and no packages published to crates.io, npm,
  PyPI, or pkg.go.dev by this project. Anything you find under those names on a
  package registry was not published from here.
- **No CVE/advisory process** beyond the email contact above.
- **Not constant-time.** No implementation attempts side-channel resistance, and
  none should be used on secret-dependent data where timing or length is
  observable.

## What you, as a user, should do

**Check where the code came from, and that it is unmodified.**

- The only canonical source is
  <https://github.com/keywan-ghadami/base85n>. This project publishes nothing to
  any package registry (see above). A crate, npm package, PyPI package, or Go
  module claiming to be "base85n" that you did not fetch from this repository
  did not come from here — verify before you install it.
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
  authenticity cryptographically. Treat that as a real limitation: if your
  supply-chain policy requires signed artifacts, do not use this project as a
  dependency yet.
- Review the diff yourself before upgrading. The code is small enough to read —
  that is a deliberate property, and the best mitigation available given that it
  is AI-generated and unaudited (see the [AI notice](README.md#ai-generated-code--notice)).

**Use it defensively.**

- Never decode untrusted input without a size bound and without handling the
  error path (see the ⚠️ section above).
- Run your own tests against your own inputs; do not treat this project's test
  suite as a substitute for validating your integration.
- Do not use Base85N as a security control. It is not one.
- Keep sanitizers on in your own CI if you link the C library.
- Watch this repository for changes if you depend on it; there is no separate
  advisory feed.
