# base85n

A Python implementation of **Base85N**, a binary-to-text encoding scheme
using a single 85-character alphabet (Alphabet-N) with an adaptive Dynamic
Passthrough (DP) mode for efficient, partially human-readable
representation of compatible byte sequences. See [the specification](../spec/base85n-v0.1.0.md)
for the full normative text, in particular
Section 6.1's two-pass ("Pass 1" window/mask discovery, "Pass 2" boundary
finalization) Dynamic Passthrough encoding procedure, which this package
follows exactly.

This package started life as the project's reference implementation (used
to generate the shared golden test vectors in
[`../testvectors/`](../testvectors/) that every language's test suite
verifies against), cleaned up into an installable library alongside the
Rust, Go, TypeScript, and C implementations.

## Install

```bash
pip install -e ".[test]"
```

## Usage

```python
from base85n import encode, decode

data = b"hello, world!"
encoded = encode(data)
decoded = decode(encoded)
assert decoded == data
```

`decode` raises `Base85NDecodeError` (a `ValueError` subclass) on
malformed input; `err.code` is a `Base85NErrorCode` member identifying
which of the specification's Section 10 error conditions was hit, and
`err.position` is the character offset (after inter-token whitespace has
been stripped) at which the error was detected.

## Test

```bash
pytest
```

The test suite (`tests/`) checks every implementation against:

- the shared golden vectors in `../testvectors/vectors.json`;
- randomized round-trip properties (`encode` then `decode` reproduces the
  original bytes) across varied lengths and byte-content mixes, with a
  deterministic, seeded `random.Random`;
- explicit edge cases (empty input, partial-block boundary lengths,
  `MIN_PASSTHROUGH_BYTES` boundary, multi-segment DP signals, the
  `MAX_CONSECUTIVE_ESCAPES` termination heuristic, all 256 byte values);
- decode-error handling for malformed input (invalid characters, a DP
  signal declaring more data than is available, a dangling escape
  character, a reserved signal payload, an invalid partial trailing
  block).
