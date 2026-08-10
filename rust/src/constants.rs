//! Named constants from README.md section 6.4.

/// Maximum number of consecutive escape-requiring bytes before the DP
/// prefix scan terminates.
pub const MAX_CONSECUTIVE_ESCAPES: u32 = 3;

/// Maximum character length of a single DP segment's transformed data,
/// derived from the 9-bit length field in the DP signal.
pub const MAX_DP_OUTPUT_CHARS_PER_SIGNAL: usize = 511;

/// Minimum original input length of a segment to attempt DP processing.
pub const MIN_PASSTHROUGH_BYTES: usize = 20;

/// The DP signal marker: decoded 5-character group values `>= 2^32`
/// indicate a Dynamic Passthrough signal rather than a standard block.
pub const DP_SIGNAL_BASE: u64 = 1u64 << 32;

/// Maximum valid `SignalPayload` (22 bits: 13-bit mask + 9-bit length).
pub const MAX_SIGNAL_PAYLOAD: u64 = (1u64 << 22) - 1;
