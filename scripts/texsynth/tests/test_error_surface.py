"""Tests for the SSD overlap error surface used by the boundary cut."""

import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

from texsynth.error_surface import ssd  # noqa: E402


def test_ssd_zero_for_identical():
    a = np.array([[1.0, 2.0], [3.0, 4.0]])
    assert np.allclose(ssd(a, a), 0.0)


def test_ssd_grayscale_values():
    a = np.array([[0.0, 0.0]])
    b = np.array([[3.0, 4.0]])
    assert np.allclose(ssd(a, b), [[9.0, 16.0]])


def test_ssd_sums_over_channels():
    a = np.zeros((1, 1, 3))
    b = np.ones((1, 1, 3)) * np.array([1.0, 2.0, 2.0])
    # 1 + 4 + 4 = 9
    assert np.allclose(ssd(a, b), [[9.0]])


def test_ssd_shape_matches_spatial():
    a = np.zeros((5, 7, 3))
    b = np.ones((5, 7, 3))
    assert ssd(a, b).shape == (5, 7)
