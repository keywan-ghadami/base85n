# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Guard against the rescanning encoder of spec Section 6.6.

Step 1 scans up to MAX_DP_ANALYSIS_BYTES bytes for each of the eight
alphabets, while step 2.b may consume as few as 4 bytes, so an encoder that
redoes those scans on every iteration performs 2048 byte inspections per
input byte. Bounded lookahead keeps that linear rather than quadratic --
unlike version 0.2.0 -- but a constant factor of 2048 is still what this
section exists to prevent.

Pseudorandom bytes are the worst case: no alphabet reaches
MIN_PASSTHROUGH_BYTES, so every iteration takes the block-mode branch and
advances 4 bytes, while a naive implementation rescans the full window each
time.

Both tests here are timing-based, which on a shared CI runner means they
have to be built to tolerate interference. Two things make them stable:
every duration is the *minimum* of several runs, since scheduling noise
only ever adds time and never removes it, and the thresholds sit far from
the values a healthy encoder produces.
"""

import random
import time

from base85n import decode, encode

SCAN_DENSE_SIZE = 128 * 1024
TIME_LIMIT_SECONDS = 20.0

# Sizes for the growth check, and how many times each is measured.
SMALL_SIZE = 32 * 1024
LARGE_SIZE = 64 * 1024
REPEATS = 5

# Below this, a measurement is too short for its ratio to mean anything.
MEASURABLE_SECONDS = 0.001

# Linear predicts ~2.0, quadratic ~4.0. Halfway between is the decision point.
MAX_GROWTH = 3.0


def _scan_dense(n: int) -> bytes:
    """Input on which no alphabet ever reaches MIN_PASSTHROUGH_BYTES."""
    rng = random.Random(f"base85n-complexity:{n}")
    return bytes(rng.randrange(256) for _ in range(n))


def _best_encode_seconds(n: int, repeats: int = REPEATS) -> float:
    """Fastest of `repeats` encodes of n scan-dense bytes."""
    data = _scan_dense(n)
    best = float("inf")
    for _ in range(repeats):
        start = time.perf_counter()
        encode(data)
        best = min(best, time.perf_counter() - start)
    return best


def test_scan_dense_input_encodes_in_linear_time():
    data = _scan_dense(SCAN_DENSE_SIZE)

    start = time.perf_counter()
    encoded = encode(data)
    elapsed = time.perf_counter() - start

    assert decode(encoded) == data
    assert elapsed < TIME_LIMIT_SECONDS, (
        f"encoding {SCAN_DENSE_SIZE} scan-dense bytes took {elapsed:.1f}s; "
        "this is the signature of the per-iteration rescan that spec "
        "Section 6.6 forbids"
    )


def test_scan_dense_growth_is_not_quadratic():
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


def test_long_representable_run_encodes_in_linear_time():
    """The other direction: one long run that DP takes 1024 bytes at a time."""
    data = b"the quick brown fox jumps over the lazy dog. " * 4000

    start = time.perf_counter()
    encoded = encode(data)
    elapsed = time.perf_counter() - start

    assert decode(encoded) == data
    assert elapsed < TIME_LIMIT_SECONDS
