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

// donorChars lists every character any profile can spend as a donor
// (Section 4.2): the bytes whose meaning depends on the segment's profile and
// mask.
func donorChars() []byte {
	seen := map[byte]bool{}
	var out []byte
	for _, profile := range profiles {
		for _, donor := range profile {
			if !seen[donor] {
				seen[donor] = true
				out = append(out, donor)
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
		// (MAX_DP_ANALYSIS_BYTES = 2048 bytes per signal).
		chunk := "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWX" // 62 chars, all literal Alphabet-N
		var sb strings.Builder
		for sb.Len() < 5000 {
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
		// second. The bytes have to vary: a run of identical ones is a Fill
		// segment long before the window fills up.
		exact := variedBytes(maxDPAnalysisBytes)
		if got, want := len(Encode(exact)), maxDPAnalysisBytes+5; got != want {
			t.Errorf("encoded length = %d, want %d", got, want)
		}
		checkRoundTrip(t, exact)
		checkRoundTrip(t, variedBytes(maxDPAnalysisBytes+1))
	})

	t.Run("literal_donor_breaks_run", func(t *testing.T) {
		// A literal donor character is representable while the segment does not
		// spend it. With a space in the run every profile pays for the space
		// with its rank-0 donor, so the scan either moves to a profile that
		// ranks the literal beyond k, or breaks the segment.
		for _, donor := range donorChars() {
			data := append(variedBytes(25), ' ', donor, ' ')
			data = append(data, variedBytes(25)...)
			checkRoundTrip(t, data)
		}
	})

	t.Run("every_rset_char_round_trips", func(t *testing.T) {
		for j, r := range rsetASCII {
			var data []byte
			for len(data) < 3*minPassthroughBytes {
				data = append(data, []byte("word")...)
				data = append(data, r)
			}
			t.Run(fmt.Sprintf("rset_%d", j), func(t *testing.T) {
				checkRoundTrip(t, data)
			})
		}
	})

	t.Run("fill_thresholds", func(t *testing.T) {
		for _, b := range []byte{0, ' ', 'a', 0xff} {
			// One below the threshold is block mode; at it and at the cap, one
			// signal carries the whole run.
			if got, want := len(Encode(bytes.Repeat([]byte{b}, minFillBytes-1))), 5; got != want {
				t.Errorf("byte %#x below threshold: encoded length = %d, want %d", b, got, want)
			}
			for _, n := range []int{minFillBytes, minFillBytes + 1, maxFillBytes} {
				data := bytes.Repeat([]byte{b}, n)
				if got, want := len(Encode(data)), 5; got != want {
					t.Errorf("byte %#x run of %d: encoded length = %d, want %d", b, n, got, want)
				}
				checkRoundTrip(t, data)
			}
			// One past the cap needs a second signal for the leftover byte.
			over := bytes.Repeat([]byte{b}, maxFillBytes+1)
			if got, want := len(Encode(over)), 7; got != want {
				t.Errorf("byte %#x run of %d: encoded length = %d, want %d", b, maxFillBytes+1, got, want)
			}
			checkRoundTrip(t, over)
		}
	})

	t.Run("fill_interrupts_passthrough", func(t *testing.T) {
		data := append(variedBytes(40), bytes.Repeat([]byte{'='}, 300)...)
		data = append(data, variedBytes(40)...)
		// 5+40 for the first segment, 5 for the run, 5+40 for the second.
		if got, want := len(Encode(data)), 5+40+5+5+40; got != want {
			t.Errorf("encoded length = %d, want %d", got, want)
		}
		checkRoundTrip(t, data)
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
		// k reaches 13, so a whole profile is spent and the segment can hold no
		// literal from it -- but it is still one segment.
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
		if val >= dpSignalBase && val < fillSignalBase {
			count++
			payload := val - dpSignalBase
			// Section 9: the length field is stored biased by one.
			length := int(payload&0x7FF) + 1
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
			wantErr: ErrInvalidFinalBlock,
		},
		{
			// Section 7.5: a trailing group must be the canonical encoding of
			// the bytes it decodes to. "%nSb" pads to 2^32-2 and would decode to
			// ff ff ff, but those bytes encode as "%nS9" -- so it is an alias,
			// and rejected.
			name:    "final_block_alias",
			input:   "%nSb",
			wantErr: ErrInvalidFinalBlock,
		},
		{
			name:    "final_block_pads_over_2_32",
			input:   "%nSc",
			wantErr: ErrInvalidFinalBlock,
		},
		{
			name:    "final_block_two_chars_pads_over_2_32",
			input:   "##",
			wantErr: ErrInvalidFinalBlock,
		},
		{
			name:    "final_block_three_chars_pads_over_2_32",
			input:   "###",
			wantErr: ErrInvalidFinalBlock,
		},
		{
			name:    "dp_signal_declares_more_than_available",
			input:   makeSignal(t, 0, 0, 400) + "hello", // declares 400 chars, only 5 follow
			wantErr: ErrUnexpectedEOF,
		},
		{
			name:    "dp_signal_length_bias_needs_its_one_character",
			input:   makeSignal(t, 0, 0, 1), // declares 1 character, none follows
			wantErr: ErrUnexpectedEOF,
		},
		{
			name:    "future_signal_space",
			input:   makeRawSignal(t, futureSignalBase),
			wantErr: ErrUndefinedSignal,
		},
		{
			name:    "max_decoded_value_undefined",
			input:   strings.Repeat("#", 5), // decodes to 85^5-1, the top of the future space
			wantErr: ErrUndefinedSignal,
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

// makeSignal builds a valid DP signal (5-char group) for the given profile,
// mask and real segment character length, WITHOUT the segment data itself.
// Section 9 stores the length biased by one.
func makeSignal(t *testing.T, profile int, mask uint16, length int) string {
	t.Helper()
	payload := (uint64(profile) << 24) | (uint64(mask) << 11) | uint64(length-1)
	digits := encode5(dpSignalBase + payload)
	return string(digits[:])
}

// variedBytes returns n bytes that cycle through the lowercase letters, so no
// run of identical bytes ever reaches the Fill threshold.
func variedBytes(n int) []byte {
	out := make([]byte, n)
	for i := range out {
		out[i] = byte('a' + i%26)
	}
	return out
}

// makeRawSignal builds a 5-char group directly from a raw decodedValue,
// bypassing payload validation, for constructing intentionally invalid
// signals in tests.
func makeRawSignal(t *testing.T, value uint64) string {
	t.Helper()
	digits := encode5(value)
	return string(digits[:])
}
