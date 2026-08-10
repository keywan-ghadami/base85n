// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Package base85n implements the Base85N binary-to-text encoding scheme:
// a Base85 variant using a single, protocol-friendly 85-character alphabet
// (Alphabet-N) plus an adaptive Dynamic Passthrough (DP) mode that can
// represent runs of mostly-printable input more compactly by substituting a
// small set of "R-Set" characters (space, quote, comma, ...) with
// passthrough-safe Alphabet-N characters and escaping the rest.
//
// See the specification in spec/ (base85n-v0.2.0.md) for the full
// formal description.
package base85n

import (
	"encoding/binary"
	"errors"
	"fmt"
	"strings"
)

// ---------------------------------------------------------------------
// Alphabet-N and derived tables (spec Section 4)
// ---------------------------------------------------------------------

const alphabetChars = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ.-:+=^!/*?`_~()[]{}@%$#"

// escapeChar is the fixed Base85N escape character (Section 4.3).
const escapeChar byte = '~'

// charToValue maps an ASCII byte to its Alphabet-N integer value (0-84), or
// -1 if the byte is not part of Alphabet-N.
var charToValue [256]int16

func init() {
	for i := range charToValue {
		charToValue[i] = -1
	}
	for v, c := range []byte(alphabetChars) {
		charToValue[c] = int16(v)
	}
}

// rsetASCII holds the ASCII value of R-Set character j (Section 4.1), indexed by j (0-12).
var rsetASCII = [13]byte{32, 34, 39, 44, 59, 92, 124, 60, 62, 38, 9, 10, 13}

// replacementChars holds allowedPassthroughSafeReplacementCharacters[j] (Section 4.2), indexed by j (0-12).
var replacementChars = [13]byte{':', '+', '=', '^', '!', '/', '*', '?', '`', '(', ')', '[', ']'}

// rsetIndexByASCII maps an R-Set character's ASCII value to its R-Set index
// j, or -1. replIndexByChar does the same for the passthrough-safe
// replacement characters.
//
// These are byte-indexed arrays rather than maps because every input byte is
// looked up in them, twice per byte in the encoder's hot path: a Go map
// lookup hashes the key and chases a bucket, which dominated encoding time.
var (
	rsetIndexByASCII = buildIndex(rsetASCII[:])
	replIndexByChar  = buildIndex(replacementChars[:])
)

func buildIndex(bs []byte) [256]int8 {
	var table [256]int8
	for i := range table {
		table[i] = -1
	}
	for j, b := range bs {
		table[b] = int8(j)
	}
	return table
}

// ---------------------------------------------------------------------
// Constants (Section 6.4)
// ---------------------------------------------------------------------

const (
	maxConsecutiveEscapes     = 3
	maxDPOutputCharsPerSignal = 511
	minPassthroughBytes       = 20

	blockSignalBase  uint64 = 1 << 32 // decodedValue threshold: DP signal iff decodedValue >= 2^32
	maxSignalPayload uint64 = (1 << 22) - 1
)

// ---------------------------------------------------------------------
// Errors (Section 10)
// ---------------------------------------------------------------------

var (
	// ErrInvalidCharacter indicates a byte encountered while decoding that is
	// not part of Alphabet-N (outside of allowed inter-token whitespace).
	ErrInvalidCharacter = errors.New("base85n: invalid character")

	// ErrUnexpectedEOF indicates the input ended before a full 5-character
	// group, or before a DP segment's declared length, could be read.
	ErrUnexpectedEOF = errors.New("base85n: unexpected end of stream")

	// ErrDanglingEscape indicates a DP segment ended with a lone escape
	// character ('~') with no following character to escape.
	ErrDanglingEscape = errors.New("base85n: dangling escape character")

	// ErrReservedSignal indicates a decoded 5-character group encoded a value
	// >= 2^32 whose payload (value - 2^32) falls outside the valid 0..2^22-1
	// range for a Dynamic Passthrough signal.
	ErrReservedSignal = errors.New("base85n: reserved or undefined signal value")

	// ErrInvalidPartialBlock indicates a malformed trailing partial block
	// (e.g. a single leftover Alphabet-N character, which cannot decode to
	// any byte).
	ErrInvalidPartialBlock = errors.New("base85n: invalid partial block length")
)

// DecodeError provides positional context for a decoding failure. It always
// wraps one of the sentinel errors declared in this package (ErrInvalidCharacter,
// ErrUnexpectedEOF, ErrDanglingEscape, ErrReservedSignal, ErrInvalidPartialBlock),
// so callers can use errors.Is to distinguish error conditions.
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

// decode5 converts 5 Alphabet-N characters (starting at cleaned[pos]) into
// their combined integer value, validating each character.
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
// Encoding (Section 6)
// ---------------------------------------------------------------------

// Encode converts data into its Base85N string representation.
func Encode(data []byte) string {
	var sb strings.Builder
	// Rough capacity estimate: block mode is 5 chars per 4 bytes.
	sb.Grow(len(data)*5/4 + 16)

	n := len(data)
	i := 0
	// State of the representable run currently being consumed; end == 0
	// with i == 0 forces the first scan.
	var run runState

	// Scratch buffers for Pass 2, reused across iterations so that a long
	// run costs a bounded number of growing reallocations rather than one
	// window-sized allocation per iteration.
	var transformedScratch []byte
	var pieceLensScratch []uint8

	for i < n {
		if i >= run.end {
			// Entering a run that has not been scanned yet. The final
			// block-mode branch below ignores representability and can step
			// past run.end, landing in a later run which is then scanned
			// here; runs handled this way are disjoint, so total scanning
			// work stays linear in len(data).
			run = scanRun(data, i)
		}
		windowLen := run.end - i
		mask := run.mask
		candidateLen, transformed, pieceLens := pass2Candidate(
			data[i:i+windowLen], mask, transformedScratch, pieceLensScratch)
		// Keep whatever capacity append() grew for the next iteration.
		transformedScratch, pieceLensScratch = transformed, pieceLens

		useDP := false
		var segments [][]byte
		if candidateLen >= minPassthroughBytes {
			segments = packSegments(transformed, pieceLens)
			numSegments := len(segments)
			conceptualDPLen := numSegments*5 + len(transformed)
			blockModeLen := ((candidateLen + 3) / 4) * 5
			if conceptualDPLen <= blockModeLen {
				useDP = true
			}
		}

		var consumed int
		switch {
		case useDP:
			for _, seg := range segments {
				payload := (uint64(mask) << 9) | uint64(len(seg))
				digits := encode5(blockSignalBase + payload)
				sb.Write(digits[:])
				sb.Write(seg)
			}
			consumed = candidateLen

		case candidateLen >= 4:
			// DP not suitable. Per spec Section 6.1 step 2.b, block-encode
			// only the exact multiple-of-4 leading portion of candidateLen
			// immediately; any 0-3 trailing bytes are deferred, unpadded, to
			// the next loop iteration.
			consumed = (candidateLen / 4) * 4
			processBlockMode(data[i:i+consumed], &sb)

		default:
			// Fewer than 4 candidate bytes (or no representable prefix at
			// all). This is the branch that can consume past run.end.
			consumed = 4
			if n-i < consumed {
				consumed = n - i
			}
			processBlockMode(data[i:i+consumed], &sb)
		}

		if i+consumed < run.end {
			// Still inside the same run: retire the consumed bytes so the
			// next iteration's mask covers exactly the remainder.
			run.consume(data, i, i+consumed)
		}
		i += consumed
	}

	return sb.String()
}

// runState carries spec Section 6.1 step 1.a (Pass 1 -- Window and Mask
// Discovery) for one whole representable run, so that the run is scanned
// once instead of once per iteration of the encoding loop.
//
// counts[j] is how often rsetASCII[j] still occurs in the unconsumed part
// of the run; end is where the run stops. The scan is bounded *only* by
// representability (an R-Set character, or any Alphabet-N character, which
// includes the escape character and all replacement characters
// unconditionally) and never terminates early due to escaping cost or the
// consecutive-escape limit.
//
// Pass 1's window for a position deeper inside the same run is a suffix of
// this one, so its mask follows from the counts in constant time.
// Rescanning it -- the literal reading of Section 6.1 -- is redundant and
// makes encoding quadratic; see spec Section 6.6.
type runState struct {
	counts [len(rsetASCII)]int
	// mask has bit j set iff counts[j] != 0; kept in step with counts so
	// the encoding loop reads it instead of recomputing it.
	mask uint16
	end  int
}

func scanRun(data []byte, pos int) runState {
	var st runState
	idx := pos
	for ; idx < len(data); idx++ {
		b := data[idx]
		if j := rsetIndexByASCII[b]; j >= 0 {
			st.counts[j]++
			st.mask |= 1 << uint(j)
			continue
		}
		if charToValue[b] >= 0 {
			continue
		}
		break // unrepresentable byte: the run ends here
	}
	st.end = idx
	return st
}

// consume retires data[from:to] from the run's counts, clearing a mask bit
// as soon as its last occurrence is consumed.
func (st *runState) consume(data []byte, from, to int) {
	for _, b := range data[from:to] {
		if j := rsetIndexByASCII[b]; j >= 0 {
			st.counts[j]--
			if st.counts[j] == 0 {
				st.mask &^= 1 << uint(j)
			}
		}
	}
}

// pass2Candidate performs spec Section 6.1 step 1.b (Pass 2 --
// Boundary Finalization with Fixed Mask): it re-walks window using the
// single, fixed finalMask (== windowMask from Pass 1, never modified here)
// to apply Case i/ii/iii and the consecutive-escape limit, producing the
// actual candidate prefix length, its transformed output, and the
// per-source-byte piece lengths (1 or 2 output characters each) needed to
// split the output into segments without ever cutting a 2-character escape
// pair in half (step 1.d).
// The scratch slices are supplied by the caller and reused across
// iterations. Sizing them from len(window) on every call would be O(n^2) on
// escape-dense input for the same reason the Pass 1 rescan is: the window
// can be the whole remaining run while Pass 2 bails out after 3 bytes, and
// Go zeroes what it allocates. The returned slices alias the scratch
// buffers, so the caller must consume them before the next call.
func pass2Candidate(window []byte, finalMask uint16, transformedScratch []byte, pieceLensScratch []uint8) (candidateLen int, transformed []byte, pieceLens []uint8) {
	transformed = transformedScratch[:0]
	pieceLens = pieceLensScratch[:0]
	consecutiveEscapes := 0
	for idx := 0; idx < len(window); idx++ {
		b := window[idx]

		if j := rsetIndexByASCII[b]; j >= 0 {
			// Case i. finalMask is guaranteed to have bit j set: Pass 1
			// always sets it for any R-Set byte included in window, and
			// bits never clear afterward.
			transformed = append(transformed, replacementChars[j])
			pieceLens = append(pieceLens, 1)
			consecutiveEscapes = 0
			candidateLen = idx + 1
			continue
		}

		needsEscape := false
		if b == escapeChar {
			needsEscape = true
		} else if j := replIndexByChar[b]; j >= 0 && finalMask&(1<<uint(j)) != 0 {
			needsEscape = true
		}
		if needsEscape {
			// Case ii, against the fixed finalMask.
			consecutiveEscapes++
			if consecutiveEscapes > maxConsecutiveEscapes {
				break // terminate; b and the rest of window are excluded
			}
			transformed = append(transformed, escapeChar, b)
			pieceLens = append(pieceLens, 2)
			candidateLen = idx + 1
			continue
		}

		// Case iii: plain literal (window guarantees representability).
		transformed = append(transformed, b)
		pieceLens = append(pieceLens, 1)
		consecutiveEscapes = 0
		candidateLen = idx + 1
	}
	return candidateLen, transformed, pieceLens
}

// packSegments implements spec Section 6.1 step 1.d (DP Output
// Segmentation): it greedily packs transformed (whose per-source-byte
// piece lengths are pieceLens) into segments of at most
// maxDPOutputCharsPerSignal characters, closing the current segment
// *before* adding a piece that would push it over the limit -- so a
// segment boundary never falls inside a Case ii 2-character escape pair.
func packSegments(transformed []byte, pieceLens []uint8) [][]byte {
	var segments [][]byte
	segStart := 0
	charOff := 0
	for _, piece := range pieceLens {
		if charOff-segStart+int(piece) > maxDPOutputCharsPerSignal && charOff > segStart {
			segments = append(segments, transformed[segStart:charOff])
			segStart = charOff
		}
		charOff += int(piece)
	}
	if charOff > segStart {
		segments = append(segments, transformed[segStart:charOff])
	}
	return segments
}

// processBlockMode implements Section 6.2 (ProcessWithBlockMode): full
// 4-byte blocks are each converted to 5 Alphabet-N characters, and any
// trailing 1-3 bytes are zero-padded, converted, and truncated to the
// first 2-4 characters.
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
	// Strip inter-token whitespace (Section 7.1): space, tab, LF, CR.
	cleaned := make([]byte, 0, len(s))
	for i := 0; i < len(s); i++ {
		c := s[i]
		if c == ' ' || c == '\t' || c == '\n' || c == '\r' {
			continue
		}
		cleaned = append(cleaned, c)
	}

	var out []byte
	n := len(cleaned)
	pos := 0

	for pos < n {
		remaining := n - pos

		if remaining >= 5 {
			groupOffset := pos
			val, err := decode5(cleaned[pos:pos+5], pos)
			if err != nil {
				return nil, err
			}
			pos += 5

			if val < blockSignalBase {
				var b4 [4]byte
				binary.BigEndian.PutUint32(b4[:], uint32(val))
				out = append(out, b4[:]...)
				continue
			}

			payload := val - blockSignalBase
			if payload > maxSignalPayload {
				return nil, newDecodeError(groupOffset, ErrReservedSignal, "signal payload %d exceeds maximum %d", payload, maxSignalPayload)
			}
			mask := uint16(payload >> 9)
			length := int(payload & 0x1FF)

			if pos+length > n {
				return nil, newDecodeError(pos, ErrUnexpectedEOF, "DP segment declares %d characters but only %d remain", length, n-pos)
			}
			segment := cleaned[pos : pos+length]
			segOffset := pos
			pos += length

			decoded, err := decodeDPSegment(segment, mask, segOffset)
			if err != nil {
				return nil, err
			}
			out = append(out, decoded...)
			continue
		}

		// Fewer than 5 characters remain: this must be the trailing
		// partial block for the whole stream (Section 7.1, last bullet).
		if remaining == 1 {
			return nil, newDecodeError(pos, ErrInvalidPartialBlock, "a single trailing character cannot form a valid partial block")
		}

		var chars5 [5]byte
		copy(chars5[:], cleaned[pos:])
		for k := remaining; k < 5; k++ {
			chars5[k] = '#' // value 84, per Section 7.1
		}
		val, err := decode5(chars5[:], pos)
		if err != nil {
			return nil, err
		}
		val32 := uint32(val) // conceptually "converting to a 32-bit number"
		var b4 [4]byte
		binary.BigEndian.PutUint32(b4[:], val32)
		nBytes := remaining - 1
		out = append(out, b4[:nBytes]...)
		pos = n
	}

	if out == nil {
		return []byte{}, nil
	}
	return out, nil
}

// decodeDPSegment implements Section 7.1.e: it converts transformed DP data
// (segment) back to original bytes using the fixed mask for the whole
// segment.
func decodeDPSegment(segment []byte, mask uint16, baseOffset int) ([]byte, error) {
	out := make([]byte, 0, len(segment))
	idx := 0
	for idx < len(segment) {
		c := segment[idx]
		if charToValue[c] < 0 {
			return nil, newDecodeError(baseOffset+idx, ErrInvalidCharacter, "invalid character %q in DP segment", rune(c))
		}

		if c == escapeChar {
			escOffset := baseOffset + idx
			idx++
			if idx >= len(segment) {
				return nil, newDecodeError(escOffset, ErrDanglingEscape, "escape character at end of DP segment")
			}
			c2 := segment[idx]
			if charToValue[c2] < 0 {
				return nil, newDecodeError(baseOffset+idx, ErrInvalidCharacter, "invalid character %q in DP segment", rune(c2))
			}
			out = append(out, c2)
			idx++
			continue
		}

		if j := replIndexByChar[c]; j >= 0 && mask&(1<<uint(j)) != 0 {
			out = append(out, rsetASCII[j])
			idx++
			continue
		}

		out = append(out, c)
		idx++
	}
	return out, nil
}
