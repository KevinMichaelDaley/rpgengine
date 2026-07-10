"""Irregular patch synthesis — overlapping exemplar patches merged along
minimum-error seams so no axis-aligned grid forms (Efros-Freeman quilting /
Kwatra graphcut textures).

Patches are drawn across a *set* of exemplars (all the seed variants of one
material channel), chosen by a deterministic hash, so a single field mixes every
variant for maximum variation — one aperiodic map per material channel, not per
seed image.

Two seam engines:
  - 'dp'       : Efros-Freeman minimum-error boundary cut (dynamic programming)
                 on the left + top overlap bands. Fast; default for large bakes.
  - 'graphcut' : Kwatra min-cut/max-flow over the whole overlap. Fully arbitrary
                 polygonal seams, higher quality, much slower.

Real exemplar pixels only; no blending, no blur.
"""

import numpy as np

from .error_surface import ssd
from .graphcut import graphcut_seam
from .hashing import hash2d
from .min_cut import min_horizontal_cut, min_vertical_cut


def _positions(extent, patch, step):
    """Start coordinates covering [0, extent) with the given step, edge-flush."""
    if extent <= patch:
        return [0]
    xs = list(range(0, extent - patch + 1, step))
    if xs[-1] != extent - patch:
        xs.append(extent - patch)
    return xs


def _source(exemplars, ph, pw, i, j, seed):
    """Hashed source block (ph, pw): pick a variant, then a region within it."""
    ex = exemplars[hash2d(i, j, seed) % len(exemplars)]
    h = hash2d(i, j, seed ^ 0x1234)
    eh, ew = ex.shape[:2]
    sy = int(h % (eh - ph + 1))
    sx = int((h >> 20) % (ew - pw + 1))
    return ex[sy:sy + ph, sx:sx + pw]


def _merge_dp(base, src, existing, ov_l, ov_t):
    """New-patch mask via Efros-Freeman min-error boundary cuts on the bands."""
    ph, pw = existing.shape
    new_mask = np.ones((ph, pw), dtype=bool)
    if ov_l > 0:
        seam = min_vertical_cut(ssd(base[:, :ov_l], src[:, :ov_l]))     # col per row
        new_mask[:, :ov_l] &= np.arange(ov_l)[None, :] > seam[:, None]
    if ov_t > 0:
        seam = min_horizontal_cut(ssd(base[:ov_t, :], src[:ov_t, :]))   # row per col
        new_mask[:ov_t, :] &= np.arange(ov_t)[:, None] > seam[None, :]
    new_mask |= ~existing        # no old content here -> must take new
    return new_mask


def _merge_graphcut(base, src, existing, has_left, has_top):
    """New-patch mask via the Kwatra min-cut over the full overlap."""
    ph, pw = existing.shape
    force_b = ~existing
    force_a = np.zeros((ph, pw), dtype=bool)
    if has_left:
        force_a[:, 0] |= existing[:, 0]
    if has_top:
        force_a[0, :] |= existing[0, :]
    if not force_a.any():
        force_a = existing & ~force_b
    return graphcut_seam(base, src, force_a, force_b)


def synth_patchwork(exemplars, width, height, patch=192, overlap=None, seed=0,
                    engine="dp"):
    """Synthesise a (height, width[, C]) field by irregular patch placement.

    exemplars: one array, or a list of same-channel arrays (all seed variants).
    engine: 'dp' (fast, default) or 'graphcut' (slower, fully polygonal).
    """
    if isinstance(exemplars, np.ndarray):
        exemplars = [exemplars]
    smallest = min(min(e.shape[0], e.shape[1]) for e in exemplars)
    patch = min(patch, smallest)
    overlap = patch // 2 if overlap is None else min(overlap, patch - 1)
    step = max(1, patch - overlap)
    multichannel = exemplars[0].ndim == 3
    shape = (height, width, exemplars[0].shape[2]) if multichannel else (height, width)
    canvas = np.zeros(shape, dtype=exemplars[0].dtype)
    filled = np.zeros((height, width), dtype=bool)

    for j, py in enumerate(_positions(height, patch, step)):
        ph = min(patch, height - py)
        for i, px in enumerate(_positions(width, patch, step)):
            pw = min(patch, width - px)
            src = _source(exemplars, ph, pw, i, j, seed)
            existing = filled[py:py + ph, px:px + pw]
            if not existing.any():
                canvas[py:py + ph, px:px + pw] = src
            else:
                region = canvas[py:py + ph, px:px + pw]
                sel_ex = existing[..., None] if multichannel else existing
                base = np.where(sel_ex, region, src)
                if engine == "graphcut":
                    new_mask = _merge_graphcut(base, src, existing, px > 0, py > 0)
                else:
                    ov_l = min(overlap, pw) if px > 0 else 0
                    ov_t = min(overlap, ph) if py > 0 else 0
                    new_mask = _merge_dp(base, src, existing, ov_l, ov_t)
                sel = new_mask[..., None] if multichannel else new_mask
                canvas[py:py + ph, px:px + pw] = np.where(sel, src, base)
            filled[py:py + ph, px:px + pw] = True
    return canvas
