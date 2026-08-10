# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""The codecs Base85N is measured against.

Four established encodings, all reference implementations rather than
reimplementations where the standard library provides one:

  Base64        RFC 4648 -- the baseline nearly everything uses today
  Ascii85       Adobe/btoa, via base64.a85encode, including the classic
                'z' shorthand for an all-zero group
  Z85           ZeroMQ RFC 32; only defined for inputs whose length is a
                multiple of 4, so the benchmark zero-pads to the next
                multiple and charges Z85 for the padding it needs
  Base85 (1924) RFC 1924's alphabet, via base64.b85encode

Every codec here is exercised round-trip by the benchmark, so a bug in
the two hand-written ones (Z85, and the padding-free variants) shows up
as a failure rather than as a flattering number.
"""

from __future__ import annotations

import base64
from dataclasses import dataclass
from typing import Callable

# --- Z85 (ZeroMQ RFC 32) ---------------------------------------------------

Z85_ALPHABET = (
    "0123456789abcdefghijklmnopqrstuvwxyz"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ.-:+=^!/*?&<>()[]{}@%$#"
)
_Z85_DECODE = {c: i for i, c in enumerate(Z85_ALPHABET)}


def z85_encode(data: bytes) -> str:
    """Encode `data`, zero-padding it to a multiple of 4 bytes first.

    Z85 has no partial-group form: it is defined only for inputs whose
    length is a multiple of 4. Applications that need it for arbitrary data
    therefore pad, and that padding is a real cost of choosing Z85, so it is
    charged here rather than reported as "not applicable".

    Padding alone is not sufficient in practice. Because the padding bytes
    are indistinguishable from trailing zeros that belong to the data, the
    original length has to travel alongside the encoded text -- in a length
    field, a framing layer, or a convention that the payload is
    self-delimiting. That extra channel is not counted in the sizes below,
    so the numbers flatter Z85 slightly.
    """
    if len(data) % 4 != 0:
        data = data + b"\x00" * (4 - len(data) % 4)
    out = []
    for i in range(0, len(data), 4):
        value = int.from_bytes(data[i : i + 4], "big")
        chunk = []
        for _ in range(5):
            value, rem = divmod(value, 85)
            chunk.append(Z85_ALPHABET[rem])
        out.append("".join(reversed(chunk)))
    return "".join(out)


def z85_decode(text: str) -> bytes:
    if len(text) % 5 != 0:
        raise ValueError("Z85 input length must be a multiple of 5")
    out = bytearray()
    for i in range(0, len(text), 5):
        value = 0
        for c in text[i : i + 5]:
            value = value * 85 + _Z85_DECODE[c]
        out += value.to_bytes(4, "big")
    return bytes(out)


# --- Base64 ----------------------------------------------------------------


def b64_encode(data: bytes) -> str:
    return base64.b64encode(data).decode("ascii")


def b64_decode(text: str) -> bytes:
    return base64.b64decode(text)


# --- Ascii85 / btoa --------------------------------------------------------


def a85_encode(data: bytes) -> str:
    return base64.a85encode(data).decode("ascii")


def a85_decode(text: str) -> bytes:
    return base64.a85decode(text.encode("ascii"))


# --- RFC 1924 Base85 -------------------------------------------------------


def b85_encode(data: bytes) -> str:
    return base64.b85encode(data).decode("ascii")


def b85_decode(text: str) -> bytes:
    return base64.b85decode(text)


# --- registry --------------------------------------------------------------


@dataclass(frozen=True)
class Codec:
    name: str
    encode: Callable[[bytes], str]
    decode: Callable[[str], bytes]
    # True when the output alphabet avoids the characters that force
    # escaping in JSON strings, XML/HTML text and shell arguments:
    # " ' \ ` < > &
    protocol_safe: bool
    note: str
    # True when encode() zero-pads its input to a multiple of 4 bytes
    # because the format cannot express other lengths. decode() then
    # returns those padding bytes as well: recovering the original length
    # is the caller's problem, not the codec's.
    zero_pads_input: bool = False


def _base85n():
    import base85n

    return Codec(
        name="Base85N",
        encode=base85n.encode,
        decode=base85n.decode,
        protocol_safe=True,
        note="this repository; block mode + Dynamic Passthrough",
    )


def all_codecs() -> list[Codec]:
    return [
        Codec(
            name="Base64",
            encode=b64_encode,
            decode=b64_decode,
            protocol_safe=True,
            note="RFC 4648, the baseline",
        ),
        Codec(
            name="Ascii85",
            encode=a85_encode,
            decode=a85_decode,
            protocol_safe=False,
            note="Adobe/btoa; alphabet includes \" ' \\ ` < > &",
        ),
        Codec(
            name="Z85",
            encode=z85_encode,
            decode=z85_decode,
            protocol_safe=False,
            note=("ZeroMQ RFC 32; 4-byte multiples only, so the benchmark "
                  "zero-pads and charges Z85 for it; alphabet includes < > &"),
            zero_pads_input=True,
        ),
        Codec(
            name="Base85 (RFC 1924)",
            encode=b85_encode,
            decode=b85_decode,
            protocol_safe=False,
            note="RFC 1924 alphabet; includes < > & and more",
        ),
        _base85n(),
    ]
