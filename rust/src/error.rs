//! Errors returned by [`crate::decode`].

use std::fmt;

/// Errors that can occur while decoding a Base85N string.
///
/// These correspond to the error conditions enumerated in README.md
/// section 10.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum DecodeError {
    /// A character was encountered (after ignorable-whitespace stripping)
    /// that is not a member of Alphabet-N.
    InvalidCharacter {
        /// The offending character.
        character: char,
        /// Its byte offset within the original input string.
        position: usize,
    },

    /// The input ended before a required 5-character group, DP signal, or
    /// DP data segment (of the length declared by its signal) could be
    /// completed.
    UnexpectedEndOfStream,

    /// A `~` escape character was the very last character of a DP data
    /// segment, with no following character to escape.
    DanglingEscapeCharacter,

    /// A decoded 5-character group was `>= 2^32` (i.e. a DP signal) but
    /// its `SignalPayload` (`decodedValue - 2^32`) exceeded `2^22 - 1`,
    /// the maximum defined payload value.
    ReservedSignalValue {
        /// The out-of-range payload value that was encountered.
        payload: u64,
    },

    /// A trailing (partial, non-multiple-of-5-character) final group was
    /// malformed: either it consisted of a single leftover character
    /// (which cannot encode any byte), or padding it out to a full group
    /// produced a value that does not fit in 32 bits.
    InvalidPartialBlock {
        /// The number of leftover characters that formed the invalid
        /// trailing group.
        length: usize,
    },
}

impl fmt::Display for DecodeError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            DecodeError::InvalidCharacter { character, position } => write!(
                f,
                "invalid character {:?} at byte offset {} is not a member of Alphabet-N",
                character, position
            ),
            DecodeError::UnexpectedEndOfStream => {
                write!(f, "unexpected end of stream: more Base85N characters were expected")
            }
            DecodeError::DanglingEscapeCharacter => write!(
                f,
                "dangling escape character: '~' appeared at the end of a DP data segment with nothing to escape"
            ),
            DecodeError::ReservedSignalValue { payload } => write!(
                f,
                "reserved/undefined DP signal payload {} (must be in 0..=4194303)",
                payload
            ),
            DecodeError::InvalidPartialBlock { length } => write!(
                f,
                "invalid partial trailing block of {} character(s)",
                length
            ),
        }
    }
}

impl std::error::Error for DecodeError {}
