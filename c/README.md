# Base85N (C)

A portable, dependency-free C11 implementation of the Base85N
binary-to-text encoding scheme. See the top-level [`../README.md`](../README.md)
for the full specification and [`../NOTES.md`](../NOTES.md) for two
mandatory clarifications this implementation follows (both are also
called out at their point of use in `src/base85n.c`).

## Build & test

```sh
make test    # builds libbase85n.a, builds tests/test_base85n, runs it
make all     # just builds libbase85n.a
make clean
```

`make test` builds the test binary with `-fsanitize=address,undefined`
whenever the active `$CC` toolchain supports it (detected automatically
via a throwaway compile), and falls back to a plain build otherwise. In
this sandbox, `gcc` supports the sanitizers and is used for the
validated build; `clang` is present but its sanitizer runtime
(`libclang_rt.asan*`) is not installed, so `CC=clang make test` builds
without sanitizers (still passes).

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
    BASE85N_ERR_DANGLING_ESCAPE,
    BASE85N_ERR_RESERVED_SIGNAL,
    BASE85N_ERR_INVALID_PARTIAL_BLOCK,
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

None of substance. The implementation follows `README.md` literally
except where `NOTES.md` mandates otherwise (the pass-1/pass-2 DP-mask
handling, and the restriction on when Block Mode may emit a partial
trailing character group), and both clarifications are implemented
exactly as documented there.
