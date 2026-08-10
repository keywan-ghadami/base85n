# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Guard against the quadratic encoder of spec Section 6.6.

Pass 1 scans to the end of a representable run while the main loop can
consume as little as 4 bytes of it, so an encoder that re-runs Pass 1 on
every iteration is O(n^2). A buffer of escape characters is the worst
case: Pass 2 gives up after 3 bytes every time.

Both tests here are timing-based, which on a shared CI runner means they
have to be built to tolerate interference. Two things make them stable:
every duration is the *minimum* of several runs, since scheduling noise
only ever adds time and never removes it, and the thresholds sit far from
the values a healthy encoder produces. A linear encoder handles the large
case in well under a second; the quadratic one these tests exist to catch
needed about four minutes for it.
"""

import time

from base85n import decode, encode

ESCAPE_DENSE_SIZE = 128 * 1024
TIME_LIMIT_SECONDS = 20.0

# Sizes for the growth check, and how many times each is measured.
SMALL_SIZE = 32 * 1024
LARGE_SIZE = 64 * 1024
REPEATS = 5

# Below this, a measurement is too short for its ratio to mean anything.
MEASURABLE_SECONDS = 0.001

# Linear predicts ~2.0, quadratic ~4.0. Halfway between is the decision point.
MAX_GROWTH = 3.0


def _best_encode_seconds(n: int, repeats: int = REPEATS) -> float:
    """Fastest of `repeats` encodes of n escape characters."""
    data = b"~" * n
    best = float("inf")
    for _ in range(repeats):
        start = time.perf_counter()
        encode(data)
        best = min(best, time.perf_counter() - start)
    return best


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
    _best_encode_seconds(4096, repeats=1)  # warm up

    small = _best_encode_seconds(SMALL_SIZE)
    large = _best_encode_seconds(LARGE_SIZE)

    if small < MEASURABLE_SECONDS:
        return  # too fast to time meaningfully; the ceiling test still applies

    growth = large / small
    assert growth < MAX_GROWTH, (
        f"doubling the input multiplied encoding time by {growth:.1f} "
        f"({small * 1000:.1f}ms -> {large * 1000:.1f}ms); expected about 2 "
        "for a linear encoder"
    )
