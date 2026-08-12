// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Package base85n implements the Base85N binary-to-text encoding scheme:
// a Base85 variant using a single, protocol-friendly 85-character alphabet
// (Alphabet-N) plus an adaptive Dynamic Passthrough (DP) mode that can
// represent runs of mostly-printable input more compactly. A DP segment names
// one of eight fixed replacement alphabets, each Alphabet-N with a few of its
// rarest characters given up so that "R-Set" characters (space, newline,
// quote, comma, ...) can be carried in their place. Each alphabet is
// injective, so DP needs no escape mechanism: one input byte becomes exactly
// one output character.
//
// See the specification in spec/ (base85n-v0.3.0.md) for the full
// formal description.
package base85n

import (
	"encoding/binary"
	"errors"
	"fmt"
	"math/bits"
	"strings"
)

// ---------------------------------------------------------------------
// Alphabet-N and derived tables (spec Section 4)
// ---------------------------------------------------------------------

const alphabetChars = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ.-:+=^!/*?`_~()[]{}@%$#"

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

// numAlphabets is the number of replacement alphabets, and the range of the
// signal's 3-bit alphabet identifier.
const numAlphabets = 8

// substitution is one (R-Set index, donor character) pair of a replacement
// alphabet: rsetASCII[j] is written as donor, and donor itself becomes
// unrepresentable under that alphabet.
type substitution struct {
	j     uint8
	donor byte
}

// replacementAlphabets holds the eight replacement alphabets of Section 4.2.
//
// The donors are the least frequent Alphabet-N characters measured over a
// mixed corpus -- ^ @ % $ ? ! ~ #, then * + = _ and backtick -- except where
// an alphabet's own target shape makes one of them common: alphabet 3 (markup)
// spends '{' rather than '#', and alphabet 5 (code) spends the backtick, which
// is rare in source but not in Markdown.
var replacementAlphabets = [numAlphabets][]substitution{
	// 0 none
	{},
	// 1 text
	{{0, '^'}, {11, '@'}, {12, '%'}, {10, '$'}},
	// 2 prose
	{{0, '^'}, {11, '@'}, {3, '%'}, {1, '$'}, {2, '?'}, {4, '!'}},
	// 3 markup
	{{0, '^'}, {11, '@'}, {7, '%'}, {8, '$'}, {9, '?'}, {1, '!'}, {2, '~'}, {3, '{'}},
	// 4 json
	{{0, '^'}, {11, '@'}, {1, '%'}, {3, '$'}, {5, '?'}, {12, '!'}},
	// 5 code
	{{0, '^'}, {11, '@'}, {3, '%'}, {4, '$'}, {1, '?'}, {2, '!'}, {10, '~'}, {8, '`'}},
	// 6 shell
	{{0, '^'}, {11, '@'}, {6, '%'}, {5, '$'}, {1, '?'}, {2, '!'}, {9, '~'}, {4, '#'}},
	// 7 full
	{{0, '^'}, {11, '@'}, {12, '%'}, {10, '$'}, {3, '?'}, {4, '!'}, {1, '~'}, {2, '#'},
		{7, '*'}, {8, '+'}, {9, '='}, {6, '_'}, {5, '`'}},
}

// repr[b] is the set of alphabets that can represent byte b: bit a is set iff
// b is representable under replacement alphabet a (Section 6.1, step 1).
//
// This is what lets the encoder settle all eight scans in one pass: it walks
// forward AND-ing this mask into a live set, and an alphabet's run ends exactly
// at the position where its bit leaves that set.
var repr [256]uint8

// encXlat[a][b] is the character byte b becomes in DP output under alphabet a.
// Only meaningful where repr[b] has bit a set.
var encXlat [numAlphabets][256]byte

// decXlat[a][c] is the byte character c stands for under alphabet a, with
// decInvalid set when c is not a member of Alphabet-N. One lookup answers both
// questions a decoder has about a character inside a DP segment, and there is
// no state to carry between characters.
var decXlat [numAlphabets][256]uint16

const decInvalid uint16 = 0x8000

// These are byte-indexed arrays rather than maps because every input byte is
// looked up in them in the encoder's hot path: a Go map lookup hashes the key
// and chases a bucket, which dominated encoding time. They are derived from
// alphabetChars, rsetASCII and replacementAlphabets at startup rather than
// written out as literals, so there is no second copy of Section 4 to keep in
// step.
func init() {
	for a := 0; a < numAlphabets; a++ {
		subs := replacementAlphabets[a]

		donor := [256]bool{}
		substituted := [13]bool{}
		for _, sub := range subs {
			donor[sub.donor] = true
			substituted[sub.j] = true
		}

		for b := 0; b < 256; b++ {
			// An Alphabet-N character represents itself unless this alphabet
			// spends it as a donor; an R-Set character is representable only if
			// this alphabet substitutes it. No byte is both.
			ok := charToValue[b] >= 0 && !donor[b]
			encXlat[a][b] = byte(b)
			decXlat[a][b] = uint16(b)
			if charToValue[b] < 0 {
				decXlat[a][b] = decInvalid | uint16(b)
			}
			if ok {
				repr[b] |= 1 << uint(a)
			}
		}
		for _, sub := range subs {
			encXlat[a][rsetASCII[sub.j]] = sub.donor
			decXlat[a][sub.donor] = uint16(rsetASCII[sub.j])
			repr[rsetASCII[sub.j]] |= 1 << uint(a)
		}
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
	// maxDPAnalysisBytes bounds how much input is examined per DP decision,
	// and therefore how many bytes one DP segment can carry.
	maxDPAnalysisBytes = 1024
	// maxDPOutputCharsPerSignal equals maxDPAnalysisBytes because the
	// transformation is 1:1, and matches the signal's 10-bit length field.
	maxDPOutputCharsPerSignal = 1024
	minPassthroughBytes       = 20

	blockSignalBase  uint64 = 1 << 32 // decodedValue threshold: DP signal iff decodedValue >= 2^32
	maxSignalPayload uint64 = (1 << 13) - 1

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

	// ErrReservedSignal indicates a decoded 5-character group encoded a value
	// >= 2^32 whose payload (value - 2^32) falls outside the valid 0..2^13-1
	// range for a Dynamic Passthrough signal.
	ErrReservedSignal = errors.New("base85n: reserved or undefined signal value")

	// ErrInvalidPartialBlock indicates a malformed trailing partial block
	// (e.g. a single leftover Alphabet-N character, which cannot decode to
	// any byte).
	ErrInvalidPartialBlock = errors.New("base85n: invalid partial block length")
)

// DecodeError provides positional context for a decoding failure. It always
// wraps one of the sentinel errors declared in this package (ErrInvalidCharacter,
// ErrUnexpectedEOF, ErrReservedSignal, ErrInvalidPartialBlock), so callers can
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
	// Block mode is 5 characters per 4 bytes, and a DP segment always costs
	// less than that, so this is an upper bound rather than an estimate.
	sb.Grow(len(data)*5/4 + 16)

	n := len(data)
	i := 0
	// Start of the pending run of block-mode bytes, or -1 for none. Consecutive
	// block-mode iterations are converted in one call instead of four bytes at
	// a time, and stretches where no alphabet can reach minPassthroughBytes are
	// skipped outright. Neither changes the output: block mode consumes exactly
	// one 4-byte group per iteration, so every position skipped would have
	// taken that branch, and block mode over a whole number of groups is the
	// concatenation of the per-group results.
	blockStart := -1

	for i < n {
		bestLen, bestAlphabet := scanAlphabets(data, i)

		if bestLen >= minPassthroughBytes {
			// Step 2.a. At minPassthroughBytes the two modes cost the same 25
			// characters and Dynamic Passthrough only gains from there, so the
			// length test settles the size comparison too.
			if blockStart >= 0 {
				processBlockMode(data[blockStart:i], &sb)
				blockStart = -1
			}
			payload := (uint64(bestAlphabet) << 10) | uint64(bestLen-1)
			digits := encode5(blockSignalBase + payload)
			sb.Write(digits[:])
			xlat := &encXlat[bestAlphabet]
			for _, b := range data[i : i+bestLen] {
				sb.WriteByte(xlat[b])
			}
			i += bestLen
			continue
		}

		// Step 2.b, block-mode fallback: exactly one 4-byte group, however long
		// the failed candidate was. Nothing but the end of the input can hand
		// processBlockMode a partial group this way.
		if blockStart < 0 {
			blockStart = i
		}
		if n-i < 4 {
			i = n
		} else {
			i += 4
		}

		// Skip the stretch in which no alphabet can reach the DP threshold.
		// Every position passed over would have taken this same branch and
		// consumed 4 bytes, so the output is unchanged.
		limit := firstDPCapableRun(data, i)
		i += ((limit - i) / 4) * 4
	}

	if blockStart >= 0 {
		processBlockMode(data[blockStart:i], &sb)
	}

	return sb.String()
}

// scanAlphabets performs spec Section 6.1 step 1 (Dynamic Prefix
// Identification) for all eight replacement alphabets in a single walk.
//
// It returns the length of the longest representable prefix starting at pos and
// the identifier of the alphabet achieving it, the numerically smallest such
// identifier winning a tie as the spec requires. The length is capped at
// maxDPAnalysisBytes.
//
// Asking the alphabets one at a time would walk the window eight times.
// Instead, live carries the alphabets that have represented every byte so far;
// repr gives that set for a byte in one lookup, so the walk is an AND per byte
// and an alphabet's run ends exactly where its bit leaves the set.
//
// This is also what satisfies Section 6.6 with no state carried between
// iterations of the encoding loop: the walk costs bestLen byte inspections and
// the caller then consumes bestLen bytes under Dynamic Passthrough, or at least
// bestLen-3 under block mode, so the work per byte of input is bounded by a
// small constant rather than by the window size.
func scanAlphabets(data []byte, pos int) (bestLen, bestAlphabet int) {
	limit := len(data) - pos
	if limit > maxDPAnalysisBytes {
		limit = maxDPAnalysisBytes
	}
	window := data[pos : pos+limit]

	// Bit a stays set while alphabet a has represented every byte so far. No
	// per-alphabet bookkeeping is needed: an alphabet that drops out earlier
	// reaches strictly less far than one still in live, so when the walk stops
	// -- at the first byte no surviving alphabet can carry, or at the cap --
	// live is exactly the set achieving the greatest length, and that length is
	// the position reached.
	live := uint8(1<<numAlphabets - 1)
	idx := 0
	for ; idx < len(window); idx++ {
		next := live & repr[window[idx]]
		if next == 0 {
			break // every surviving alphabet ends here
		}
		live = next
	}

	// Lowest set bit: the smallest identifier, which is the tie-break the spec
	// requires. live is never zero here.
	return idx, bits.TrailingZeros8(live)
}

// firstDPCapableRun returns the offset of the first position at or after from
// where a Dynamic Passthrough candidate could begin -- the first position
// starting a run of at least minPassthroughBytes bytes that some alphabet can
// represent -- or len(data) if there is none.
//
// Every position before it takes the block-mode branch and consumes exactly 4
// bytes, so the encoder may jump to the last 4-byte boundary at or before it
// without changing the output (spec Section 6.6).
//
// It can afford to look ahead because any minPassthroughBytes consecutive
// positions contain exactly one multiple of minPassthroughBytes, so a run that
// long cannot avoid a sampling lattice of that stride. Sampling instead of
// scanning turns the lookahead from one table lookup per byte into one per 20
// bytes on the input where it matters -- high-entropy data, where nearly every
// sample lands on a byte no alphabet can represent and is rejected at once.
func firstDPCapableRun(data []byte, from int) int {
	n := len(data)
	p := from
	for p < n {
		if repr[data[p]] == 0 {
			p += minPassthroughBytes
			continue
		}

		// Back to this run's start, but never before from.
		start := p
		for start > from && repr[data[start-1]] != 0 {
			start--
		}

		// Forward only until the threshold is settled either way.
		end := p
		for end < n && repr[data[end]] != 0 {
			end++
			if end-start >= minPassthroughBytes {
				return start
			}
		}

		// Too short. Resume the lattice at this run's end.
		p = end
		if p == from {
			p++ // defensive: always make progress
		}
	}
	return n
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
	// group yields 4 bytes and a DP segment exactly 1 byte per character, so no
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
// group likewise, and inside a DP segment the writer trails the reader by the
// segment's own 5-character signal, which produced no output of its own, and
// stays exactly that far behind since each character yields one byte. Any other
// overlap is undefined.
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
			alphabet := int(payload>>10) & 0x7
			// Section 9: the length field is stored biased by one, so the
			// smallest segment a signal can name is 1 character and the
			// largest 1024.
			length := int(payload&0x3FF) + 1

			if pos+length > n {
				return 0, newDecodeError(pos, ErrUnexpectedEOF, "DP segment declares %d characters but only %d remain", length, n-pos)
			}

			// Section 7.1.e, inline: one decXlat lookup per character answers
			// membership and substitution together, and the two sides are the
			// same length -- version 0.3.0 has no construct that spends two
			// characters on one byte.
			xlat := &decXlat[alphabet]
			end := pos + length
			for pos < end {
				c := in[pos]
				t := xlat[c]
				pos++
				if t&decInvalid != 0 {
					return 0, newDecodeError(pos-1, ErrInvalidCharacter, "invalid character %q in DP segment", rune(c))
				}
				out[w] = byte(t)
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
