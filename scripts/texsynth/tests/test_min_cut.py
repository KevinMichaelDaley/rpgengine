"""Tests for the Efros-Freeman minimum-error boundary cut (DP path)."""

import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

from texsynth.min_cut import (  # noqa: E402
    min_vertical_cut, min_horizontal_cut, cut_mask_from_cols,
)


def test_vertical_cut_follows_zero_channel():
    # A winding column of zeros through a field of ones: the min path must ride it.
    err = np.ones((5, 5))
    zero_cols = [2, 2, 1, 1, 0]
    for r, c in enumerate(zero_cols):
        err[r, c] = 0.0
    cols = min_vertical_cut(err)
    assert list(cols) == zero_cols
    # Total cost along the path is zero.
    assert sum(err[r, cols[r]] for r in range(5)) == 0.0


def test_vertical_cut_path_is_connected():
    rng = np.random.default_rng(0)
    err = rng.random((20, 12))
    cols = min_vertical_cut(err)
    assert len(cols) == 20
    for r in range(1, 20):
        assert abs(int(cols[r]) - int(cols[r - 1])) <= 1  # 8-connected in column


def test_vertical_cut_is_optimal_vs_bruteforce():
    # On a tiny grid, DP result must match exhaustive search over connected paths.
    rng = np.random.default_rng(3)
    err = rng.random((4, 3))
    cols = min_vertical_cut(err)
    dp_cost = sum(err[r, cols[r]] for r in range(4))

    best = [float("inf")]

    def walk(r, c, acc):
        acc += err[r, c]
        if r == 3:
            best[0] = min(best[0], acc)
            return
        for nc in (c - 1, c, c + 1):
            if 0 <= nc < 3:
                walk(r + 1, nc, acc)

    for c0 in range(3):
        walk(0, c0, 0.0)
    assert abs(dp_cost - best[0]) < 1e-9


def test_horizontal_cut_matches_transpose():
    rng = np.random.default_rng(7)
    err = rng.random((9, 6))
    rows = min_horizontal_cut(err)
    cols_t = min_vertical_cut(err.T)
    assert list(rows) == list(cols_t)


def test_cut_mask_splits_left_right():
    cols = np.array([1, 2, 0])
    mask = cut_mask_from_cols(cols, width=4)
    # True = new (right) patch, strictly right of the seam column.
    assert mask.tolist() == [
        [False, False, True, True],
        [False, False, False, True],
        [False, True, True, True],
    ]
