// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Python bindings for Base85N, built with PyO3 and packaged by maturin.
//!
//! There is no Python implementation of the format: this module is a thin
//! layer over the `base85n` crate, so what Python runs is byte-for-byte the
//! same encoder and decoder the Rust, and the C ABI, callers get. The layer
//! does three things and nothing else -- convert argument types, release the
//! GIL around the call, and turn a [`DecodeError`] into a Python exception
//! carrying the same error code the shared test vectors use.

use pyo3::create_exception;
use pyo3::exceptions::{PyTypeError, PyValueError};
use pyo3::prelude::*;
use pyo3::types::{PyByteArray, PyBytes, PyString};

use base85n::DecodeError;

create_exception!(
    base85n,
    Base85NDecodeError,
    PyValueError,
    "Raised by decode() on malformed input.\n\n\
     `code` is one of the spec section 10 conditions, as the string the shared\n\
     test vectors use; `position` is the byte offset at which the error was\n\
     detected, or None where the condition does not name one."
);

/// Build the Python exception for a decode failure, carrying `code` and
/// `position` as attributes.
fn decode_error(py: Python<'_>, err: &DecodeError) -> PyErr {
    let py_err = Base85NDecodeError::new_err(err.to_string());
    let value = py_err.value(py);
    // Attribute assignment on a freshly created exception instance cannot
    // fail; if it somehow did, the error itself is still what matters, so the
    // result is deliberately dropped rather than replacing the real error.
    let _ = value.setattr("code", err.code());
    let _ = value.setattr("position", err.position());
    py_err
}

/// The bytes of a `bytes` or `bytearray` argument.
///
/// Both are matched by type rather than by extraction, so that a sequence that
/// merely happens to hold small integers -- a list, a tuple -- is a `TypeError`
/// and not silently an input.
fn byte_argument(obj: &Bound<'_, PyAny>, what: &str) -> PyResult<Vec<u8>> {
    if let Ok(b) = obj.cast::<PyBytes>() {
        return Ok(b.as_bytes().to_vec());
    }
    if let Ok(b) = obj.cast::<PyByteArray>() {
        return Ok(b.to_vec());
    }
    Err(PyTypeError::new_err(what.to_string()))
}

/// Encode bytes as a Base85N string.
///
/// Accepts `bytes` or `bytearray`, and always succeeds: every byte sequence
/// has a Base85N encoding, including the empty one.
///
/// `threads` is a performance knob and nothing else: any value produces the
/// same string, because the format has one canonical encoding and the parallel
/// encoder reproduces it exactly (spec section 11.3). The default of 1 encodes
/// on the calling thread; 0 asks for one worker per available core. Inputs
/// below a couple of megabytes ignore it -- splitting them costs more than it
/// saves.
#[pyfunction]
#[pyo3(signature = (data, /, threads = 1))]
#[pyo3(text_signature = "(data, /, threads=1)")]
fn encode<'py>(
    py: Python<'py>,
    data: &Bound<'py, PyAny>,
    threads: usize,
) -> PyResult<Bound<'py, PyString>> {
    let data = byte_argument(data, "encode() expects bytes or bytearray")?;
    let threads = if threads == 0 {
        std::thread::available_parallelism().map_or(1, |n| n.get())
    } else {
        threads
    };
    // The encoder touches no Python object, so other threads may run while it
    // works -- and so the worker threads it starts are free of the GIL too.
    // That matters here: this is the call a caller makes on a whole file.
    let encoded = py.detach(|| base85n::encode_parallel(&data, threads));
    Ok(PyString::new(py, &encoded))
}

