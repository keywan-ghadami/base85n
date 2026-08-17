// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Package base85n implements the Base85N binary-to-text encoding scheme: a
// Base85 variant using a single, protocol-friendly 85-character alphabet
// (Alphabet-N) plus two adaptive modes.
//
// Dynamic Passthrough (DP) carries a run of text-like input at exactly one
// output character per input byte. Its signal names which "R-Set" characters
// (space, newline, quote, comma, ...) the segment contains and which donor
// profile lends the Alphabet-N characters that stand in for them, so the
// substitution is built per segment and needs no escape mechanism.
//
// Fill carries a run of up to 2048 identical bytes in the five characters of
// its signal alone, or a short zero run together with the two bytes beside it.
//
// See the specification in spec/ (base85n-v0.5.0.md) for the full formal
// description.
package base85n

import (
	"encoding/binary"
	"errors"
	"fmt"
	"math/bits"
	"slices"
	"strings"
)

// ---------------------------------------------------------------------
// Alphabet-N, R-Set and donor profiles (spec Section 4)
// ---------------------------------------------------------------------

const alphabetChars = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ.-:+=^!/*?`_~()[]{}@%$#"

// rsetASCII holds the ASCII value of R-Set character j (Section 4.1), indexed
// by j (0-12). Its indices are normative: they fix the bit positions in a
// segment's mask.
var rsetASCII = [13]byte{32, 34, 39, 44, 59, 92, 124, 60, 62, 38, 9, 10, 13}

const (
	// rsetLen is the size of the R-Set, and the width of the mask field.
	rsetLen = 13
	// numProfiles is the number of donor profiles, and the range of the
	// signal's 3-bit profile identifier.
	numProfiles = 8
)

// profiles holds the eight donor profiles of Section 4.2: each an ordered
// sequence of 13 distinct Alphabet-N characters.
//
// A segment whose mask has k bits set spends the profile's first k characters,
// in mask-bit order, as the stand-ins for the R-Set characters that occur in
// it. Only those first k become unrepresentable, which is why a profile is a
// ranking and not an alphabet.
var profiles = [numProfiles][rsetLen]byte{
	{'~', '^', '?', '%', '@', '+', '`', '$', '#', '!', '*', '.', '-'},
	{'~', '^', '+', '[', ']', '`', '?', '@', '!', '%', '#', '*', '('},
	{'^', '~', '$', '#', '?', '%', '!', '`', '@', '[', ']', '+', '_'},
	{'~', '+', '?', '%', '@', '!', '^', '[', ']', ':', '`', '(', ')'},
	{'~', '%', '^', '`', '+', '?', '!', '$', '@', '(', ')', '{', '}'},
	{'^', '~', '?', '@', '!', '+', '%', '*', '$', '(', ')', '_', '#'},
	{'^', '~', '@', '%', '?', '$', '+', '!', '#', '[', ']', '=', '*'},
	{'^', '$', '~', '@', '?', '!', '%', '`', '[', ']', ':', '}', '{'},
}

// charToValue maps an ASCII byte to its Alphabet-N integer value (0-84), or
// -1 if the byte is not part of Alphabet-N.
var charToValue [256]int16

// rsetIndex maps an ASCII byte to its R-Set index j (0-12), or -1.
var rsetIndex [256]int8

// rankPacked[b] holds the rank byte b would occupy in each profile, one profile
// per byte lane, lane p carrying profile p. A character absent from a profile
// ranks rankAbsent there, one past the last real rank, so "absent" and "ranked
// below no possible k" are the same value. Bytes that are neither Alphabet-N
// nor R-Set carry notRepresentable instead, which the scan tests for before
// using the entry.
var rankPacked [256]uint64

const (
	rankAbsent       uint64 = rsetLen
	rankAbsentAll    uint64 = 0x0d0d0d0d0d0d0d0d
	notRepresentable uint64 = ^uint64(0)
)

// decBase is the decoding table for a DP segment before its donors are patched
// in: an Alphabet-N character stands for its own byte value, anything else is
// invalid.
var decBase [256]uint16

const decInvalid uint16 = 0x8000

// These are byte-indexed arrays rather than maps because every input byte is
// looked up in them in the encoder's hot path: a Go map lookup hashes the key
// and chases a bucket, which dominated encoding time. They are derived from
// alphabetChars, rsetASCII and profiles at startup rather than written out as
// literals, so there is no second copy of Section 4 to keep in step.
func init() {
	for i := range charToValue {
		charToValue[i] = -1
		rsetIndex[i] = -1
		rankPacked[i] = notRepresentable
	}
	for v, c := range []byte(alphabetChars) {
		charToValue[c] = int16(v)
	}
	for j, c := range rsetASCII {
		rsetIndex[c] = int8(j)
	}
	for b := 0; b < 256; b++ {
		if charToValue[b] >= 0 || rsetIndex[b] >= 0 {
			var packed uint64
			for p := 0; p < numProfiles; p++ {
				rank := rankAbsent
				for r, c := range profiles[p] {
					if int(c) == b {
						rank = uint64(r)
					}
				}
				packed |= rank << (8 * uint(p))
			}
			rankPacked[b] = packed
		}
		if charToValue[b] < 0 {
			decBase[b] = decInvalid | uint16(b)
		} else {
			decBase[b] = uint16(b)
		}
	}
}

// donorTable derives the substitution of Section 4.3: the set bits of mask
// consume the first k characters of the profile, lowest bit taking rank 0.
// It returns the (R-Set byte, donor character) pairs and k.
func donorTable(profile int, mask uint16) ([rsetLen][2]byte, int) {
	var pairs [rsetLen][2]byte
	rank := 0
	for j := 0; j < rsetLen; j++ {
		if mask&(1<<uint(j)) != 0 {
			pairs[rank] = [2]byte{rsetASCII[j], profiles[profile][rank]}
			rank++
		}
	}
	return pairs, rank
}

// isIgnorableWS reports whether c is one of the four inter-token whitespace
// characters Section 7.1 allows between Base85N constructs.
func isIgnorableWS(c byte) bool {
	return c == ' ' || c == '\t' || c == '\n' || c == '\r'
}

// ---------------------------------------------------------------------
// Constants (Sections 6.4 and 9)
// ---------------------------------------------------------------------

const (
	// maxDPAnalysisBytes bounds how much input is examined per DP decision,
	// and therefore how many bytes one DP segment can carry.
	maxDPAnalysisBytes = 2048
	// maxDPSegmentChars equals maxDPAnalysisBytes because the transformation
	// is 1:1, and matches the signal's 11-bit length field.
	maxDPSegmentChars   = 2048
	minPassthroughBytes = 20

	// minFillBytes is the shortest run a Fill signal is spent on. At four bytes
	// block mode also costs five characters; at five, Fill is ahead.
	minFillBytes = 5
	// minFillInSegmentBytes is the shortest run that ends a DP segment. Inside
	// passthrough text a run already costs one character per byte, so breaking
	// out to a Fill signal also costs the signal that resumes passthrough
	// afterwards. Ratio alone puts the break-even at eleven, but the threshold
	// also decides how much text stays readable, how many substitution tables a
	// decoder rebuilds, and how often the scan rolls back; sixteen is the top of
	// the plateau where ratio is unchanged and those three are at their best.
	minFillInSegmentBytes = 16
	maxFillBytes          = 2048

	// minTailZeros is the shortest zero run Fill carries a tail on: two zeros
	// and two literals are the four bytes block mode also spends five
	// characters on. maxTailZeros matches the variant's 5-bit length field.
	minTailZeros = 3
	maxTailZeros = 32

	dpSignalBase     uint64 = 1 << 32                    // block mode occupies 0 .. 2^32
	fillSignalBase   uint64 = dpSignalBase + (1 << 27)   // 3 profile + 13 mask + 11 length bits
	tailSignalBase   uint64 = fillSignalBase + (1 << 19) // 8 byte-value + 11 length bits
	futureSignalBase uint64 = tailSignalBase + (1 << 22) // 16 literal + 5 length + 1 order bit

	laneHi   uint64 = 0x8080808080808080
	laneOnes uint64 = 0x0101010101010101

	pow85_2 uint32 = 7225     // 85^2
	pow85_3 uint32 = 614125   // 85^3
	pow85_4 uint64 = 52200625 // 85^4
)

// ---------------------------------------------------------------------
// Errors (Section 10)
// ---------------------------------------------------------------------

var (
	// ErrInvalidCharacter indicates a byte encountered while decoding that is
	// not part of Alphabet-N (outside of allowed inter-token whitespace).
	ErrInvalidCharacter = errors.New("base85n: invalid character")

	// ErrUnexpectedEOF indicates the input ended before a full 5-character
	// group, or before a segment's declared length, could be read.
	ErrUnexpectedEOF = errors.New("base85n: unexpected end of stream")

	// ErrUndefinedSignal indicates a decoded 5-character group whose value
	// falls in FUTURE_SIGNAL_SPACE, above every signal this version defines.
	ErrUndefinedSignal = errors.New("base85n: undefined signal value")

	// ErrInvalidFinalBlock indicates a malformed trailing group: a single
	// leftover character, a padded value that does not fit in 32 bits, or
	// characters that are not the canonical encoding of the bytes they decode
	// to.
	ErrInvalidFinalBlock = errors.New("base85n: invalid final block")
)

// DecodeError provides positional context for a decoding failure. It always
// wraps one of the sentinel errors declared in this package (ErrInvalidCharacter,
// ErrUnexpectedEOF, ErrUndefinedSignal, ErrInvalidFinalBlock), so callers can
// use errors.Is to distinguish error conditions.
type DecodeError struct {
	// Offset is the index (in characters, after inter-token whitespace has
	// been stripped) at which the error was detected.
	Offset int
	// Err is the underlying sentinel error describing the error condition.
	Err error
}

func (e *DecodeError) Error() string {
	return fmt.Sprintf("base85n: %v at offset %d", e.Err, e.Offset)
}

func (e *DecodeError) Unwrap() error { return e.Err }

func newDecodeError(offset int, sentinel error, format string, args ...any) error {
	return &DecodeError{
		Offset: offset,
		Err:    fmt.Errorf("%s: %w", fmt.Sprintf(format, args...), sentinel),
	}
}

// ---------------------------------------------------------------------
// Base85 digit conversion (Section 8)
// ---------------------------------------------------------------------

// encode5 converts an integer value (0 <= value < 85^5) into its 5
// Alphabet-N characters (big-endian digit order, most significant first).
func encode5(value uint64) [5]byte {
	var digits [5]byte
	v := value
	for i := 4; i >= 0; i-- {
		digits[i] = alphabetChars[v%85]
		v /= 85
	}
	return digits
}

// decode5 converts 5 Alphabet-N characters into their combined integer value,
// validating each character.
func decode5(chars []byte, offset int) (uint64, error) {
	var val uint64
	for i, c := range chars {
		v := charToValue[c]
		if v < 0 {
			return 0, newDecodeError(offset+i, ErrInvalidCharacter, "invalid character %q", rune(c))
		}
		val = val*85 + uint64(v)
	}
	return val, nil
}

// ---------------------------------------------------------------------
// Lane arithmetic, for the eight profiles the prefix scan tracks at once
// ---------------------------------------------------------------------

// laneGE sets bit 7 of lane p iff x's lane p is >= y's. Setting each lane's
// high bit before subtracting keeps the lane difference in 1..255 whenever both
// operands are below 128, so no lane can borrow from the next and the high bit
// is left holding the comparison.
func laneGE(x, y uint64) uint64 { return ((x | laneHi) - y) & laneHi }

// laneMin is the lane-wise minimum of two packings of values below 128.
func laneMin(x, y uint64) uint64 {
	// 0xFF in each lane where x is the larger, 0 elsewhere: the shifted
	// comparison bit sits at the lane's low bit, and multiplying by 0xFF fills
	// that lane and only that lane.
	m := (laneGE(x, y) >> 7) * 0xFF
	return (x &^ m) | (y & m)
}

// ---------------------------------------------------------------------
// Encoding (Section 6)
// ---------------------------------------------------------------------

// Encode converts data into its Base85N string representation.
func Encode(data []byte) string {
	var sb strings.Builder
	// Block mode is 5 characters per 4 bytes, and neither other mode ever costs
	// more, so this is an upper bound rather than an estimate.
	sb.Grow(len(data)*5/4 + 16)

	n := len(data)
	i := 0
	// Start of the pending run of block-mode bytes, or -1 for none. Consecutive
	// block-mode iterations are converted in one call instead of four bytes at a
	// time, which does not change the output: block mode consumes exactly one
	// 4-byte group per iteration, and block mode over a whole number of groups is
	// the concatenation of the per-group results.
	blockStart := -1

	for i < n {
		// Step 1: a run worth a signal of its own -- either variant of Fill.
		// Five characters for up to 2048 identical bytes, or for a short zero
		// run together with the two bytes beside it, which block mode would
		// otherwise charge 1.25 characters each for. Both cost the same five
		// characters, so the one that covers more bytes wins and a tie goes to
		// the solid variant (Section 6.5).
		{
			window := data[i:]
			run := fillRun(window)
			cover, signal := 0, uint64(0)
			if run >= minFillBytes {
				cover = run
				signal = fillSignalBase + (uint64(window[0]) << 11) + uint64(run-1)
			}
			if window[0] == 0 {
				// The run just counted is the zero run.
				zeros := min(run, maxTailZeros)
				if zeros >= minTailZeros && zeros+2 <= len(window) && zeros+2 > cover {
					cover = zeros + 2
					signal = tailSignal(zeros, 0, window[zeros], window[zeros+1])
				}
			}
			if len(window) >= 3 && window[2] == 0 {
				zeros := zeroRun(window[2:])
				if zeros >= minTailZeros && zeros+2 > cover {
					cover = zeros + 2
					signal = tailSignal(zeros, 1, window[0], window[1])
				}
			}
			if cover > 0 {
				if blockStart >= 0 {
					processBlockMode(data[blockStart:i], &sb)
					blockStart = -1
				}
				digits := encode5(signal)
				sb.Write(digits[:])
				i += cover
				continue
			}
		}

		// Steps 2 and 3.
		length, mask, profile := scanDP(data[i:])
		if length >= minPassthroughBytes {
			// At minPassthroughBytes the two modes cost the same 25 characters
			// and DP only gains from there, so the length test settles step 3's
			// size comparison too.
			if blockStart >= 0 {
				processBlockMode(data[blockStart:i], &sb)
				blockStart = -1
			}
			payload := (uint64(profile) << 24) | (uint64(mask) << 11) | uint64(length-1)
			digits := encode5(dpSignalBase + payload)
			sb.Write(digits[:])

			// Section 4.3's substitution, as a table: the identity over ASCII
			// with the segment's k donors patched in. Every byte a DP segment can
			// carry is ASCII, so 128 entries cover it.
			var xlat [128]byte
			for b := range xlat {
				xlat[b] = byte(b)
			}
			pairs, k := donorTable(profile, mask)
			for _, pair := range pairs[:k] {
				xlat[pair[0]] = pair[1]
			}
			for _, b := range data[i : i+length] {
				sb.WriteByte(xlat[b&0x7f])
			}
			i += length
			continue
		}

		// Step 4, block-mode fallback: exactly one 4-byte group, however long the
		// failed candidate was. Nothing but the end of the input can hand
		// processBlockMode a partial group this way.
		if blockStart < 0 {
			blockStart = i
		}
		if n-i < 4 {
			i = n
		} else {
			i += 4
		}

		// Every position up to the next decision point takes this same branch,
		// so jump to it rather than re-deciding every four bytes.
		//
		// The gate is what keeps the lookahead off the path it cannot help:
		// where the next byte is representable, a DP candidate starts right
		// here and the scan the loop is about to run is the cheaper way to
		// find out how far it reaches. Where it is not, the lookahead runs
		// over binary, which is exactly where it earns its keep.
		if i < n && !representable(data[i]) {
			i += ((nextDecisionPoint(data, i) - i) / 4) * 4
		}
	}

	if blockStart >= 0 {
		processBlockMode(data[blockStart:i], &sb)
	}

	return sb.String()
}

// representable reports whether a byte is one a DP segment could carry:
// Alphabet-N or R-Set. It is the only question nextDecisionPoint asks.
func representable(b byte) bool {
	return charToValue[b] >= 0 || rsetIndex[b] >= 0
}

// nextDecisionPoint returns the next position at or after from where the main
// loop could take a branch other than block mode, given that it is inside a
// block-mode run and therefore only ever *visits* positions from, from+4,
// from+8, ...
//
// Only those positions have to be tested, and at each of them the three tests
// are exact rather than heuristic: a Fill segment starts there iff minFillBytes
// equal bytes do or minTailZeros zeros do (at the position itself or two bytes
// in), and a DP segment can only start there if minPassthroughBytes
// representable bytes do. All bail out on their first counterexample, which on
// high-entropy input is the second byte they read -- so the whole test costs a
// handful of loads per 4 bytes consumed, where running the real scans costs an
// order of magnitude more.
//
// The caller may jump straight to the returned position: every position it
// passes over would have taken step 4 and consumed exactly 4 bytes, and block
// mode over a whole number of groups is the concatenation of the per-group
// results, so the output is unchanged.
func nextDecisionPoint(data []byte, from int) int {
	n := len(data)
	for q := from; q < n; q += 4 {
		// A Fill with a tail, in either order. Both need a zero at q+2 --
		// three zeros have to reach it whether they start at q or at q+2 --
		// so one load gates both scans.
		if q+2 < n && data[q+2] == 0 {
			if zerosAt(data, q, minTailZeros) && q+minTailZeros+2 <= n {
				return q
			}
			if zerosAt(data, q+2, minTailZeros) {
				return q
			}
		}
		if q+1 < n && data[q+1] == data[q] {
			limit := min(n, q+minFillBytes)
			e := q + 1
			for e < limit && data[e] == data[q] {
				e++
			}
			if e-q >= minFillBytes {
				return q
			}
		}
		if representable(data[q]) {
			limit := min(n, q+minPassthroughBytes)
			e := q
			for e < limit && representable(data[e]) {
				e++
			}
			if e-q >= minPassthroughBytes {
				return q
			}
		}
	}
	return n
}

// tailSignal is the Fill signal value for the tail variant: zeros zero bytes
// and two literals, in the order the top payload bit names (Section 9).
func tailSignal(zeros, order int, lit0, lit1 byte) uint64 {
	payload := uint64(order)<<21 | uint64(zeros-1)<<16 | uint64(lit0)<<8 | uint64(lit1)
	return tailSignalBase + payload
}

// zeroRun returns the run of zero bytes at window[0], capped where the tail
// variant's 5-bit length field saturates.
func zeroRun(window []byte) int {
	limit := min(len(window), maxTailZeros)
	i := 0
	for i < limit && window[i] == 0 {
		i++
	}
	return i
}

// zerosAt reports whether data has want zero bytes starting at s.
func zerosAt(data []byte, s, want int) bool {
	if s+want > len(data) {
		return false
	}
	for _, b := range data[s : s+want] {
		if b != 0 {
			return false
		}
	}
	return true
}

// fillRun returns the length of the run of identical bytes starting at
// window[0], capped at maxFillBytes (Section 6.1, step 1).
func fillRun(window []byte) int {
	b := window[0]
	limit := len(window)
	if limit > maxFillBytes {
		limit = maxFillBytes
	}
	i := 1
	for i < limit && window[i] == b {
		i++
	}
	return i
}

// scanDP performs the Dynamic Passthrough prefix scan of Section 6.2: the
// longest prefix of window that one profile can carry, with the mask and
// profile in effect for it. The state returned is the one in effect *before*
// the byte that ended the scan, and the length is capped at
// maxDPAnalysisBytes.
//
// The scan tracks, per profile, the lowest rank any literal Alphabet-N
// character has held in it -- eight numbers, one per byte lane of a uint64.
// A profile stays viable exactly while that number is at least k, the count of
// R-Set characters the mask names, so the per-byte update is two table lookups
// and a handful of arithmetic, whatever the state of the eight.
//
// It also stops where a run of minFillInSegmentBytes identical bytes begins, so
// that Fill can reach runs inside passthrough text (Section 6.5, rule 1). The
// rolled-back state is what that costs: a run's first byte may have widened the
// mask or narrowed the profile choice, and the bytes after it cannot have
// changed anything, being equal to a byte already accounted for.
func scanDP(window []byte) (length int, mask uint16, profile int) {
	limit := len(window)
	if limit > maxDPAnalysisBytes {
		limit = maxDPAnalysisBytes
	}

	var k uint64
	minDonor := rankAbsentAll

	// The state as it stood before the most recent change, and where that change
	// happened. At most 26 changes can occur in a segment, so this costs nothing
	// per byte.
	prevMask, prevProfile, prevPos := uint16(0), 0, -1

	// Length of the run of identical bytes ending just before i.
	run := 0

	i := 0
	for i < limit {
		b := window[i]

		if i > 0 && b == window[i-1] {
			run++
			if run+1 >= minFillInSegmentBytes {
				start := i - run
				if prevPos == start {
					return start, prevMask, prevProfile
				}
				return start, mask, profile
			}
		} else {
			run = 0
		}

		if j := rsetIndex[b]; j >= 0 {
			bit := uint16(1) << uint(j)
			if mask&bit != 0 {
				i++ // already named by the mask; nothing changes
				continue
			}
			// One more donor to spend: every profile whose lowest literal rank
			// has been reached now drops out.
			viable := laneGE(minDonor, (k+1)*laneOnes)
			if viable == 0 {
				break
			}
			prevMask, prevProfile, prevPos = mask, profile, i
			profile = bits.TrailingZeros64(viable) >> 3
			mask |= bit
			k++
		} else {
			ranks := rankPacked[b]
			if ranks == notRepresentable {
				break // not representable under any mask or profile
			}
			newMin := laneMin(minDonor, ranks)
			if newMin == minDonor {
				i++ // ranks below nothing already seen; viability is unchanged
				continue
			}
			viable := laneGE(newMin, k*laneOnes)
			if viable == 0 {
				break
			}
			prevMask, prevProfile, prevPos = mask, profile, i
			profile = bits.TrailingZeros64(viable) >> 3
			minDonor = newMin
		}
		i++
	}

	return i, mask, profile
}

// processBlockMode implements Section 6.3: full 4-byte blocks are each
// converted to 5 Alphabet-N characters, and any trailing 1-3 bytes are
// zero-padded, converted, and truncated to the first 2-4 characters.
func processBlockMode(data []byte, sb *strings.Builder) {
	i := 0
	n := len(data)
	for i+4 <= n {
		val := uint64(binary.BigEndian.Uint32(data[i : i+4]))
		digits := encode5(val)
		sb.Write(digits[:])
		i += 4
	}
	rem := n - i
	if rem > 0 {
		var b4 [4]byte
		copy(b4[:], data[i:])
		val := uint64(binary.BigEndian.Uint32(b4[:]))
		digits := encode5(val)
		sb.Write(digits[:rem+1])
	}
}

// ---------------------------------------------------------------------
// Decoding (Section 7)
// ---------------------------------------------------------------------

// Decode parses a Base85N string and returns the original bytes it
// represents, or a non-nil error (typically a *DecodeError wrapping one of
// the sentinel errors declared in this package) describing the first
// decoding failure encountered.
func Decode(s string) ([]byte, error) {
	// len(s) is the exact bound for every construct but Solid Fill, which is
	// also the only one that can grow the buffer.
	out, err := decodeScan(s, make([]byte, 0, len(s)))

	// Section 7.1 has the decoder ignore inter-token whitespace. Rather than
	// copy every input to strip characters that a valid stream never contains,
	// take the rejection as the signal: none of the four whitespace bytes is in
	// Alphabet-N, and decodeScan validates every character it consumes, so a
	// stream with whitespace in it can never decode successfully. Only once it
	// has failed is it worth building the filtered copy and decoding again.
	//
	// The retry is on any failure, not just ErrInvalidCharacter: whitespace also
	// shifts the group boundaries after it, so it can equally well surface as a
	// truncated final group or a short DP segment.
	if err != nil {
		if i := strings.IndexAny(s, " \t\n\r"); i >= 0 {
			filtered := make([]byte, 0, len(s))
			filtered = append(filtered, s[:i]...)
			for k := i; k < len(s); k++ {
				if !isIgnorableWS(s[k]) {
					filtered = append(filtered, s[k])
				}
			}
			out, err = decodeScan(filtered, out[:0])
		}
		if err != nil {
			return nil, err
		}
	}

	return out, nil
}

// decodeScan decodes in, which must already be free of the inter-token
// whitespace Section 7.1 allows, appending to out and returning the result.
//
// It is generic over string and []byte so that the first pass can read the
// caller's string without copying it and the retry can read the filtered bytes.
func decodeScan[T string | []byte](in T, out []byte) ([]byte, error) {
	pos := 0
	n := len(in)

	for pos < n {
		remaining := n - pos

		if remaining >= 5 {
			groupOffset := pos
			// charToValue is -1 for every byte outside Alphabet-N, so one sign
			// test on the OR of the five covers all of them.
			v0 := charToValue[in[pos]]
			v1 := charToValue[in[pos+1]]
			v2 := charToValue[in[pos+2]]
			v3 := charToValue[in[pos+3]]
			v4 := charToValue[in[pos+4]]
			if (v0 | v1 | v2 | v3 | v4) < 0 {
				return nil, firstInvalidChar(in, pos, 5)
			}
			// Horner's rule would chain five multiplies end to end; weighing the
			// digits directly leaves them independent. Only the top term can
			// leave 32 bits.
			val := uint64(v0)*pow85_4 +
				uint64(uint32(v1)*pow85_3+uint32(v2)*pow85_2+uint32(v3)*85+uint32(v4))
			pos += 5

			if val < dpSignalBase {
				out = binary.BigEndian.AppendUint32(out, uint32(val))
				continue
			}

			if val >= futureSignalBase {
				return nil, newDecodeError(groupOffset, ErrUndefinedSignal,
					"group value %d is in FUTURE_SIGNAL_SPACE", val)
			}

			if val >= tailSignalBase {
				// Section 7.4, tail variant: zeros and two literals, in the
				// order the payload's top bit names. No characters are read to
				// construct any of it either.
				payload := val - tailSignalBase
				zeros := int((payload>>16)&0x1F) + 1
				lit0 := byte((payload >> 8) & 0xFF)
				lit1 := byte(payload & 0xFF)
				out = slices.Grow(out, zeros+2)
				seg := out[len(out) : len(out)+zeros+2]
				for k := range seg {
					seg[k] = 0
				}
				if payload&(1<<21) != 0 {
					seg[0], seg[1] = lit0, lit1
				} else {
					seg[zeros], seg[zeros+1] = lit0, lit1
				}
				out = out[:len(out)+zeros+2]
				continue
			}

			if val >= fillSignalBase {
				// Section 7.4, solid variant: no characters are read to
				// construct the data.
				payload := val - fillSignalBase
				length := int(payload&0x7FF) + 1
				b := byte((payload >> 11) & 0xFF)
				out = slices.Grow(out, length)
				seg := out[len(out) : len(out)+length]
				for k := range seg {
					seg[k] = b
				}
				out = out[:len(out)+length]
				continue
			}

			payload := val - dpSignalBase
			profile := int(payload>>24) & 0x7
			mask := uint16(payload>>11) & 0x1FFF
			// Section 9: the length field is stored biased by one, so the
			// smallest segment a signal can name is 1 character and the largest
			// maxDPSegmentChars.
			length := int(payload&0x7FF) + 1

			if pos+length > n {
				return nil, newDecodeError(pos, ErrUnexpectedEOF,
					"DP segment declares %d characters but only %d remain", length, n-pos)
			}

			// Section 4.3, inline: one lookup per character answers membership
			// and substitution together, and the two sides are the same length --
			// no construct spends two characters on one byte.
			xlat := decBase
			pairs, k := donorTable(profile, mask)
			for _, pair := range pairs[:k] {
				xlat[pair[1]] = uint16(pair[0])
			}
			end := pos + length
			out = slices.Grow(out, length)
			for pos < end {
				c := in[pos]
				t := xlat[c]
				pos++
				if t&decInvalid != 0 {
					return nil, newDecodeError(pos-1, ErrInvalidCharacter,
						"invalid character %q in DP segment", rune(c))
				}
				out = append(out, byte(t))
			}
			continue
		}

		// Fewer than 5 characters remain: this must be the trailing partial
		// block for the whole stream (Section 7.5).
		if remaining == 1 {
			// The character has to be an Alphabet-N one before its being
			// alone is the complaint. Section 10 makes a significant
			// character outside Alphabet-N an ErrInvalidCharacter
			// unconditionally, and Section 8 gives no digit value to one, so
			// a character with no value cannot be the trailing group whose
			// size is at issue. Reporting the size instead was a real
			// divergence -- this implementation and the C one did, the Rust
			// and TypeScript ones did not -- found by differential fuzzing.
			if charToValue[in[pos]] < 0 {
				return nil, newDecodeError(pos, ErrInvalidCharacter,
					"invalid character %q", rune(in[pos]))
			}
			return nil, newDecodeError(pos, ErrInvalidFinalBlock,
				"a single trailing character cannot form a valid final block")
		}

		var chars5 [5]byte
		for k := 0; k < remaining; k++ {
			chars5[k] = in[pos+k]
		}
		for k := remaining; k < 5; k++ {
			chars5[k] = '#' // value 84, per Section 7.5
		}
		val, err := decode5(chars5[:], pos)
		if err != nil {
			return nil, err
		}
		// Section 7.5: the padded group's value must be below 2^32.
		if val >= dpSignalBase {
			return nil, newDecodeError(pos, ErrInvalidFinalBlock,
				"final block of %d characters pads to %d, which is not below 2^32", remaining, val)
		}
		var b4 [4]byte
		binary.BigEndian.PutUint32(b4[:], uint32(val))
		nBytes := remaining - 1

		// Section 7.5, canonical enforcement: the characters must be exactly what
		// encoding those bytes zero-padded to four would have produced. Without
		// this, several character sequences decode to the same bytes.
		var padded [4]byte
		copy(padded[:], b4[:nBytes])
		canonical := encode5(uint64(binary.BigEndian.Uint32(padded[:])))
		for k := 0; k < remaining; k++ {
			if canonical[k] != in[pos+k] {
				return nil, newDecodeError(pos, ErrInvalidFinalBlock,
					"final block of %d characters is not the canonical encoding of the bytes it decodes to",
					remaining)
			}
		}

		out = append(out, b4[:nBytes]...)
		pos = n
	}

	return out, nil
}

// firstInvalidChar reports the first of the count characters at offset that is
// not in Alphabet-N. It runs only on the failure path, where the packed test in
// decodeScan has already established that one of them is.
func firstInvalidChar[T string | []byte](in T, offset, count int) error {
	for k := 0; k < count; k++ {
		c := in[offset+k]
		if charToValue[c] < 0 {
			return newDecodeError(offset+k, ErrInvalidCharacter, "invalid character %q", rune(c))
		}
	}
	return newDecodeError(offset, ErrInvalidCharacter, "invalid character")
}
