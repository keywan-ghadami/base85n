// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! C ABI for this crate: the same API as the C implementation's
//! `c/include/base85n.h`, backed by the Rust encoder and decoder.
//!
//! The point of this module is that a C, C++, Python, Ruby, Zig, Java or
//! any-other-FFI-capable caller can use the memory-safe implementation
//! instead of the hand-written C one, without giving up the C calling
//! convention. Everything below the four exported functions is safe Rust:
//! the parsing of attacker-controlled input, which is the part worth
//! protecting, is bounds-checked by the compiler.
//!
//! # Contract
//!
//! Identical to `c/include/base85n.h`, deliberately, so that the two
//! libraries are interchangeable at the ABI level: same function names,
//! same status codes with the same numeric values, same ownership rules.
//! Output buffers are allocated with the C `malloc()` — not with Rust's
//! allocator — so the caller releases them with `free()`, exactly as with
//! the C library. Nothing else crosses the boundary: no Rust type is
//! exposed, no pointer the caller passes in is retained after a call
//! returns, and the library holds no global state.
//!
//! # Failure behaviour
//!
//! Every entry point is `extern "C"`, so a panic inside one aborts the
//! process rather than unwinding into foreign frames (Rust guarantees this
//! since 1.71). No panic is expected: `encode` is total, and `decode`
//! returns its error conditions as values. A failure of Rust's *internal*
//! allocator also aborts, which is Rust's global policy and not something
//! this layer can turn into `BASE85N_ERR_ALLOC`; only the allocation of
//! the caller-owned output buffer is reported that way.

#![allow(non_camel_case_types)]

use core::ffi::{c_char, c_void};
use core::slice;

use crate::DecodeError;

extern "C" {
    fn malloc(size: usize) -> *mut c_void;
}

/// Status codes, numerically identical to `base85n_status` in the C
/// implementation's header.
#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum base85n_status {
    BASE85N_OK = 0,
    BASE85N_ERR_INVALID_CHAR = 1,
    BASE85N_ERR_UNEXPECTED_EOF = 2,
    BASE85N_ERR_RESERVED_SIGNAL = 3,
    BASE85N_ERR_INVALID_PARTIAL_BLOCK = 4,
    BASE85N_ERR_ALLOC = 5,
    BASE85N_ERR_INVALID_ARGUMENT = 6,
}

use base85n_status::*;

impl From<&DecodeError> for base85n_status {
    fn from(e: &DecodeError) -> Self {
        match e {
            DecodeError::InvalidCharacter { .. } => BASE85N_ERR_INVALID_CHAR,
            DecodeError::UnexpectedEndOfStream => BASE85N_ERR_UNEXPECTED_EOF,
            DecodeError::ReservedSignalValue { .. } => BASE85N_ERR_RESERVED_SIGNAL,
            DecodeError::InvalidPartialBlock { .. } => BASE85N_ERR_INVALID_PARTIAL_BLOCK,
        }
    }
}

/// Copy `bytes` into a `malloc`'d buffer, appending a NUL byte that is not
/// counted in the reported length.
///
/// Returns null on allocation failure. `nul` is what makes the encode path
/// hand back a C string while the decode path hands back exactly the bytes
/// it decoded — the C library terminates its string output too, and its
/// byte output not at all. One extra byte is also what lets a zero-length
/// decode return a non-null pointer the caller can `free()`.
fn malloc_copy(bytes: &[u8], nul: bool) -> *mut u8 {
    // +1 for the terminator, or for the "always a real pointer" case.
    let size = match bytes.len().checked_add(1) {
        Some(n) => n,
        None => return core::ptr::null_mut(),
    };
    // SAFETY: `size` is non-zero; `malloc` is documented to return either a
    // buffer of at least `size` writable bytes or null, which is checked.
    let p = unsafe { malloc(size) } as *mut u8;
    if p.is_null() {
        return p;
    }
    // SAFETY: `p` points to `size` = `bytes.len() + 1` writable bytes, and
    // `bytes` is a live slice that cannot overlap a fresh allocation.
    unsafe {
        core::ptr::copy_nonoverlapping(bytes.as_ptr(), p, bytes.len());
        if nul {
            p.add(bytes.len()).write(0);
        }
    }
    p
}

