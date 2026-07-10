"""Tests for paired diffuse+roughness synthesis with correlated tile selection."""

import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

from texsynth.paired_synth import synth_paired_field  # noqa: E402


def _luma(img):
    return img[..., :3].mean(axis=2)


def test_shapes_and_sources():
    a = [np.random.default_rng(k).random((64, 64, 3)) for k in range(2)]
    r = [np.random.default_rng(k + 9).random((64, 64, 3)) for k in range(2)]
    alb, rgh = synth_paired_field(a, r, 96, 96, patch=16, overlap=8, seed=0,
                                  corr_downscale=2, rough_normalize=False)
    assert alb.shape == (96, 96, 3) and rgh.shape == (96, 96, 3)
    assert set(np.unique(alb)).issubset({v for e in a for v in np.unique(e)})
    assert set(np.unique(rgh)).issubset({v for e in r for v in np.unique(e)})


def test_rough_normalization_equalizes_tile_levels():
    # Two roughness variants at very different overall levels; distinct albedo
    # variants so both get used. Without normalization the field shows both
    # levels (blocks); with it, every tile is pulled to the common level.
    a = [np.random.default_rng(0).random((64, 64, 3)),
         1.0 - np.random.default_rng(0).random((64, 64, 3))]
    # two roughness variants at very different levels, each with a little detail
    r = [0.25 + 0.03 * np.random.default_rng(1).random((64, 64, 3)),
         0.85 + 0.03 * np.random.default_rng(2).random((64, 64, 3))]
    _, raw = synth_paired_field(a, r, 160, 160, patch=20, overlap=10, seed=1,
                                corr_downscale=2, rough_normalize=False)
    _, norm = synth_paired_field(a, r, 160, 160, patch=20, overlap=10, seed=1,
                                 corr_downscale=2, rough_normalize=True)
    assert raw.std() > 0.15          # raw field carries both levels (blocks)
    assert norm.std() < 0.08         # normalised field is uniform in level


def test_roughness_follows_diffuse_when_rough_equals_luma():
    # Build rough variants that ARE the luma of the albedo variants. Then the
    # best-correlating rough window for any albedo patch is the matching spot,
    # so the rough field should track the albedo field's luminance closely.
    # Use structured (smooth) content like real material seeds — pure white
    # noise is a pathological case that resists sub-pixel correlation alignment.
    from scipy.ndimage import gaussian_filter
    a = [np.stack([gaussian_filter(np.random.default_rng(k).random((80, 80)), 2.5)] * 3,
                  axis=2) for k in range(3)]
    r = [np.repeat(_luma(x)[..., None], 3, axis=2) for x in a]
    alb, rgh = synth_paired_field(a, r, 128, 128, patch=24, overlap=12, seed=1,
                                  corr_downscale=3, rough_normalize=False)
    la, lr = _luma(alb).ravel(), _luma(rgh).ravel()
    corr = np.corrcoef(la, lr)[0, 1]
    assert corr > 0.9        # rough field spatially matches diffuse


def test_deterministic():
    a = [np.random.default_rng(k).random((64, 64, 3)) for k in range(2)]
    r = [np.random.default_rng(k + 5).random((64, 64, 3)) for k in range(2)]
    f1 = synth_paired_field(a, r, 80, 80, patch=16, overlap=8, seed=2, corr_downscale=2)
    f2 = synth_paired_field(a, r, 80, 80, patch=16, overlap=8, seed=2, corr_downscale=2)
    assert np.array_equal(f1[0], f2[0]) and np.array_equal(f1[1], f2[1])
