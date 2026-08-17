// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Named constants from spec sections 6.4 and 9.

/// Maximum number of leading input bytes examined when identifying a Dynamic
/// Passthrough prefix, and therefore the largest number of bytes one DP
/// segment can carry.
pub const MAX_DP_ANALYSIS_BYTES: usize = 2048;

/// Maximum character length of a single DP segment's data. Equal to
/// [`MAX_DP_ANALYSIS_BYTES`] because DP spends exactly one output character per
/// input byte, and matching the 11-bit length field in the signal.
pub const MAX_DP_SEGMENT_CHARS: usize = 2048;

/// Minimum length of a candidate prefix for DP processing to be attempted:
/// the smallest `L` at which `5 + L` characters is never more than block
/// mode's `ceil(L / 4) * 5`.
pub const MIN_PASSTHROUGH_BYTES: usize = 20;

/// Shortest run of identical bytes a Solid Fill signal is spent on. At four
/// bytes block mode also costs five characters; at five, Fill is ahead.
pub const MIN_FILL_BYTES: usize = 5;

/// Shortest run of identical bytes that ends a Dynamic Passthrough segment.
///
/// Inside passthrough text a run already costs one character per byte, so
/// breaking out to a Fill signal also costs the signal that resumes
/// passthrough afterwards. Ratio alone puts the break-even at eleven bytes,
/// but the threshold moves four things at once, and three of them want it
/// higher: a longer run left inside a segment stays readable, the decoder
/// rebuilds fewer substitution tables, and the encoder rolls back less often.
/// Sixteen is the top of the plateau where ratio is unchanged from thirteen
/// and those three are at their best; see `spec/history/proposals/` for the four
/// measured columns.
pub const MIN_FILL_IN_SEGMENT_BYTES: usize = 16;

/// Longest run one Solid Fill signal can carry, matching its 11-bit length
/// field. It is also the bound on how far a single signal can expand, which is
/// what keeps the format's decompression ratio finite (spec section 13).
pub const MAX_FILL_BYTES: usize = 2048;

/// Shortest zero run a Fill signal carries a tail on. Two zeros and two
/// literals are four bytes, exactly what block mode also spends five
/// characters on; at three the tail variant is ahead.
pub const MIN_TAIL_ZEROS: usize = 3;

/// Longest zero run the tail variant can carry, matching its 5-bit length
/// field. Longer runs are carried by the solid variant, which has 11 bits of
/// length but no tail.
pub const MAX_TAIL_ZEROS: usize = 32;

/// First value that is a signal rather than a standard 4-byte block: block
/// mode occupies `0 .. 2^32`.
pub const DP_SIGNAL_BASE: u64 = 1u64 << 32;

/// Number of DP signal values: 3 profile bits + 13 mask bits + 11 length bits.
pub const DP_SIGNAL_SPAN: u64 = 1u64 << 27;

/// First Fill signal value; one past the last DP signal. Fill's solid variant
/// occupies the first [`FILL_SIGNAL_SPAN`] values of the range.
pub const FILL_SIGNAL_BASE: u64 = DP_SIGNAL_BASE + DP_SIGNAL_SPAN;

/// Number of solid Fill signal values: 8 byte-value bits + 11 length bits.
pub const FILL_SIGNAL_SPAN: u64 = 1u64 << 19;

/// First Fill signal value of the tail variant, one past the solid variant.
pub const TAIL_SIGNAL_BASE: u64 = FILL_SIGNAL_BASE + FILL_SIGNAL_SPAN;

/// Number of tail Fill signal values: 16 literal bits + 5 length bits + one
/// order bit.
pub const TAIL_SIGNAL_SPAN: u64 = 1u64 << 22;

/// First value of `FUTURE_SIGNAL_SPACE`, which a decoder must reject
/// (spec section 9). It runs to `85^5 - 1`.
pub const FUTURE_SIGNAL_BASE: u64 = TAIL_SIGNAL_BASE + TAIL_SIGNAL_SPAN;

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn signal_ranges_match_the_specification_table() {
        assert_eq!(DP_SIGNAL_BASE, 4_294_967_296);
        assert_eq!(FILL_SIGNAL_BASE, 4_429_185_024);
        assert_eq!(TAIL_SIGNAL_BASE, 4_429_709_312);
        assert_eq!(FUTURE_SIGNAL_BASE, 4_433_903_616);
        // Five characters span 85^5 values, and the future space is what is
        // left of them. The tail variant's 22 bits are the widest field that
        // still fits: 23 would need 8,388,608 values and only 7,343,813 were
        // left above the solid variant.
        let total: u64 = 85u64.pow(5);
        assert_eq!(total, 4_437_053_125);
        assert_eq!(total - FILL_SIGNAL_BASE - FILL_SIGNAL_SPAN, 7_343_813);
        assert_eq!(total - FUTURE_SIGNAL_BASE, 3_149_509);
    }
}
