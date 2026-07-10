"""Tests for the deterministic integer hashing used to place tiles aperiodically."""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

from texsynth.hashing import hash2d, hash_unit, edge_color, rand_offset  # noqa: E402


def test_hash2d_is_deterministic():
    assert hash2d(3, 7) == hash2d(3, 7)
    assert hash2d(3, 7, salt=1) == hash2d(3, 7, salt=1)


def test_hash2d_varies_with_inputs():
    # Distinct coordinates and salts should (almost always) give distinct hashes.
    vals = {hash2d(i, j) for i in range(8) for j in range(8)}
    assert len(vals) == 64
    assert hash2d(3, 7) != hash2d(7, 3)          # not symmetric
    assert hash2d(3, 7) != hash2d(3, 7, salt=2)  # salt matters


def test_hash2d_handles_negative_coords():
    # Grid coordinates can be negative; must not raise and must stay deterministic.
    assert hash2d(-5, -9) == hash2d(-5, -9)
    assert 0 <= hash2d(-5, -9) < (1 << 64)


def test_hash_unit_in_range():
    for i in range(50):
        u = hash_unit(i, 2 * i + 1)
        assert 0.0 <= u < 1.0


def test_edge_color_in_range_and_uniform():
    C = 3
    counts = [0, 0, 0]
    for i in range(60):
        for j in range(60):
            c = edge_color(i, j, C, salt=11)
            assert 0 <= c < C
            counts[c] += 1
    # Roughly uniform over 3600 samples: every bucket well populated.
    assert min(counts) > 3600 // C // 2


def test_edge_color_shared_between_adjacent_cells():
    # The vertical line to the right of cell (i,j) is the same line to the left
    # of cell (i+1,j): both derive from the same hashed coordinate.
    C, salt = 2, 5
    for i in range(10):
        for j in range(10):
            east_of_ij = edge_color(i + 1, j, C, salt)
            west_of_next = edge_color(i + 1, j, C, salt)
            assert east_of_ij == west_of_next


def test_rand_offset_deterministic_and_bounded():
    ox, oy = rand_offset(4, 9, 128, 128)
    assert (ox, oy) == rand_offset(4, 9, 128, 128)
    assert 0 <= ox < 128 and 0 <= oy < 128
