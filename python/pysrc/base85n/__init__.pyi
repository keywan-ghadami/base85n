# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Type stubs for the compiled `base85n` extension module.

The module is built from Rust (see `src/lib.rs`), so nothing in it carries a
signature a type checker can read: without this file `encode` is `Any` and a
typo in an argument name is a runtime error rather than a red squiggle. It is
shipped in the wheel next to the extension, together with the PEP 561 marker.

Keep it in step with `src/lib.rs`; `tests/test_stubs.py` checks that the two
still describe the same module.
"""

from typing import Final, Union

__version__: str
SPEC_VERSION: Final[str]
__all__: list[str]

class Base85NDecodeError(ValueError):
    """Raised by `decode` on malformed input.

    `code` is one of the specification's section 10 conditions, spelled the
    way the shared test vectors spell it: `"invalid_character"`,
    `"unexpected_end_of_stream"`, `"undefined_signal"` or
    `"invalid_final_block"`. `position` is the byte offset at which the
    condition was detected, or `None` where it does not name one.
    """

    code: str
    position: Union[int, None]

def encode(data: Union[bytes, bytearray], /, threads: int = 1) -> str:
    """Encode bytes as a Base85N string.

    Never fails: every byte sequence has an encoding, including the empty one.

    `threads` is a performance knob and nothing else -- every value returns the
    same string. 1 encodes on the calling thread, 0 asks for one worker per
    available core, and inputs below a couple of megabytes ignore it.
    """

def decode(s: Union[str, bytes, bytearray], /) -> bytes:
    """Decode a Base85N string back into the bytes it stands for.

    Raises `Base85NDecodeError` on malformed input. A `bytes` argument is read
    byte by byte, each byte standing for the character of the same value --
    the identity on ASCII, which is all a valid stream can contain.
    """

# Section 4: the alphabet, the R-Set and the donor profiles.
ALPHABET_N: Final[str]
# `bytes`, not a list of ints: the Rust side hands PyO3 a `Vec<u8>`, and that
# is the object PyO3 makes of one.
R_SET: Final[bytes]
PROFILES: Final[list[str]]
NUM_PROFILES: Final[int]

# Sections 6.4 and 9: the thresholds and the signal ranges.
MIN_PASSTHROUGH_BYTES: Final[int]
MAX_DP_ANALYSIS_BYTES: Final[int]
MAX_DP_SEGMENT_CHARS: Final[int]
MIN_FILL_BYTES: Final[int]
MIN_FILL_IN_SEGMENT_BYTES: Final[int]
MAX_FILL_BYTES: Final[int]
MIN_TAIL_ZEROS: Final[int]
MAX_TAIL_ZEROS: Final[int]
DP_SIGNAL_BASE: Final[int]
FILL_SIGNAL_BASE: Final[int]
TAIL_SIGNAL_BASE: Final[int]
FUTURE_SIGNAL_BASE: Final[int]
