// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Named constants from spec section 6.4.

/// Maximum number of leading input bytes examined when identifying a Dynamic
/// Passthrough prefix, and therefore the largest number of bytes one DP
/// segment can carry.
pub const MAX_DP_ANALYSIS_BYTES: usize = 1024;

/// Maximum character length of a single DP segment's data. Equal to
/// [`MAX_DP_ANALYSIS_BYTES`] because a replacement alphabet maps one input
/// byte to exactly one output character, and matching the 10-bit length field
/// in the DP signal.
pub const MAX_DP_OUTPUT_CHARS_PER_SIGNAL: usize = 1024;

/// Minimum length of a candidate prefix for DP processing to be attempted.
pub const MIN_PASSTHROUGH_BYTES: usize = 20;

/// The DP signal marker: decoded 5-character group values `>= 2^32`
/// indicate a Dynamic Passthrough signal rather than a standard block.
pub const DP_SIGNAL_BASE: u64 = 1u64 << 32;

/// Maximum valid `SignalPayload` (13 bits: 3-bit alphabet id + 10-bit length).
pub const MAX_SIGNAL_PAYLOAD: u64 = (1u64 << 13) - 1;
