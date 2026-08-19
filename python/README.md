# base85n (Python bindings)

Python bindings for **Base85N**, an encoding for data that has to be embedded in
a text-based format — JSON, XML, HTML, configuration files — where Base64 would
otherwise be used and the size or the cleanliness of the result matters. It uses
a single 85-character alphabet (Alphabet-N) with a Dynamic Passthrough mode for
efficient, partially human-readable representation of text-like bytes and a
Solid Fill mode for runs of identical bytes. See
[the specification](https://github.com/keywan-ghadami/base85n/blob/main/spec/base85n-v0.5.0.md) for the full normative text.

**This is not a Python implementation of the format.** It is a thin
[PyO3](https://pyo3.rs) layer over the Rust crate in [`rust/`](https://github.com/keywan-ghadami/base85n/tree/main/rust),
packaged by [maturin](https://www.maturin.rs), so what Python runs is the same
encoder and decoder the Rust and C-ABI callers get — one implementation to
review, one to keep in step with the specification. The hand-written Python
implementation that used to live here was replaced in version 0.4.0.

## Install

```bash
pip install base85n
```

Wheels are published for Linux (glibc and musl, x86-64 and arm64), macOS
(Intel and Apple silicon) and Windows (x64). They are `abi3` from CPython 3.9
up, so one wheel per platform serves every interpreter version from 3.9 to 3.14
and nothing is compiled at install time. Where no wheel matches — an unusual
platform, a source-only policy — pip falls back to the source distribution,
which carries the Rust crate with it and needs a Rust toolchain
([rustup](https://rustup.rs) is enough) to build.

From a checkout:

```bash
pip install .            # from this directory
pip install ".[test]"    # plus pytest
```

or, for a development build that skips the wheel:

```bash
maturin develop --release
```

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
encodes about 2.4× faster at `threads=4` (see
`cargo run --release --example parallel` in
[`rust/`](https://github.com/keywan-ghadami/base85n/tree/main/rust)).

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
test vectors in [`testvectors/`](https://github.com/keywan-ghadami/base85n/tree/main/testvectors) use, so a vector's
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

### Type information

The distribution is typed: the wheel carries `base85n/__init__.pyi` and the
PEP 561 marker, so mypy and pyright check calls into it like any other typed
package rather than treating the module as `Any`. There is nothing to install
alongside it and no `types-base85n` stub package.

```python
reveal_type(base85n.encode(b""))    # str
base85n.encode("text")              # error: expected bytes | bytearray
```

The stubs are checked against the module that was built, not maintained by
eye: `tests/test_stubs.py` reads the stub file that ended up in the wheel and
compares the names, the value types and the call signatures with the extension
it sits next to.

## Test

```bash
pytest
```

The suite covers what is specific to the binding rather than to the format —
argument types, the exception and its attributes, the constants, and that the
GIL is released — plus the shared golden and adversarial vectors end to end, as
a check that the built wheel really is the implementation the rest of the
repository agrees on. The format itself is tested in
[`rust/src/tests/`](https://github.com/keywan-ghadami/base85n/tree/main/rust/src/tests).

## Packaging

Everything that has to be true before a release — the version agreeing in all
three manifests, the stubs and the marker reaching the wheel, a type checker
finding them, and the source distribution building and passing its own suite
outside the repository — is one script:

```bash
../tools/python-package-check.sh          # or with the version to release
```

It runs here, in CI on every push, and again in the release workflow on the
exact tree that is about to be uploaded. It builds into a temporary directory
and installs its tools into throwaway virtual environments, so it changes
nothing in the working copy.

## Versioning

The major and minor version track the specification version this package
implements — `0.5.x` implements specification v0.5.0, whose wire format is
frozen. The patch level is this package's own: packaging, provenance and
documentation changes that alter no encoded output. Anything that would change
the wire format would change the specification's version first.

## Provenance

What is on PyPI is built and uploaded by
[`.github/workflows/release_python.yml`](https://github.com/keywan-ghadami/base85n/blob/main/.github/workflows/release_python.yml),
never from anyone's machine, and no upload token exists to be stolen: the
workflow identifies itself to PyPI with a token minted for that single run.
Every file carries two signatures over the same bytes, both keyless — no
signing key exists for longer than the job that made it — and both recording the
repository, the workflow and the commit the file came from:

```sh
# SLSA build provenance, on a file you already have, whatever you got it from
gh attestation verify base85n-<version>-*.whl --repo keywan-ghadami/base85n

# the PEP 740 attestation, as PyPI stores and displays it
pypi-attestations verify pypi base85n-<version>-*.whl \
  --repository https://github.com/keywan-ghadami/base85n
```

Each tagged release also appears under
[Releases](https://github.com/keywan-ghadami/base85n/releases) with the exact
wheels and source distribution that were uploaded and their checksums.
