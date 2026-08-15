# Base85N (C)

A portable, dependency-free C11 implementation of the Base85N
binary-to-text encoding scheme. See [the specification](../spec/base85n-v0.4.0.md)
for the full normative text, in particular Section 4.2's eight replacement
alphabets and Section 6.1's single-scan Dynamic Passthrough
encoding procedure, which this library follows exactly.

> **Decoding untrusted input?** The Rust crate builds as a C library with this
> same API — same function names, same status-code values, same `malloc`/`free`
> ownership — so you can link
> [`rust/include/base85n.h`](../rust/include/base85n.h) instead and get a
> bounds-checked parser without changing your calling convention. That is the
> recommended route for bindings in other languages; see
> [SECURITY.md](../SECURITY.md#recommended-bind-the-rust-build-not-the-c-one).
> This implementation stays supported and is the one to use when a Rust
> toolchain is not available.

## Build & test

```sh
make test    # builds libbase85n.a, builds tests/test_base85n, runs it
make all     # just builds libbase85n.a
make clean
```

`make test` builds the test binary with `-fsanitize=address,undefined`
whenever the active `$CC` toolchain supports it (detected automatically
via a throwaway compile), and falls back to a plain build otherwise, so
`make test` still works on a minimal toolchain. If `CC=clang make test`
reports that sanitizers are unsupported, the clang sanitizer runtime
(`libclang_rt.asan*`) is not installed on that machine; the tests still
run, just without instrumentation.

CI runs this target under both `gcc` and `clang`, plus the CMake/CTest
build, on every push.

Compiled with `-std=c11 -Wall -Wextra -Werror`, no warnings suppressed.

A `CMakeLists.txt` is also provided:

```sh
mkdir build && cd build
cmake ..
make
ctest --output-on-failure
```

## Layout

- `include/base85n.h` — public API (`base85n_encode`, `base85n_decode`,
  `base85n_strerror`), with ownership documented in header comments.
- `src/base85n.c` — implementation.
- `tests/test_base85n.c` — self-contained test suite (golden vectors,
  randomized round-trip property tests with a fixed seed, explicit
  boundary edge cases, and decode-error/malformed-input tests).

## API

```c
typedef enum {
    BASE85N_OK = 0,
    BASE85N_ERR_INVALID_CHAR,
    BASE85N_ERR_UNEXPECTED_EOF,
    BASE85N_ERR_UNDEFINED_SIGNAL,
    BASE85N_ERR_INVALID_FINAL_BLOCK,
    BASE85N_ERR_ALLOC,
    BASE85N_ERR_INVALID_ARGUMENT
} base85n_status;

base85n_status base85n_encode(const uint8_t *data, size_t data_len,
                               char **out_str, size_t *out_len);

base85n_status base85n_decode(const char *s, size_t s_len,
                               uint8_t **out_data, size_t *out_len);

const char *base85n_strerror(base85n_status status);
```

Both functions return newly `malloc`'d buffers on `BASE85N_OK`
(`*out_str` is NUL-terminated; `*out_data` is not) which the caller
must release with `free()`. No global state; safe to call concurrently
from multiple threads.

## Notes / deviations

None. The implementation follows the specification literally, including its
Section 6.1 single-scan Dynamic Prefix Identification, the smallest-identifier
tie-break, and the Block Mode fallback rule (step 2.b).
