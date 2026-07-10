"""Tests for the field-baking helpers (config merge, synth, image IO)."""

import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

from texsynth.bake_fields import (  # noqa: E402
    merge_config, bake_field, save_image, load_image, highpass,
)


def test_highpass_removes_low_frequency_variation():
    # A smooth left-to-right brightness ramp with fine noise on top: high-pass
    # must kill the ramp (low-freq) while keeping per-pixel detail variance.
    h, w = 64, 64
    ramp = np.linspace(0.2, 0.8, w)[None, :].repeat(h, axis=0)
    noise = np.random.default_rng(0).normal(0, 0.02, (h, w))
    img = np.clip(ramp + noise, 0, 1)
    out = highpass(img, sigma=8)
    # Column-mean spread (the ramp) collapses; local detail survives.
    assert out.mean(axis=0).std() < 0.35 * img.mean(axis=0).std()
    assert out.std() > 0.01


def test_highpass_zero_is_noop():
    img = np.random.default_rng(1).random((16, 16, 3))
    assert np.array_equal(highpass(img, 0), img)


def test_merge_config_overrides_defaults():
    cfg = merge_config({"colors": 2, "tile_size": 128, "border": 16},
                       {"colors": 3, "tile_size": 256})
    assert cfg["colors"] == 3
    assert cfg["tile_size"] == 256
    assert cfg["border"] == 16          # inherited default


def test_bake_field_shape():
    ex = np.random.default_rng(0).random((64, 64, 3))
    cfg = {"patch": 20, "overlap": 10, "field_size": 50}
    field = bake_field(ex, cfg, seed=0)
    assert field.shape == (50, 50, 3)


def test_save_load_roundtrip(tmp_path):
    arr = np.random.default_rng(0).random((16, 16, 3))
    p = str(tmp_path / "sub" / "x.png")
    save_image(p, arr)
    assert os.path.exists(p)
    back = load_image(p)
    assert back.shape == (16, 16, 3)
    assert 0.0 <= back.min() and back.max() <= 1.0
    assert np.allclose(back, arr, atol=1.0 / 255)   # 8-bit roundtrip tolerance
