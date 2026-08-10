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
// Both tests here are timing-based, which on a shared CI runner means they
// have to be built to tolerate interference. Two things make them stable:
// every duration is the *minimum* of several runs, since scheduling noise
// only ever adds time and never removes it, and the thresholds sit far from
// the values a healthy encoder produces. A linear encoder handles the large
// case in milliseconds; the quadratic one these tests exist to catch needed
// minutes.

const (
	escapeDenseSize = 128 * 1024
	timeLimit       = 20 * time.Second

	// Sizes for the growth check, and how many times each is measured.
	smallSize = 32 * 1024
	largeSize = 64 * 1024
	repeats   = 5

	// Below this, a measurement is too short for its ratio to mean anything.
	measurable = time.Millisecond

	// Linear predicts ~2.0, quadratic ~4.0. Halfway between is the decision point.
	maxGrowth = 3.0
)

// bestEncodeTime returns the fastest of n encodes of an escape-dense buffer.
func bestEncodeTime(size, n int) time.Duration {
	data := bytes.Repeat([]byte{'~'}, size)
	best := time.Duration(1<<62 - 1)
	for i := 0; i < n; i++ {
		start := time.Now()
		Encode(data)
		if d := time.Since(start); d < best {
			best = d
		}
	}
	return best
}

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
	bestEncodeTime(4096, 1) // warm up

	small := bestEncodeTime(smallSize, repeats)
	large := bestEncodeTime(largeSize, repeats)

	if small < measurable {
		return // too fast to time meaningfully; the ceiling test still applies
	}

	growth := float64(large) / float64(small)
	if growth >= maxGrowth {
		t.Fatalf("doubling the input multiplied encoding time by %.1f (%v -> %v); "+
			"expected about 2 for a linear encoder", growth, small, large)
	}
}
