// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package base85n

import (
	"bytes"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"math/rand"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// ---------------------------------------------------------------------
// Golden test vectors
// ---------------------------------------------------------------------

type vector struct {
	Name     string `json:"name"`
	InputHex string `json:"input_hex"`
	Output   string `json:"output"`
}

func loadVectors(t *testing.T) []vector {
	t.Helper()
	path := filepath.Join("..", "testvectors", "vectors.json")
	data, err := os.ReadFile(path)
	if err != nil {
		t.Fatalf("failed to read test vectors at %s: %v", path, err)
	}
	var vecs []vector
	if err := json.Unmarshal(data, &vecs); err != nil {
		t.Fatalf("failed to parse test vectors: %v", err)
	}
	if len(vecs) == 0 {
		t.Fatalf("no test vectors loaded from %s", path)
	}
	return vecs
}

func mustHexDecode(t *testing.T, s string) []byte {
	t.Helper()
	b, err := hex.DecodeString(s)
	if err != nil {
		t.Fatalf("invalid hex %q: %v", s, err)
	}
	return b
}

func TestGoldenVectors(t *testing.T) {
	vecs := loadVectors(t)
	for _, v := range vecs {
		v := v
		t.Run(v.Name, func(t *testing.T) {
			input := mustHexDecode(t, v.InputHex)

			got := Encode(input)
			if got != v.Output {
				t.Errorf("Encode mismatch:\n  input:    %s\n  expected: %s\n  got:      %s", v.InputHex, v.Output, got)
			}

			decoded, err := Decode(v.Output)
			if err != nil {
				t.Fatalf("Decode(%q) returned error: %v", v.Output, err)
			}
			if !bytes.Equal(decoded, input) {
				t.Errorf("Decode mismatch:\n  encoded:  %s\n  expected: %x\n  got:      %x", v.Output, input, decoded)
			}
		})
	}
}

// ---------------------------------------------------------------------
// Round-trip property tests
// ---------------------------------------------------------------------

func randomAlphabetN(r *rand.Rand) byte {
	return alphabetChars[r.Intn(len(alphabetChars))]
}

func randomRSetChar(r *rand.Rand) byte {
	return rsetASCII[r.Intn(len(rsetASCII))]
}

// donorChars lists every character any replacement alphabet spends as a donor
// (Section 4.2): the bytes whose meaning depends on the segment's alphabet.
func donorChars() []byte {
	seen := map[byte]bool{}
	var out []byte
	for _, subs := range replacementAlphabets {
		for _, sub := range subs {
			if !seen[sub.donor] {
				seen[sub.donor] = true
				out = append(out, sub.donor)
			}
		}
	}
	return out
}

// genMixedInput builds a random byte slice mixing raw random bytes,
// Alphabet-N literal bytes, R-Set characters, and donor characters.
func genMixedInput(r *rand.Rand, length int) []byte {
	out := make([]byte, length)
	for i := range out {
		switch r.Intn(5) {
		case 0:
			out[i] = byte(r.Intn(256)) // raw random byte
		case 1:
			out[i] = randomAlphabetN(r) // literal Alphabet-N byte
		case 2:
			out[i] = randomRSetChar(r) // R-Set candidate
		case 3:
			donors := donorChars()
			out[i] = donors[r.Intn(len(donors))] // donor character
		default:
			out[i] = byte(r.Intn(256))
		}
	}
	return out
}

func TestRoundTripRandom(t *testing.T) {
	r := rand.New(rand.NewSource(42))

	lengths := []int{0, 1, 2, 3, 4, 5, 10, 19, 20, 21, 50, 100, 255, 256,
		511, 512, 513, 1000, 2000, 4096}

	for _, length := range lengths {
		length := length
		// Several independent random trials per length for both fully
		// random and text-ish (mixed) inputs.
		for trial := 0; trial < 5; trial++ {
			trial := trial
			t.Run(fmt.Sprintf("random_%d_%d", length, trial), func(t *testing.T) {
				data := make([]byte, length)
				r.Read(data)
				checkRoundTrip(t, data)
			})
			t.Run(fmt.Sprintf("mixed_%d_%d", length, trial), func(t *testing.T) {
				data := genMixedInput(r, length)
				checkRoundTrip(t, data)
			})
		}
	}
}

func checkRoundTrip(t *testing.T, data []byte) {
	t.Helper()
	encoded := Encode(data)
	decoded, err := Decode(encoded)
	if err != nil {
		t.Fatalf("Decode(Encode(data)) failed: %v\n  input len=%d: %x\n  encoded: %s", err, len(data), data, encoded)
	}
	if !bytes.Equal(decoded, data) {
		t.Fatalf("round-trip mismatch\n  input:   %x\n  encoded: %s\n  decoded: %x", data, encoded, decoded)
	}
}

// ---------------------------------------------------------------------
// Explicit edge cases
// ---------------------------------------------------------------------

func TestEdgeCases(t *testing.T) {
	t.Run("empty", func(t *testing.T) {
		checkRoundTrip(t, []byte{})
		if got := Encode(nil); got != "" {
			t.Errorf("Encode(nil) = %q, want empty string", got)
		}
	})

	t.Run("lengths_1_to_4", func(t *testing.T) {
		r := rand.New(rand.NewSource(1))
		for length := 1; length <= 4; length++ {
			data := make([]byte, length)
			r.Read(data)
			checkRoundTrip(t, data)
		}
	})

	t.Run("min_passthrough_boundary", func(t *testing.T) {
		// Literal text so that DP mode is a candidate; MIN_PASSTHROUGH_BYTES = 20.
		base := []byte("abcdefghijklmnopqrstuvwxyz0123456789")
		for _, n := range []int{19, 20, 21} {
			data := base[:n]
			checkRoundTrip(t, data)
		}
	})

	t.Run("multi_segment_dp", func(t *testing.T) {
		// A long literal run forces multiple DP signal segments
		// (MAX_DP_ANALYSIS_BYTES = 1024 bytes per signal).
		chunk := "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWX" // 62 chars, all literal Alphabet-N
		var sb strings.Builder
		for sb.Len() < 3000 {
			sb.WriteString(chunk)
		}
		data := []byte(sb.String())
		encoded := Encode(data)

		// Count DP signals (values >= 2^32) to confirm multiple segments were used.
		signals := countDPSignals(t, encoded)
		if signals < 2 {
			t.Errorf("expected multiple DP signal segments for %d-byte literal run, got %d signal(s); encoded=%s", len(data), signals, encoded)
		}
		checkRoundTrip(t, data)
	})

	t.Run("analysis_window_boundary", func(t *testing.T) {
		// A candidate prefix is capped at MAX_DP_ANALYSIS_BYTES, so exactly
		// that many representable bytes are one segment and one more needs a
		// second.
		exact := bytes.Repeat([]byte{'x'}, maxDPAnalysisBytes)
		if got, want := len(Encode(exact)), maxDPAnalysisBytes+5; got != want {
			t.Errorf("encoded length = %d, want %d", got, want)
		}
		checkRoundTrip(t, exact)
		checkRoundTrip(t, bytes.Repeat([]byte{'x'}, maxDPAnalysisBytes+1))
	})

	t.Run("literal_donor_breaks_run", func(t *testing.T) {
		// A literal donor character is representable under any alphabet that
		// does not spend it. With a space in the run, the alphabets that could
		// carry the space all spend '^' on it, so the run breaks at the '^'.
		for _, donor := range donorChars() {
			data := append(bytes.Repeat([]byte{'a'}, 25), ' ', donor, ' ')
			data = append(data, bytes.Repeat([]byte{'b'}, 25)...)
			checkRoundTrip(t, data)
		}
	})

	t.Run("every_alphabet_carries_its_rset_chars", func(t *testing.T) {
		for a, subs := range replacementAlphabets {
			if len(subs) == 0 {
				continue
			}
			var data []byte
			for len(data) < 3*minPassthroughBytes {
				for _, sub := range subs {
					data = append(data, rsetASCII[sub.j])
					data = append(data, []byte("word")...)
				}
			}
			t.Run(fmt.Sprintf("alphabet_%d", a), func(t *testing.T) {
				checkRoundTrip(t, data)
			})
		}
	})

	t.Run("all_byte_values", func(t *testing.T) {
		data := make([]byte, 256)
		for i := range data {
			data[i] = byte(i)
		}
		checkRoundTrip(t, data)
	})

	t.Run("every_rset_and_donor_char", func(t *testing.T) {
		var data []byte
		for _, b := range rsetASCII {
			data = append(data, b)
		}
		data = append(data, donorChars()...)
		data = append(data, []byte("padding_to_reach_minimum_length_threshold_for_dp_mode")...)
		checkRoundTrip(t, data)
	})

	t.Run("all_rset_chars_at_once_is_one_segment", func(t *testing.T) {
		// Only alphabet 7 substitutes all 13, so a run containing every one of
		// them can only be carried by that alphabet -- and must be.
		var data []byte
		for i := 0; i < 3; i++ {
			data = append(data, rsetASCII[:]...)
		}
		if got, want := len(Encode(data)), len(data)+5; got != want {
			t.Errorf("encoded length = %d, want %d", got, want)
		}
		checkRoundTrip(t, data)
	})
}

func countDPSignals(t *testing.T, encoded string) int {
	t.Helper()
	// Re-walk the encoded string the same way Decode does, counting how
	// many 5-char groups decode to a DP signal (value >= 2^32).
	cleaned := []byte(encoded)
	count := 0
	pos := 0
	n := len(cleaned)
	for pos+5 <= n {
		val, err := decode5(cleaned[pos:pos+5], pos)
		if err != nil {
			t.Fatalf("unexpected decode5 error while counting signals: %v", err)
		}
		pos += 5
		if val >= blockSignalBase {
			count++
			payload := val - blockSignalBase
			// Section 9: the length field is stored biased by one.
			length := int(payload&0x3FF) + 1
			pos += length
		}
	}
	return count
}

// ---------------------------------------------------------------------
// Decode error tests
// ---------------------------------------------------------------------

func TestDecodeErrors(t *testing.T) {
	cases := []struct {
		name    string
		input   string
		wantErr error
	}{
		{
			name:    "invalid_character",
			input:   "vp&Z", // '&' is not in Alphabet-N
			wantErr: ErrInvalidCharacter,
		},
		{
			name:    "invalid_character_in_partial_block",
			input:   "v&",
			wantErr: ErrInvalidCharacter,
		},
		{
			name:    "single_trailing_character",
			input:   "vpA.2v", // 5-char full group "vpA.2" then a lone "v"
			wantErr: ErrInvalidPartialBlock,
		},
		{
			// Spec 7.1: a trailing group is padded with '#' and the result must
			// be below 2^32. "%nSb" pads to 2^32-2 and decodes; "%nSc", the very
			// next group, pads to 2^32+83 and must not.
			name:    "partial_block_pads_over_2_32",
			input:   "%nSc",
			wantErr: ErrInvalidPartialBlock,
		},
		{
			name:    "partial_block_two_chars_pads_over_2_32",
			input:   "##",
			wantErr: ErrInvalidPartialBlock,
		},
		{
			name:    "partial_block_three_chars_pads_over_2_32",
			input:   "###",
			wantErr: ErrInvalidPartialBlock,
		},
		{
			name:    "dp_signal_declares_more_than_available",
			input:   makeSignal(t, 0, 400) + "hello", // declares 400 chars, only 5 follow
			wantErr: ErrUnexpectedEOF,
		},
		{
			name:    "dp_signal_length_bias_needs_its_one_character",
			input:   makeSignal(t, 0, 1), // declares 1 character, none follows
			wantErr: ErrUnexpectedEOF,
		},
		{
			name:    "reserved_signal_payload",
			input:   makeRawSignal(t, blockSignalBase+maxSignalPayload+1),
			wantErr: ErrReservedSignal,
		},
		{
			name:    "max_decoded_value_reserved",
			input:   strings.Repeat("#", 5), // decodes to 85^5-1, far above the valid signal range
			wantErr: ErrReservedSignal,
		},
	}

	for _, tc := range cases {
		tc := tc
		t.Run(tc.name, func(t *testing.T) {
			decoded, err := Decode(tc.input)
			if err == nil {
				t.Fatalf("Decode(%q) = %x, nil; want error %v", tc.input, decoded, tc.wantErr)
			}
			if !errors.Is(err, tc.wantErr) {
				t.Errorf("Decode(%q) error = %v; want error wrapping %v", tc.input, err, tc.wantErr)
			}
		})
	}
}

// TestDecodeNeverPanics feeds a variety of adversarial and randomly
// mutated strings into Decode and asserts it never panics (it may return
// an error, which is fine).
func TestDecodeNeverPanics(t *testing.T) {
	inputs := []string{
		"", "~", "~~", "#####", "@@@@@", strings.Repeat("~", 100),
		"vpA.2", "vpA.2v", "vpA.2vp", "vpA.2vpA",
		string([]byte{0x00, 0x01, 0x02}),
		"\t\n\r  \t\n\r",
	}

	r := rand.New(rand.NewSource(7))
	valid := Encode(genMixedInput(r, 300))
	inputs = append(inputs, valid)

	// Randomly mutate a valid encoded string to try to trigger edge cases.
	for i := 0; i < 200; i++ {
		mutated := []byte(valid)
		if len(mutated) == 0 {
			continue
		}
		mutations := r.Intn(4) + 1
		for m := 0; m < mutations; m++ {
			idx := r.Intn(len(mutated))
			switch r.Intn(3) {
			case 0:
				mutated[idx] = byte(r.Intn(256))
			case 1:
				mutated = append(mutated[:idx], mutated[idx+1:]...)
			case 2:
				mutated = append(mutated[:idx], append([]byte{byte(r.Intn(256))}, mutated[idx:]...)...)
			}
			if len(mutated) == 0 {
				break
			}
		}
		inputs = append(inputs, string(mutated))
	}

	for i, in := range inputs {
		func() {
			defer func() {
				if rec := recover(); rec != nil {
					t.Errorf("Decode panicked on input %d (%q): %v", i, in, rec)
				}
			}()
			_, _ = Decode(in)
		}()
	}
}

// makeSignal builds a valid DP signal (5-char group) for the given alphabet
// identifier and real segment character length, WITHOUT the segment data
// itself. Section 9 stores the length biased by one.
func makeSignal(t *testing.T, alphabet int, length int) string {
	t.Helper()
	payload := (uint64(alphabet) << 10) | uint64(length-1)
	digits := encode5(blockSignalBase + payload)
	return string(digits[:])
}

// makeRawSignal builds a 5-char group directly from a raw decodedValue,
// bypassing payload validation, for constructing intentionally invalid
// signals in tests.
func makeRawSignal(t *testing.T, value uint64) string {
	t.Helper()
	digits := encode5(value)
	return string(digits[:])
}
