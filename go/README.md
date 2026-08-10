# base85n (Go)

A Go implementation of Base85N, a binary-to-text encoding scheme combining
a dense 4-byte-to-5-character Base85 core with an adaptive Dynamic
Passthrough (DP) mode for partially human-readable output. See
[the specification](../spec/base85n-v0.2.0.md) for the full normative text, in
particular Section 6.1's two-pass ("Pass 1" window/mask discovery,
"Pass 2" boundary finalization) Dynamic Passthrough encoding procedure,
which this module follows exactly.

## Install

```
go get github.com/keywan-ghadami/base85n/go
```

## Usage

```go
import "github.com/keywan-ghadami/base85n/go"

encoded := base85n.Encode([]byte("hello world this is a test"))

decoded, err := base85n.Decode(encoded)
if err != nil {
    // err wraps one of base85n.ErrInvalidCharacter, ErrUnexpectedEOF,
    // ErrDanglingEscape, ErrReservedSignal, ErrInvalidPartialBlock.
}
```

## Public API

```go
func Encode(data []byte) string
func Decode(s string) ([]byte, error)

type DecodeError struct { Offset int; Err error }

var (
    ErrInvalidCharacter    error
    ErrUnexpectedEOF       error
    ErrDanglingEscape      error
    ErrReservedSignal      error
    ErrInvalidPartialBlock error
)
```

## Build & Test

```
go build ./...
go vet ./...
go test ./...
```

The test suite loads the shared golden vectors from
[`../testvectors/vectors.json`](../testvectors/vectors.json), runs
randomized round-trip property tests (`Decode(Encode(data)) == data`) over
varying lengths and byte mixes, exercises explicit edge cases (empty input,
partial-block boundaries, the `MIN_PASSTHROUGH_BYTES` boundary, multi-segment
DP output, the `MAX_CONSECUTIVE_ESCAPES` heuristic, all 256 byte values), and
verifies that `Decode` returns errors (never panics) on malformed input.
