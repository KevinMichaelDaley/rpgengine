"""Tests for the high-level field synthesiser."""

import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

from texsynth.field_api import synth_field  # noqa: E402

EX = np.random.default_rng(5).random((72, 72, 3))


def test_exact_requested_size_non_multiple():
    f = synth_field(EX, width=100, height=70, patch=24, overlap=12, seed=0)
    assert f.shape == (70, 100, 3)


def test_values_come_from_exemplar():
    # Patches are composited from real exemplar pixels only, so the field's
    # values are a subset of the exemplar's.
    f = synth_field(EX, 80, 80, patch=20, overlap=10, seed=1)
    assert set(np.unique(f)).issubset(set(np.unique(EX)))


def test_deterministic():
    a = synth_field(EX, 60, 60, patch=20, overlap=10, seed=2)
    b = synth_field(EX, 60, 60, patch=20, overlap=10, seed=2)
    assert np.array_equal(a, b)


def test_grayscale_exemplar():
    gray = np.random.default_rng(6).random((64, 64))
    f = synth_field(gray, 50, 50, patch=20, overlap=10, seed=0)
    assert f.shape == (50, 50)
