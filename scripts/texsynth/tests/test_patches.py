"""Tests for exemplar region sampling."""

import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

from texsynth.patches import sample_region, random_patch  # noqa: E402


def test_sample_region_shape_and_bounds():
    ex = np.arange(100 * 80).reshape(100, 80).astype(float)
    rng = np.random.default_rng(0)
    r = sample_region(ex, 10, 12, rng)
    assert r.shape == (10, 12)
    # Values must come from the exemplar (subset of its values).
    assert set(np.unique(r)).issubset(set(np.unique(ex)))


def test_sample_region_multichannel():
    ex = np.zeros((40, 40, 3))
    rng = np.random.default_rng(0)
    r = sample_region(ex, 8, 8, rng)
    assert r.shape == (8, 8, 3)


def test_sample_region_deterministic_with_seed():
    ex = np.random.default_rng(9).random((50, 50))
    a = sample_region(ex, 7, 7, np.random.default_rng(2))
    b = sample_region(ex, 7, 7, np.random.default_rng(2))
    assert np.array_equal(a, b)


def test_random_patch_is_square():
    ex = np.random.default_rng(1).random((64, 64))
    p = random_patch(ex, 16, np.random.default_rng(4))
    assert p.shape == (16, 16)