/// Encode `data_len` bytes into a NUL-terminated Base85N string.
///
/// On [`BASE85N_OK`], `*out_str` receives a `malloc`'d buffer the caller
/// owns and must release with `free()`, and `*out_len` its length
/// excluding the terminator. On any error both are left untouched.
///
/// # Safety
///
/// `data` must point to `data_len` readable bytes, or be null when
/// `data_len` is 0. `out_str` and `out_len` must be non-null and writable.
#[no_mangle]
pub unsafe extern "C" fn base85n_encode(
    data: *const u8,
    data_len: usize,
    out_str: *mut *mut c_char,
    out_len: *mut usize,
) -> base85n_status {
    if out_str.is_null() || out_len.is_null() {
        return BASE85N_ERR_INVALID_ARGUMENT;
    }
    if data.is_null() && data_len != 0 {
        return BASE85N_ERR_INVALID_ARGUMENT;
    }
    // SAFETY: the caller guarantees `data_len` readable bytes at `data`;
    // the empty case must not build a slice from a possibly null pointer.
    let input = if data_len == 0 {
        &[][..]
    } else {
        unsafe { slice::from_raw_parts(data, data_len) }
    };

    let encoded = crate::encode(input);
    let buf = malloc_copy(encoded.as_bytes(), true);
    if buf.is_null() {
        return BASE85N_ERR_ALLOC;
    }
    // SAFETY: both out-pointers were checked non-null above, and the
    // caller guarantees they are writable.
    unsafe {
        *out_str = buf as *mut c_char;
        *out_len = encoded.len();
    }
    BASE85N_OK
}

/// Decode `s_len` characters of Base85N text back into bytes.
///
/// On [`BASE85N_OK`], `*out_data` receives a `malloc`'d buffer the caller
/// owns and must release with `free()`, and `*out_len` the number of
/// decoded bytes. The buffer is *not* NUL-terminated and is non-null even
/// when the decoded length is 0. On any error both are left untouched.
///
/// `s` need not be NUL-terminated; exactly `s_len` characters are read.
/// Input that is not valid UTF-8 is rejected with
/// [`BASE85N_ERR_INVALID_CHAR`], since every Alphabet-N character is ASCII
/// and any byte outside the alphabet is an invalid character either way.
///
/// # Safety
///
/// `s` must point to `s_len` readable bytes, or be null when `s_len` is 0.
/// `out_data` and `out_len` must be non-null and writable.
#[no_mangle]
pub unsafe extern "C" fn base85n_decode(
    s: *const c_char,
    s_len: usize,
    out_data: *mut *mut u8,
    out_len: *mut usize,
) -> base85n_status {
    if out_data.is_null() || out_len.is_null() {
        return BASE85N_ERR_INVALID_ARGUMENT;
    }
    if s.is_null() && s_len != 0 {
        return BASE85N_ERR_INVALID_ARGUMENT;
    }
    // SAFETY: as in `base85n_encode`; `c_char` and `u8` have the same
    // layout, and the text is read as bytes, not as a NUL-terminated string.
    let input = if s_len == 0 {
        &[][..]
    } else {
        unsafe { slice::from_raw_parts(s as *const u8, s_len) }
    };

    let text = match core::str::from_utf8(input) {
        Ok(t) => t,
        Err(_) => return BASE85N_ERR_INVALID_CHAR,
    };
    let decoded = match crate::decode(text) {
        Ok(d) => d,
        Err(e) => return base85n_status::from(&e),
    };

    let buf = malloc_copy(&decoded, false);
    if buf.is_null() {
        return BASE85N_ERR_ALLOC;
    }
    // SAFETY: both out-pointers were checked non-null above, and the
    // caller guarantees they are writable.
    unsafe {
        *out_data = buf;
        *out_len = decoded.len();
    }
    BASE85N_OK
}

/// A short, static, human-readable description of a status code.
///
/// The returned pointer is to static storage: it is never freed and stays
/// valid for the lifetime of the process.
#[no_mangle]
pub extern "C" fn base85n_strerror(status: base85n_status) -> *const c_char {
    let s: &'static str = match status {
        BASE85N_OK => "ok\0",
        BASE85N_ERR_INVALID_CHAR => "invalid character (not in Alphabet-N)\0",
        BASE85N_ERR_UNEXPECTED_EOF => "unexpected end of stream\0",
        BASE85N_ERR_RESERVED_SIGNAL => "reserved/undefined DP signal value\0",
        BASE85N_ERR_INVALID_PARTIAL_BLOCK => "invalid partial final block\0",
        BASE85N_ERR_ALLOC => "memory allocation failure\0",
        BASE85N_ERR_INVALID_ARGUMENT => "invalid argument\0",
    };
    s.as_ptr() as *const c_char
}

#[cfg(test)]
mod tests {
    use super::*;
    use core::ptr;

    extern "C" {
        fn free(p: *mut c_void);
    }