/// Decode a Base85N string back into bytes.
///
/// Accepts `str`, `bytes` or `bytearray`; ASCII is the only encoding a valid
/// Base85N stream can be in. Raises `Base85NDecodeError` on malformed input.
#[pyfunction]
#[pyo3(text_signature = "(s, /)")]
fn decode<'py>(py: Python<'py>, s: &Bound<'py, PyAny>) -> PyResult<Bound<'py, PyBytes>> {
    let text: String = match s.extract::<String>() {
        Ok(text) => text,
        Err(_) => {
            let raw = byte_argument(s, "decode() expects str, bytes or bytearray")?;
            // Every Alphabet-N character is ASCII, so a byte string that is not
            // valid UTF-8 contains a byte the format does not define. Reporting
            // it as the invalid character it is keeps the answer the same as
            // for a `str` argument holding the same text.
            String::from_utf8(raw).map_err(|e| {
                let position = e.utf8_error().valid_up_to();
                let byte = e.as_bytes()[position];
                decode_error(
                    py,
                    &DecodeError::InvalidCharacter { character: byte as char, position },
                )
            })?
        }
    };

    match py.detach(|| base85n::decode(&text)) {
        Ok(bytes) => Ok(PyBytes::new(py, &bytes)),
        Err(e) => Err(decode_error(py, &e)),
    }
}

#[pymodule]
#[pyo3(name = "base85n")]
fn base85n_module(m: &Bound<'_, PyModule>) -> PyResult<()> {
    use base85n::constants as c;

    m.add_function(wrap_pyfunction!(encode, m)?)?;
    m.add_function(wrap_pyfunction!(decode, m)?)?;
    m.add("Base85NDecodeError", m.py().get_type::<Base85NDecodeError>())?;

    m.add("__version__", env!("CARGO_PKG_VERSION"))?;
    m.add("SPEC_VERSION", "0.5.0")?;

    // Section 4: the alphabet, the R-Set and the donor profiles, so that
    // tooling -- the vector generator, the benchmarks -- has one source for
    // them rather than a transcribed copy.
    m.add("ALPHABET_N", std::str::from_utf8(base85n::ALPHABET_N).unwrap())?;
    m.add("R_SET", base85n::RSET_ASCII.to_vec())?;
    m.add(
        "PROFILES",
        base85n::PROFILES
            .iter()
            .map(|p| std::str::from_utf8(p).unwrap())
            .collect::<Vec<_>>(),
    )?;

    // Section 6.4 and section 9.
    m.add("MIN_PASSTHROUGH_BYTES", c::MIN_PASSTHROUGH_BYTES)?;
    m.add("MAX_DP_ANALYSIS_BYTES", c::MAX_DP_ANALYSIS_BYTES)?;
    m.add("MAX_DP_SEGMENT_CHARS", c::MAX_DP_SEGMENT_CHARS)?;
    m.add("MIN_FILL_BYTES", c::MIN_FILL_BYTES)?;
    m.add("MIN_FILL_IN_SEGMENT_BYTES", c::MIN_FILL_IN_SEGMENT_BYTES)?;
    m.add("MAX_FILL_BYTES", c::MAX_FILL_BYTES)?;
    m.add("MIN_TAIL_ZEROS", c::MIN_TAIL_ZEROS)?;
    m.add("MAX_TAIL_ZEROS", c::MAX_TAIL_ZEROS)?;
    m.add("DP_SIGNAL_BASE", c::DP_SIGNAL_BASE)?;
    m.add("FILL_SIGNAL_BASE", c::FILL_SIGNAL_BASE)?;
    m.add("TAIL_SIGNAL_BASE", c::TAIL_SIGNAL_BASE)?;
    m.add("FUTURE_SIGNAL_BASE", c::FUTURE_SIGNAL_BASE)?;
    m.add("NUM_PROFILES", base85n::PROFILES.len())?;

    m.add(
        "__all__",
        vec![
            "__version__",
            "encode",
            "decode",
            "Base85NDecodeError",
            "ALPHABET_N",
            "R_SET",
            "PROFILES",
            "MIN_PASSTHROUGH_BYTES",
            "MAX_DP_ANALYSIS_BYTES",
            "MAX_DP_SEGMENT_CHARS",
            "MIN_FILL_BYTES",
            "MIN_FILL_IN_SEGMENT_BYTES",
            "MAX_FILL_BYTES",
            "MIN_TAIL_ZEROS",
            "MAX_TAIL_ZEROS",
            "DP_SIGNAL_BASE",
            "FILL_SIGNAL_BASE",
            "TAIL_SIGNAL_BASE",
            "FUTURE_SIGNAL_BASE",
            "NUM_PROFILES",
            "SPEC_VERSION",
        ],
    )?;
    Ok(())
}
