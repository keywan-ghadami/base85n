// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package base85n

import (
	"bytes"
	"testing"
	"time"
)

// Guard against the quadratic encoder of spec Section 6.6.
//
// Pass 1 scans to the end of a representable run while the main loop can
// consume as little as 4 bytes of it, so an encoder that re-runs Pass 1 on
// every iteration is O(n^2). A buffer of escape characters is the worst
// case: Pass 2 gives up after 3 bytes every time.
//
// The time limit is deliberately loose. A linear encoder handles this input
// in milliseconds; the quadratic one this test exists to catch needed
// minutes, so any bound in between works and a generous one does not go
// flaky on a slow or loaded machine.

const (
	escapeDenseSize = 128 * 1024
	timeLimit       = 20 * time.Second
)

func TestEscapeDenseInputEncodesInLinearTime(t *testing.T) {
	data := bytes.Repeat([]byte{'~'}, escapeDenseSize)

	start := time.Now()
	encoded := Encode(data)
	elapsed := time.Since(start)

	decoded, err := Decode(encoded)
	if err != nil {
		t.Fatalf("Decode returned error: %v", err)
	}
	if !bytes.Equal(decoded, data) {
		t.Fatal("round trip mismatch")
	}
	if elapsed > timeLimit {
		t.Fatalf("encoding %d escape characters took %v; this is the signature of "+
			"the quadratic Pass 1 rescan that spec Section 6.6 forbids",
			escapeDenseSize, elapsed)
	}
}

func TestEscapeDenseGrowthIsNotQuadratic(t *testing.T) {
	timed := func(n int) time.Duration {
		data := bytes.Repeat([]byte{'~'}, n)
		start := time.Now()
		Encode(data)
		return time.Since(start)
	}

	timed(4096) // warm up

	small := timed(32 * 1024)
	large := timed(64 * 1024)

	// Linear predicts ~2x, quadratic predicts ~4x. A 3x ceiling rules out
	// quadratic growth without being sensitive to ordinary timing noise.
	if large > small*3 {
		t.Fatalf("doubling the input multiplied encoding time by %.1f (%v -> %v); "+
			"expected about 2 for a linear encoder",
			float64(large)/float64(small), small, large)
	}
}
