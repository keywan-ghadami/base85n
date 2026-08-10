# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Guard against the quadratic encoder of spec Section 6.6.

Pass 1 scans to the end of a representable run while the main loop can
consume as little as 4 bytes of it, so an encoder that re-runs Pass 1 on
every iteration is O(n^2). A buffer of escape characters is the worst
case: Pass 2 gives up after 3 bytes every time.

The time limit is deliberately loose. A linear encoder handles this input
in well under a second; the quadratic one this test exists to catch needed
about four minutes for it, so any bound in between works and a generous one
does not go flaky on a slow or loaded machine.
"""

import time

from base85n import decode, encode

ESCAPE_DENSE_SIZE = 128 * 1024
TIME_LIMIT_SECONDS = 20.0


def test_escape_dense_input_encodes_in_linear_time():
    data = b"~" * ESCAPE_DENSE_SIZE

    start = time.perf_counter()
    encoded = encode(data)
    elapsed = time.perf_counter() - start

    assert decode(encoded) == data
    assert elapsed < TIME_LIMIT_SECONDS, (
        f"encoding {ESCAPE_DENSE_SIZE} escape characters took {elapsed:.1f}s; "
        "this is the signature of the quadratic Pass 1 rescan that spec "
        "Section 6.6 forbids"
    )


def test_escape_dense_growth_is_not_quadratic():
    """Doubling the input should roughly double the time, not quadruple it."""

    def timed(n: int) -> float:
        data = b"~" * n
        start = time.perf_counter()
        encode(data)
        return time.perf_counter() - start

    # Warm up so the first call does not absorb import-time costs.
    timed(4096)

    small = timed(32 * 1024)
    large = timed(64 * 1024)

    # Linear predicts ~2.0, quadratic predicts ~4.0. Anything below 3.0 rules
    # out quadratic growth without being sensitive to ordinary timing noise.
    assert large < small * 3.0, (
        f"doubling the input multiplied encoding time by {large / small:.1f}; "
        "expected about 2 for a linear encoder"
    )
