"""Tests for irregular polygonal patch synthesis (Kwatra graphcut textures)."""

import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

from texsynth.patch_synth import synth_patchwork  # noqa: E402

EX = np.random.default_rng(11).random((64, 64, 3))


def test_output_shape():
    out = synth_patchwork(EX, width=50, height=40, patch=20, overlap=10, seed=0)
    assert out.shape == (40, 50, 3)


def test_values_come_from_exemplar():
    # Patches are composited from real exemplar pixels only (no blending), so
    # the output values are a subset of the exemplar's.
    out = synth_patchwork(EX, 48, 48, patch=20, overlap=10, seed=1)
    assert set(np.unique(out)).issubset(set(np.unique(EX)))


def test_full_coverage_no_holes():
    # A constant exemplar must fill every pixel (no unwritten/zero holes).
    const = np.full((40, 40, 3), 0.5)
    out = synth_patchwork(const, 55, 47, patch=16, overlap=8, seed=2)
    assert np.allclose(out, 0.5)


def test_deterministic():
    a = synth_patchwork(EX, 44, 44, patch=18, overlap=9, seed=3)
    b = synth_patchwork(EX, 44, 44, patch=18, overlap=9, seed=3)
    assert np.array_equal(a, b)


def test_seed_changes_result():
    a = synth_patchwork(EX, 44, 44, patch=18, overlap=9, seed=3)
    b = synth_patchwork(EX, 44, 44, patch=18, overlap=9, seed=4)
    assert not np.array_equal(a, b)


def test_grayscale_exemplar():
    gray = np.random.default_rng(6).random((48, 48))
    out = synth_patchwork(gray, 40, 40, patch=16, overlap=8, seed=0)
    assert out.shape == (40, 40)


def test_multiple_exemplars_mix_all_variants():
    # A field synthesised from several variants draws pixels from all of them.
    a = np.full((40, 40, 3), 0.1)
    b = np.full((40, 40, 3), 0.6)
    c = np.full((40, 40, 3), 0.9)
    out = synth_patchwork([a, b, c], 200, 200, patch=20, overlap=10, seed=0)
    vals = set(np.unique(out).round(3))
    assert {0.1, 0.6, 0.9}.issubset(vals)          # every variant appears
    assert out.shape == (200, 200, 3)


def test_graphcut_engine_matches_invariants():
    # The slower graphcut engine must satisfy the same shape/coverage/pixel-source
    # guarantees as the default DP engine.
    out = synth_patchwork(EX, 48, 48, patch=20, overlap=10, seed=1, engine="graphcut")
    assert out.shape == (48, 48, 3)
    assert set(np.unique(out)).issubset(set(np.unique(EX)))
    const = np.full((40, 40, 3), 0.25)
    assert np.allclose(synth_patchwork(const, 44, 44, patch=16, overlap=8,
                                       seed=2, engine="graphcut"), 0.25)
