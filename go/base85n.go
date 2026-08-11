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

// isRepresentable folds Pass 1's two membership questions into one lookup: a
// byte belongs to a representable run iff it is an R-Set character or an
// Alphabet-N character (which includes the escape character and every
// replacement character, regardless of escaping cost).
var isRepresentable [256]bool

// decSub is the decoder's view of a character inside a DP segment, packed so
// that the segment loop asks one question per character instead of three:
//
//	bit 31      decInvalid -- not a member of Alphabet-N.
//	bit 30      decEscape -- the character is '~'.
//	bits 16..28 (1 << j) if the character is replacement character j;
//	            intersect with the signal's 13-bit mask to decide whether this
//	            occurrence stands for an R-Set byte or for itself.
//	bits 0..7   the byte to emit when it does: R-Set character j's ASCII value,
//	            or the character itself when it is not a replacement character.
//
// It is derived from rsetASCII and replacementChars at startup rather than
// written out as a literal, so there is no second copy of the table to keep in
// step with Section 4.
var decSub [256]uint32

const (
	decInvalid uint32 = 0x80000000
	decEscape  uint32 = 0x40000000
)

func init() {
	for _, c := range []byte(alphabetChars) {
		isRepresentable[c] = true
	}
	for _, b := range rsetASCII {
		isRepresentable[b] = true
	}

	for i := 0; i < 256; i++ {
		b := byte(i)
		entry := uint32(b)
		if charToValue[b] < 0 {
			entry |= decInvalid
		} else if b == escapeChar {
			entry |= decEscape
		} else if j := replIndexByChar[b]; j >= 0 {
			entry = uint32(1)<<(16+uint(j)) | uint32(rsetASCII[j])
		}
		decSub[i] = entry
	}
}

