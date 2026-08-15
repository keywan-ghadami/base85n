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
/// spending a Fill signal on it also costs the five characters of the signal
/// that resumes passthrough afterwards: breaking pays only from eleven bytes
/// up. See spec section 6.5 and `bench/results/RESULTS.md`.
pub const MIN_FILL_IN_SEGMENT_BYTES: usize = 11;

/// Longest run one Solid Fill signal can carry, matching its 11-bit length
/// field. It is also the bound on how far a single signal can expand, which is
/// what keeps the format's decompression ratio finite (spec section 13).
pub const MAX_FILL_BYTES: usize = 2048;

/// First value that is a signal rather than a standard 4-byte block: block
/// mode occupies `0 .. 2^32`.
pub const DP_SIGNAL_BASE: u64 = 1u64 << 32;

/// Number of DP signal values: 3 profile bits + 13 mask bits + 11 length bits.
pub const DP_SIGNAL_SPAN: u64 = 1u64 << 27;

/// First Solid Fill signal value; one past the last DP signal.
pub const FILL_SIGNAL_BASE: u64 = DP_SIGNAL_BASE + DP_SIGNAL_SPAN;

/// Number of Solid Fill signal values: 8 byte-value bits + 11 length bits.
pub const FILL_SIGNAL_SPAN: u64 = 1u64 << 19;

/// First value of `FUTURE_SIGNAL_SPACE`, which a decoder must reject
/// (spec section 9). It runs to `85^5 - 1`.
pub const FUTURE_SIGNAL_BASE: u64 = FILL_SIGNAL_BASE + FILL_SIGNAL_SPAN;

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn signal_ranges_match_the_specification_table() {
        assert_eq!(DP_SIGNAL_BASE, 4_294_967_296);
        assert_eq!(FILL_SIGNAL_BASE, 4_429_185_024);
        assert_eq!(FUTURE_SIGNAL_BASE, 4_429_709_312);
        // Five characters span 85^5 values, and the future space is what is
        // left of them.
        let total: u64 = 85u64.pow(5);
        assert_eq!(total, 4_437_053_125);
        assert_eq!(total - FUTURE_SIGNAL_BASE, 7_343_813);
    }
}
