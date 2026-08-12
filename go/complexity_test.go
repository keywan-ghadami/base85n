// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package base85n

import (
	"bytes"
	"math/rand"
	"testing"
	"time"
)

// Guard against the rescanning encoder of spec Section 6.6.
//
// Step 1 scans up to MAX_DP_ANALYSIS_BYTES bytes for each of the eight
// alphabets, while step 2.b may consume as few as 4 bytes, so an encoder that
// redoes those scans every iteration performs 2048 byte inspections per input
// byte. Bounded lookahead keeps that linear rather than quadratic -- unlike
// version 0.2.0 -- but a constant factor of 2048 is still what Section 6.6
// exists to prevent. Pseudorandom bytes are the worst case: no alphabet
// reaches MIN_PASSTHROUGH_BYTES, so every iteration takes the block-mode
// branch and advances 4 bytes.
//
// Both tests here are timing-based, which on a shared CI runner means they
// have to be built to tolerate interference. Two things make them stable:
// every duration is the *minimum* of several runs, since scheduling noise
// only ever adds time and never removes it, and the thresholds sit far from
// the values a healthy encoder produces.

const (
	scanDenseSize = 128 * 1024
	timeLimit     = 20 * time.Second

	// Sizes for the growth check, and how many times each is measured.
	smallSize = 32 * 1024
	largeSize = 64 * 1024
	repeats   = 5

	// Below this, a measurement is too short for its ratio to mean anything.
	measurable = time.Millisecond

	// Linear predicts ~2.0, quadratic ~4.0. Halfway between is the decision point.
	maxGrowth = 3.0
)

// scanDense builds input on which no alphabet ever reaches
// MIN_PASSTHROUGH_BYTES, so every iteration takes the block-mode branch.
func scanDense(size int) []byte {
	r := rand.New(rand.NewSource(int64(size) ^ 0x5CA4DE45))
	data := make([]byte, size)
	r.Read(data)
	return data
}

// bestEncodeTime returns the fastest of n encodes of a scan-dense buffer.
func bestEncodeTime(size, n int) time.Duration {
	data := scanDense(size)
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

func TestScanDenseInputEncodesInLinearTime(t *testing.T) {
	data := scanDense(scanDenseSize)

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
		t.Fatalf("encoding %d scan-dense bytes took %v; this is the signature of "+
			"the per-iteration rescan that spec Section 6.6 forbids",
			scanDenseSize, elapsed)
	}
}

func TestScanDenseGrowthIsNotQuadratic(t *testing.T) {
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
