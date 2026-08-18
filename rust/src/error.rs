// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Errors returned by [`crate::decode`].

use std::fmt;

/// Errors that can occur while decoding a Base85N string.
///
/// These are the four error conditions enumerated in spec section 10.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum DecodeError {
    /// `INVALID_CHARACTER`: a significant character -- one left after
    /// ignorable whitespace is removed -- is not a member of Alphabet-N.
    InvalidCharacter {
        /// The offending character.
        character: char,
        /// Its byte offset within the original input string.
        position: usize,
    },

    /// `UNEXPECTED_EOS`: the input ended while a block, signal or segment was
    /// still required.
    UnexpectedEndOfStream,

    /// `UNDEFINED_SIGNAL`: a 5-character group's value fell in
    /// `FUTURE_SIGNAL_SPACE`, above every signal this version defines.
    UndefinedSignal {
        /// The group value that was encountered.
        value: u64,
    },

    /// `INVALID_FINAL_BLOCK`: a trailing group of fewer than five characters
    /// was malformed -- a single leftover character, a padded value that does
    /// not fit in 32 bits, or characters that are not the canonical encoding of
    /// the bytes they decode to.
    InvalidFinalBlock {
        /// The number of leftover characters that formed the invalid
        /// trailing group.
        length: usize,
    },
}

impl DecodeError {
    /// The shared error-code string used by `testvectors/` and by the language
    /// bindings, one per condition in spec section 10.
    pub fn code(&self) -> &'static str {
        match self {
            DecodeError::InvalidCharacter { .. } => "invalid_character",
            DecodeError::UnexpectedEndOfStream => "unexpected_end_of_stream",
            DecodeError::UndefinedSignal { .. } => "undefined_signal",
            DecodeError::InvalidFinalBlock { .. } => "invalid_final_block",
        }
    }

    /// Byte offset in the input at which the error was detected, where the
    /// condition names one.
    /// A byte offset into the string that was decoded.
    ///
    /// Which is the caller's string for [`crate::decode`], and not always the
    /// caller's *buffer* for [`crate::ffi::base85n_decode`]: that entry point
    /// takes bytes, and where they are not all ASCII it decodes a converted
    /// copy in which every byte from 0x80 up occupies two. An offset past such
    /// a byte therefore counts positions in the copy, not in what the C caller
    /// passed. Nothing exports it today -- the C ABI returns a status code and
    /// no position -- so this is a note for whoever adds one: the conversion in
    /// `ffi::base85n_decode` is where the mapping back would have to happen.
    pub fn position(&self) -> Option<usize> {
        match self {
            DecodeError::InvalidCharacter { position, .. } => Some(*position),
            _ => None,
        }
    }
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
            DecodeError::UndefinedSignal { value } => write!(
                f,
                "undefined signal: group value {} is in FUTURE_SIGNAL_SPACE",
                value
            ),
            DecodeError::InvalidFinalBlock { length } => {
                write!(f, "invalid final block of {} character(s)", length)
            }
        }
    }
}

impl std::error::Error for DecodeError {}
