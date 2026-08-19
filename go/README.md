# base85n (Go)

A Go implementation of Base85N, an encoding for data that has to be embedded in
a text-based format — JSON, XML, HTML, configuration files — where Base64 would
otherwise be used and the size or the cleanliness of the result matters. It
combines a dense 4-byte-to-5-character Base85 core with an adaptive Dynamic
Passthrough (DP) mode for partially human-readable output. See
[the specification](../spec/base85n-v0.5.0.md) for the full normative text, in
particular Section 4.2's donor profiles and Section 6.1's
single-scan Dynamic Passthrough prefix identification,
which this module follows exactly.

## Install

```sh
go get github.com/keywan-ghadami/base85n/go@v0.5.1
```

Nothing is uploaded anywhere: the module proxy fetches the tag from this
repository on first request, and `sum.golang.org` records the hash of what it
fetched, so a later change to a released tag would show up in anybody's
`go.sum`. The tags are named `go/vX.Y.Z` because the module lives in a
subdirectory — Go derives one from the other, and the version you write stays
`vX.Y.Z`.

## Versioning

The major and minor version track the specification version this package
implements — `v0.5.x` implements specification v0.5.0, whose wire format is
frozen. The patch level is this package's own: changes that alter no encoded
output.

## Usage

```go
import "github.com/keywan-ghadami/base85n/go"

encoded := base85n.Encode([]byte("hello world this is a test"))

decoded, err := base85n.Decode(encoded)
if err != nil {
    // err wraps one of base85n.ErrInvalidCharacter, ErrUnexpectedEOF,
    // ErrUndefinedSignal, ErrInvalidFinalBlock.
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
    ErrReservedSignal      error
    ErrInvalidFinalBlock error
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
DP output, the `MAX_DP_ANALYSIS_BYTES` window, all 256 byte values), and
verifies that `Decode` returns errors (never panics) on malformed input.
