# base85n

A Rust implementation of **Base85N**, a binary-to-text encoding scheme
combining a dense 4-byte-to-5-character Base85 core with an adaptive
Dynamic Passthrough (DP) mode for near 1:1-efficiency, partially
human-readable output on favorable input.

See [`../README.md`](../README.md) for the full specification, in
particular Section 6.1's two-pass ("Pass 1" window/mask discovery,
"Pass 2" boundary finalization) Dynamic Passthrough encoding procedure,
which this crate follows exactly and exercises in its test suite.

## Usage

```rust
let data = b"hello, world!";
let encoded = base85n::encode(data);
let decoded = base85n::decode(&encoded).unwrap();
assert_eq!(decoded, data);
```

## API

```rust
pub fn encode(data: &[u8]) -> String;
pub fn decode(s: &str) -> Result<Vec<u8>, DecodeError>;
```

`DecodeError` implements `std::error::Error` and `Display`, with variants
for each error condition described in the spec's Section 10 (invalid
character, unexpected end of stream, dangling escape character,
reserved/undefined DP signal payload, and invalid partial trailing block).

## Building and testing

```sh
cargo build
cargo test
```

The test suite:

- Loads and verifies every golden vector in
  [`../testvectors/vectors.json`](../testvectors/vectors.json) for both
  `encode` and `decode`.
- Runs randomized, fixed-seed round-trip property tests over a mix of
  arbitrary bytes, Alphabet-N literals, R-Set characters, and the escape
  character (`~`), across a wide range of input lengths.
- Exercises explicit edge cases: empty input, partial-block boundary
  lengths (1-4 bytes), the `MIN_PASSTHROUGH_BYTES` (20) boundary,
  multi-segment Dynamic Passthrough output (transformed length > 511
  characters), escape runs that trigger the `MAX_CONSECUTIVE_ESCAPES`
  scan-termination heuristic, and every byte value 0-255.
- Feeds `decode` deliberately malformed input (invalid characters,
  truncated DP segments, dangling escapes, reserved signal payloads,
  invalid partial trailing groups) and asserts it returns `Err`, never
  panics.