    /// Encode through the C entry point and return the string it produced.
    fn encode_ffi(data: &[u8]) -> Result<String, base85n_status> {
        let mut out: *mut c_char = ptr::null_mut();
        let mut len: usize = 0;
        let st = unsafe { base85n_encode(data.as_ptr(), data.len(), &mut out, &mut len) };
        if st != BASE85N_OK {
            return Err(st);
        }
        let bytes = unsafe { slice::from_raw_parts(out as *const u8, len) };
        let s = String::from_utf8(bytes.to_vec()).expect("encoder emits ASCII");
        // The terminator is there, and is not counted in `len`.
        assert_eq!(unsafe { *out.add(len) }, 0);
        unsafe { free(out as *mut c_void) };
        Ok(s)
    }

    /// Decode through the C entry point and return the bytes it produced.
    fn decode_ffi(text: &[u8]) -> Result<Vec<u8>, base85n_status> {
        let mut out: *mut u8 = ptr::null_mut();
        let mut len: usize = 0;
        let st = unsafe {
            base85n_decode(text.as_ptr() as *const c_char, text.len(), &mut out, &mut len)
        };
        if st != BASE85N_OK {
            return Err(st);
        }
        assert!(!out.is_null(), "a successful decode always yields a pointer");
        let v = unsafe { slice::from_raw_parts(out, len) }.to_vec();
        unsafe { free(out as *mut c_void) };
        Ok(v)
    }

    #[test]
    fn round_trip_matches_the_rust_api() {
        let cases: [&[u8]; 5] = [
            b"",
            b"hello, world!",
            b"{\"id\":184223,\"name\":\"Ada Lovelace\"}",
            &[0u8; 64],
            b"\x00\x01\x02\xfd\xfe\xff",
        ];
        for case in cases {
            let encoded = encode_ffi(case).expect("encoding never fails");
            assert_eq!(encoded, crate::encode(case));
            assert_eq!(decode_ffi(encoded.as_bytes()).unwrap(), case);
        }
    }

    #[test]
    fn empty_input_still_yields_freeable_pointers() {
        assert_eq!(encode_ffi(b"").unwrap(), "");
        assert_eq!(decode_ffi(b"").unwrap(), Vec::<u8>::new());
    }

    #[test]
    fn null_data_is_only_allowed_with_zero_length() {
        let mut out: *mut c_char = ptr::null_mut();
        let mut len: usize = 0;
        assert_eq!(
            unsafe { base85n_encode(ptr::null(), 0, &mut out, &mut len) },
            BASE85N_OK
        );
        unsafe { free(out as *mut c_void) };
        assert_eq!(
            unsafe { base85n_encode(ptr::null(), 1, &mut out, &mut len) },
            BASE85N_ERR_INVALID_ARGUMENT
        );
    }

    #[test]
    fn null_out_pointers_are_rejected_not_dereferenced() {
        let mut len: usize = 0;
        assert_eq!(
            unsafe { base85n_encode(b"x".as_ptr(), 1, ptr::null_mut(), &mut len) },
            BASE85N_ERR_INVALID_ARGUMENT
        );
        let mut out: *mut u8 = ptr::null_mut();
        assert_eq!(
            unsafe { base85n_decode(b"x".as_ptr() as *const c_char, 1, &mut out, ptr::null_mut()) },
            BASE85N_ERR_INVALID_ARGUMENT
        );
    }

    #[test]
    fn decode_errors_map_to_the_c_status_codes() {
        // Not in Alphabet-N; and not valid UTF-8 either, on the second one.
        assert_eq!(decode_ffi(b"ab\x22cd"), Err(BASE85N_ERR_INVALID_CHAR));
        assert_eq!(decode_ffi(b"ab\xffcd"), Err(BASE85N_ERR_INVALID_CHAR));
        // A lone trailing character cannot encode any byte.
        assert_eq!(
            decode_ffi(b"a"),
            Err(BASE85N_ERR_INVALID_PARTIAL_BLOCK)
        );
        for st in [
            BASE85N_OK,
            BASE85N_ERR_INVALID_CHAR,
            BASE85N_ERR_UNEXPECTED_EOF,
            BASE85N_ERR_RESERVED_SIGNAL,
            BASE85N_ERR_INVALID_PARTIAL_BLOCK,
            BASE85N_ERR_ALLOC,
            BASE85N_ERR_INVALID_ARGUMENT,
        ] {
            let p = base85n_strerror(st);
            assert!(!p.is_null());
            assert_ne!(unsafe { *p }, 0, "every status has a description");
        }
    }

    #[test]
    fn text_the_decoder_rejects_leaves_the_out_pointers_alone() {
        let mut sentinel: u8 = 0;
        let untouched: *mut u8 = &mut sentinel;
        let mut out = untouched;
        let mut len: usize = 42;
        let bad = b"a";
        let st = unsafe {
            base85n_decode(bad.as_ptr() as *const c_char, bad.len(), &mut out, &mut len)
        };
        assert_ne!(st, BASE85N_OK);
        assert_eq!(out, untouched);
        assert_eq!(len, 42);
    }
}