// isIgnorableWS reports whether c is one of the four inter-token whitespace
// characters Section 7.1 allows between Base85N constructs.
func isIgnorableWS(c byte) bool {
	return c == ' ' || c == '\t' || c == '\n' || c == '\r'
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
	var segEndsScratch []int

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

		// Skip the mode decision where it cannot change the answer. A DP
		// candidate is never longer than the representable run it starts in, so
		// until the next run that reaches minPassthroughBytes the block-mode
		// branch is certain -- and block mode over whole 4-byte groups is the
		// concatenation of the per-group results, so that entire stretch can be
		// encoded in one call instead of re-entering this loop every 4 bytes.
		// Only worth trying when the current window is itself too short for DP;
		// inside a long representable run the scan would return immediately and
		// cost a rescan for nothing.
		if windowLen < minPassthroughBytes {
			limit := firstDPCapableRun(data, i)
			if batch := ((limit - i) / 4) * 4; batch >= 4 {
				processBlockMode(data[i:i+batch], &sb)
				i += batch
				// The batch may end mid-run, so the cached run state no longer
				// describes the new position.
				run.end = 0
				continue
			}
		}

		mask := run.mask
		candidateLen, transformed, segEnds := pass2Candidate(
			data[i:i+windowLen], mask, transformedScratch, segEndsScratch)
		// Keep whatever capacity append() grew for the next iteration.
		transformedScratch, segEndsScratch = transformed, segEnds

		useDP := false
		if candidateLen >= minPassthroughBytes {
			conceptualDPLen := len(segEnds)*5 + len(transformed)
			blockModeLen := ((candidateLen + 3) / 4) * 5
			if conceptualDPLen <= blockModeLen {
				useDP = true
			}
		}

		var consumed int
		switch {
		case useDP:
			segStart := 0
			for _, segEnd := range segEnds {
				seg := transformed[segStart:segEnd]
				payload := (uint64(mask) << 9) | uint64(len(seg))
				digits := encode5(blockSignalBase + payload)
				sb.Write(digits[:])
				sb.Write(seg)
				segStart = segEnd
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

// firstDPCapableRun returns the offset of the first position at or after from
// where a Dynamic Passthrough candidate could begin -- the first position whose
// representable run reaches minPassthroughBytes -- or len(data) if there is
// none.
//
// It can afford to look ahead because any minPassthroughBytes consecutive
// positions contain exactly one multiple of minPassthroughBytes, so a run that
// long cannot avoid a sampling lattice of that stride. Sampling instead of
// scanning turns the lookahead from one table lookup per byte into one per 20
// bytes on the input where it matters -- high-entropy data, where nearly every
// sample lands on an unrepresentable byte and is rejected immediately. A sample
// that does land in a run costs a walk to that run's bounds, and the walk
// forward stops as soon as the threshold is reached.
func firstDPCapableRun(data []byte, from int) int {
	n := len(data)
	p := from
	for p < n {
		if !isRepresentable[data[p]] {
			p += minPassthroughBytes
			continue
		}

		// Back to this run's start, but never before from: positions before it
		// are not the caller's concern.
		start := p
		for start > from && isRepresentable[data[start-1]] {
			start--
		}

		// Forward only until the threshold is settled either way.
		end := p
		for end < n && isRepresentable[data[end]] {
			end++
			if end-start >= minPassthroughBytes {
				return start
			}
		}

		// Too short. Resume the lattice at this run's end; a later run of the
		// required length still cannot dodge it.
		p = end
		if p == from {
			p++ // defensive: always make progress
		}
	}
	return n
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
// actual candidate prefix length and its transformed output.
//
// DP Output Segmentation (step 1.d) rides along in the same pass. It is greedy
// over the same byte sequence -- close the current segment *before* adding a
// piece that would push it past maxDPOutputCharsPerSignal, so a boundary never
// falls inside a Case ii escape pair -- which makes it a prefix computation
// like everything else here, and lets it run in this loop rather than in two
// more walks over the result. segEnds[k] is the end offset of segment k within
// transformed; segment k is transformed[segEnds[k-1]:segEnds[k]].
//
// The scratch slices are supplied by the caller and reused across
// iterations. Sizing them from len(window) on every call would be O(n^2) on
// escape-dense input for the same reason the Pass 1 rescan is: the window
// can be the whole remaining run while Pass 2 bails out after 3 bytes, and
// Go zeroes what it allocates. The returned slices alias the scratch
// buffers, so the caller must consume them before the next call.
func pass2Candidate(window []byte, finalMask uint16, transformedScratch []byte, segEndsScratch []int) (candidateLen int, transformed []byte, segEnds []int) {
	transformed = transformedScratch[:0]
	segEnds = segEndsScratch[:0]
	consecutiveEscapes := 0
	segLen := 0
	for idx := 0; idx < len(window); idx++ {
		b := window[idx]

		if j := rsetIndexByASCII[b]; j >= 0 {
			// Case i. finalMask is guaranteed to have bit j set: Pass 1
			// always sets it for any R-Set byte included in window, and
			// bits never clear afterward.
			if segLen+1 > maxDPOutputCharsPerSignal {
				segEnds = append(segEnds, len(transformed))
				segLen = 0
			}
			transformed = append(transformed, replacementChars[j])
			segLen++
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
			if segLen+2 > maxDPOutputCharsPerSignal {
				segEnds = append(segEnds, len(transformed))
				segLen = 0
			}
			transformed = append(transformed, escapeChar, b)
			segLen += 2
			candidateLen = idx + 1
			continue
		}

		// Case iii: plain literal (window guarantees representability).
		if segLen+1 > maxDPOutputCharsPerSignal {
			segEnds = append(segEnds, len(transformed))
			segLen = 0
		}
		transformed = append(transformed, b)
		segLen++
		consecutiveEscapes = 0
		candidateLen = idx + 1
	}
	if segLen > 0 {
		segEnds = append(segEnds, len(transformed))
	}
	return candidateLen, transformed, segEnds
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
	// One allocation, sized by the bound decodeScan documents: a 5-character
	// group yields 4 bytes and a DP segment at most 1 byte per character, so no
	// input character ever yields more than one output byte. The decode loop
	// therefore never tests capacity.
	out := make([]byte, len(s))

	produced, err := decodeScan(s, out)

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
	//
	// The filtered copy goes into out and is decoded in place. out is already
	// big enough -- stripping only ever shortens -- and the first attempt's
	// partial output is being discarded anyway, so the retry allocates nothing.
	// That matters because the retry is reachable from untrusted input: a long,
	// otherwise valid stream with one trailing space decodes all the way to the
	// last character before failing, and should not also double the decoder's
	// peak footprint.
	if err != nil {
		i := 0
		for ; i < len(s); i++ {
			if isIgnorableWS(s[i]) {
				break
			}
		}
		if i < len(s) {
			copy(out, s[:i]) // everything before the first whitespace copies wholesale
			n := i
			for k := i; k < len(s); k++ {
				if !isIgnorableWS(s[k]) {
					out[n] = s[k]
					n++
				}
			}
			produced, err = decodeScan(out[:n], out)
		}
		if err != nil {
			return nil, err
		}
	}

	return out[:produced], nil
}

// decodeScan decodes in, which must already be free of the inter-token
// whitespace Section 7.1 allows, into out, and returns the number of bytes
// produced. out must have room for len(in) bytes.
//
// out may alias in exactly (the whitespace retry in Decode decodes in place on
// the strength of it). The writer never catches the reader: a 5-character group
// is loaded into locals before any of its 4 bytes are stored, a partial final
// group likewise, and inside a DP segment the writer trails the reader by at
// least the segment's own 5-character signal, which produced no output of its
// own. Any other overlap is undefined.
//
// It is generic over string and []byte so that the first pass can read the
// caller's string without copying it and the retry can read the filtered bytes.
func decodeScan[T string | []byte](in T, out []byte) (int, error) {
	w := 0
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
				return 0, firstInvalidChar(in, pos, 5)
			}
			// Horner's rule would chain five multiplies end to end; weighing the
			// digits directly leaves them independent. Only the top term can
			// leave 32 bits.
			val := uint64(v0)*pow85_4 +
				uint64(uint32(v1)*pow85_3+uint32(v2)*pow85_2+uint32(v3)*85+uint32(v4))
			pos += 5

			if val < blockSignalBase {
				binary.BigEndian.PutUint32(out[w:], uint32(val))
				w += 4
				continue
			}

			payload := val - blockSignalBase
			if payload > maxSignalPayload {
				return 0, newDecodeError(groupOffset, ErrReservedSignal, "signal payload %d exceeds maximum %d", payload, maxSignalPayload)
			}
			mask13 := uint32(payload>>9) << 16
			length := int(payload & 0x1FF)

			if pos+length > n {
				return 0, newDecodeError(pos, ErrUnexpectedEOF, "DP segment declares %d characters but only %d remain", length, n-pos)
			}

			// Section 7.1.e, inline: one decSub lookup per character answers
			// membership, escaping and substitution together.
			end := pos + length
			for pos < end {
				c := in[pos]
				t := decSub[c]
				pos++
				if t&(decInvalid|decEscape) != 0 {
					if t&decInvalid != 0 {
						return 0, newDecodeError(pos-1, ErrInvalidCharacter, "invalid character %q in DP segment", rune(c))
					}
					// '~': the next character stands for itself.
					if pos >= end {
						return 0, newDecodeError(pos-1, ErrDanglingEscape, "escape character at end of DP segment")
					}
					c2 := in[pos]
					pos++
					if decSub[c2]&decInvalid != 0 {
						return 0, newDecodeError(pos-1, ErrInvalidCharacter, "invalid character %q in DP segment", rune(c2))
					}
					out[w] = c2
					w++
					continue
				}
				// A replacement character stands for its R-Set byte exactly
				// while the signal's mask says the window contained it.
				if t&mask13 != 0 {
					out[w] = byte(t)
				} else {
					out[w] = c
				}
				w++
			}
			continue
		}

		// Fewer than 5 characters remain: this must be the trailing
		// partial block for the whole stream (Section 7.1, last bullet).
		if remaining == 1 {
			return 0, newDecodeError(pos, ErrInvalidPartialBlock, "a single trailing character cannot form a valid partial block")
		}

		var chars5 [5]byte
		for k := 0; k < remaining; k++ {
			chars5[k] = in[pos+k]
		}
		for k := remaining; k < 5; k++ {
			chars5[k] = '#' // value 84, per Section 7.1
		}
		val, err := decode5(chars5[:], pos)
		if err != nil {
			return 0, err
		}
		// Spec 7.1: the padded group's value must be below 2^32. The encoder
		// truncates a group whose value already is, and re-padding with '#'
		// raises it by at most 614124, so a group that crosses 2^32 cannot be
		// this format's output. Reducing it modulo 2^32 instead would accept
		// several character sequences as encodings of the same bytes.
		if val >= blockSignalBase {
			return 0, newDecodeError(pos, ErrInvalidPartialBlock,
				"partial final block of %d characters pads to %d, which is not below 2^32", remaining, val)
		}
		var b4 [4]byte
		binary.BigEndian.PutUint32(b4[:], uint32(val))
		nBytes := remaining - 1
		copy(out[w:], b4[:nBytes])
		w += nBytes
		pos = n
	}

	return w, nil
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
