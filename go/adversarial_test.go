// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package base85n

import (
	"encoding/json"
	"errors"
	"os"
	"path/filepath"
	"testing"
)

// Adversarial decode vectors (testvectors/adversarial_vectors.json):
// multi-byte Unicode input at various positions (character-position vs.
// storage-unit discrepancies), 0-length DP signals, invalid/reserved DP
// signals, and deliberately malformed escaping.

type adversarialVector struct {
	Name        string `json:"name"`
	Category    string `json:"category"`
	Kind        string `json:"kind"` // "must_fail" or "valid"
	InputHex    string `json:"input_hex"`
	ErrorCode   string `json:"error_code"`
	ExpectedHex string `json:"expected_hex"`
}

func loadAdversarialVectors(t *testing.T) []adversarialVector {
	t.Helper()
	path := filepath.Join("..", "testvectors", "adversarial_vectors.json")
	data, err := os.ReadFile(path)
	if err != nil {
		t.Fatalf("failed to read adversarial vectors at %s: %v", path, err)
	}
	var vecs []adversarialVector
	if err := json.Unmarshal(data, &vecs); err != nil {
		t.Fatalf("failed to parse adversarial vectors: %v", err)
	}
	if len(vecs) < 15 {
		t.Fatalf("expected a non-trivial adversarial vector set, got %d", len(vecs))
	}
	return vecs
}

// sentinelForErrorCode maps the shared error_code string (identical across
// every language implementation) to this package's sentinel error.
func sentinelForErrorCode(code string) error {
	switch code {
	case "invalid_character":
		return ErrInvalidCharacter
	case "unexpected_end_of_stream":
		return ErrUnexpectedEOF
	case "undefined_signal":
		return ErrUndefinedSignal
	case "invalid_final_block":
		return ErrInvalidFinalBlock
	default:
		return nil
	}
}

func TestAdversarialVectors(t *testing.T) {
	vectors := loadAdversarialVectors(t)

	for _, v := range vectors {
		v := v
		t.Run(v.Name, func(t *testing.T) {
			inputBytes := mustHexDecode(t, v.InputHex)
			input := string(inputBytes) // Go strings are just byte sequences; preserves the UTF-8 bytes as-is.

			switch v.Kind {
			case "must_fail":
				want := sentinelForErrorCode(v.ErrorCode)
				if want == nil {
					t.Fatalf("unknown error_code %q", v.ErrorCode)
				}
				got, err := Decode(input)
				if err == nil {
					t.Fatalf("expected Decode to fail with %v, but it succeeded with %v", want, got)
				}
				if !errors.Is(err, want) {
					t.Fatalf("expected error category %v, got %v", want, err)
				}
			case "valid":
				expected := mustHexDecode(t, v.ExpectedHex)
				got, err := Decode(input)
				if err != nil {
					t.Fatalf("expected Decode to succeed, got error %v", err)
				}
				if string(got) != string(expected) {
					t.Fatalf("decoded bytes did not match expected_hex: got %x, want %x", got, expected)
				}
			default:
				t.Fatalf("unknown vector kind %q", v.Kind)
			}
		})
	}
}
