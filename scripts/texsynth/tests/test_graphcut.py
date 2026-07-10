"""Tests for the scipy min-cut/max-flow seam (Kwatra graphcut)."""

import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

from texsynth.graphcut import graphcut_seam  # noqa: E402


def _forces(h, w, a_cols, b_cols):
    fa = np.zeros((h, w), bool)
    fb = np.zeros((h, w), bool)
    fa[:, a_cols] = True
    fb[:, b_cols] = True
    return fa, fb


def test_forced_pixels_respected():
    h, w = 3, 5
    a = np.zeros((h, w))
    b = np.ones((h, w))
    fa, fb = _forces(h, w, [0, 1], [3, 4])
    mask = graphcut_seam(a, b, fa, fb)          # True == take B
    assert not mask[:, 0].any() and not mask[:, 1].any()   # forced A
    assert mask[:, 3].all() and mask[:, 4].all()           # forced B


def test_free_pixel_joins_cheaper_seam_side():
    # Free column 2 joins whichever side makes the OTHER boundary cheaper:
    # low diff on col 1 -> cutting col1|col2 is cheap -> col2 becomes B.
    h, w = 3, 5
    a = np.zeros((h, w))
    b = np.array([[0.0, 0.1, 1.0, 9.0, 0.0]]).repeat(h, axis=0)
    fa, fb = _forces(h, w, [0, 1], [3, 4])
    mask = graphcut_seam(a, b, fa, fb)
    assert mask[:, 2].all()                     # joined B

    b2 = np.array([[0.0, 9.0, 1.0, 0.1, 0.0]]).repeat(h, axis=0)
    mask2 = graphcut_seam(a, b2, fa, fb)
    assert not mask2[:, 2].any()                # joined A


def test_deterministic():
    rng = np.random.default_rng(1)
    a = rng.random((8, 8, 3))
    b = rng.random((8, 8, 3))
    fa, fb = _forces(8, 8, [0], [7])
    m1 = graphcut_seam(a, b, fa, fb)
    m2 = graphcut_seam(a, b, fa, fb)
    assert np.array_equal(m1, m2)


def test_multichannel_shape():
    a = np.zeros((6, 6, 3))
    b = np.ones((6, 6, 3))
    fa, fb = _forces(6, 6, [0], [5])
    mask = graphcut_seam(a, b, fa, fb)
    assert mask.shape == (6, 6)
    assert mask.dtype == bool
