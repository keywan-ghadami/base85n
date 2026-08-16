# base85n (Python bindings)

Python bindings for **Base85N**, a binary-to-text encoding scheme using a
single 85-character alphabet (Alphabet-N) with a Dynamic Passthrough mode for
efficient, partially human-readable representation of text-like bytes and a
Solid Fill mode for runs of identical bytes. See
[the specification](../spec/base85n-v0.5.0.md) for the full normative text.

**This is not a Python implementation of the format.** It is a thin
[PyO3](https://pyo3.rs) layer over the Rust crate in [`../rust/`](../rust/),
packaged by [maturin](https://www.maturin.rs), so what Python runs is the same
encoder and decoder the Rust and C-ABI callers get — one implementation to
review, one to keep in step with the specification. The hand-written Python
implementation that used to live here was replaced in version 0.4.0.

## Install

Building the wheel needs a Rust toolchain ([rustup](https://rustup.rs) is
enough); nothing is published to PyPI.

```bash
pip install .            # from this directory
pip install ".[test]"    # plus pytest
```

or, for a development build that skips the wheel:

```bash
maturin develop --release
```

The wheel is `abi3` from CPython 3.9 up, so one build serves every supported
interpreter version.

## Usage

```python
from base85n import encode, decode

data = b"hello, world!"
encoded = encode(data)          # str
decoded = decode(encoded)       # bytes
assert decoded == data
```

`encode` takes `bytes` or `bytearray` and never fails. On a large payload it
can also use several cores:

```python
encoded = encode(data, threads=0)   # 0 = one worker per available core
```

`threads` is a performance knob and nothing else. The format has a single
canonical encoding and the parallel encoder reproduces it exactly, so every
thread count returns the same string; inputs below a couple of megabytes
ignore the argument entirely. On a four-core machine, 16 MiB of mixed input
encodes about 2.7× faster at `threads=4` (see
`cargo run --release --example parallel` in `../rust/`).

`decode` takes `str`,
`bytes` or `bytearray` and raises `Base85NDecodeError` (a `ValueError`
subclass) on malformed input:

```python
from base85n import Base85NDecodeError, decode

try:
    decode("abcd|e")
except Base85NDecodeError as err:
    err.code        # "invalid_character" -- one of the spec section 10 conditions
    err.position    # byte offset where it was detected, or None
```

`err.code` is one of `"invalid_character"`, `"unexpected_end_of_stream"`,
`"undefined_signal"` or `"invalid_final_block"` — the same strings the shared
test vectors in [`../testvectors/`](../testvectors/) use, so a vector's
`error_code` compares directly against it.

Both calls release the GIL for the duration of the encode or decode, so other
threads keep running while a large buffer is converted.

### Constants

The module re-exports the tables and thresholds of specification sections 4,
6.4 and 9, so tooling does not have to transcribe them:

```python
import base85n

base85n.ALPHABET_N                  # the 85 characters, in index order
base85n.R_SET                       # the 13 R-Set bytes, in R-Set index order
base85n.PROFILES                    # the eight donor profiles
base85n.MIN_PASSTHROUGH_BYTES       # 20
base85n.MIN_FILL_BYTES              # 5
base85n.MIN_FILL_IN_SEGMENT_BYTES   # 16
base85n.MAX_FILL_BYTES              # 2048
base85n.MIN_TAIL_ZEROS              # 3
base85n.MAX_TAIL_ZEROS              # 32
base85n.MAX_DP_SEGMENT_CHARS        # 2048
base85n.DP_SIGNAL_BASE              # 2**32
base85n.FILL_SIGNAL_BASE            # 2**32 + 2**27
base85n.TAIL_SIGNAL_BASE            # 2**32 + 2**27 + 2**19
base85n.FUTURE_SIGNAL_BASE          # 2**32 + 2**27 + 2**19 + 2**22
base85n.SPEC_VERSION                # "0.5.0"
```

`tools/gen_vectors.py` and the benchmarks in `bench/` are built on exactly
these.

## Test

```bash
pytest
```

The suite covers what is specific to the binding rather than to the format —
argument types, the exception and its attributes, the constants, and that the
GIL is released — plus the shared golden and adversarial vectors end to end, as
a check that the built wheel really is the implementation the rest of the
repository agrees on. The format itself is tested in
[`../rust/src/tests/`](../rust/src/tests/).
