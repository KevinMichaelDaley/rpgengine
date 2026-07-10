"""Irregular polygonal patch synthesis — Kwatra et al. graphcut textures.

Overlapping exemplar patches are placed across the output and merged with the
graphcut min-cut seam. Because each seam is an arbitrary min-error path, every
patch ends up owning an irregular *polygonal* region — there is no axis-aligned
square grid for the eye to lock onto (the failure mode of square Wang tiles).
Patch source locations are chosen by a deterministic hash, so the result is
reproducible and aperiodic. Real exemplar pixels only; no blending, no blur.
"""

import numpy as np

from .graphcut import graphcut_seam
from .hashing import hash2d


def _positions(extent, patch, step):
    """Start coordinates covering [0, extent) with the given step, edge-flush."""
    if extent <= patch:
        return [0]
    xs = list(range(0, extent - patch + 1, step))
    if xs[-1] != extent - patch:
        xs.append(extent - patch)
    return xs


def _source(exemplar, ph, pw, i, j, seed):
    """Deterministic (hashed) source block of size (ph, pw) from *exemplar*."""
    eh, ew = exemplar.shape[:2]
    h = hash2d(i, j, seed)
    sy = int(h % (eh - ph + 1))
    sx = int((h >> 20) % (ew - pw + 1))
    return exemplar[sy:sy + ph, sx:sx + pw]


def synth_patchwork(exemplar, width, height, patch=192, overlap=None, seed=0):
    """Synthesise a (height, width[, C]) field by graphcut patch placement.

    Args:
        patch:   patch side in px.
        overlap: overlap between neighbours (default patch//2); the wider it is,
                 the more the seam can wander, breaking any residual grid.
    """
    if overlap is None:
        overlap = patch // 2
    step = max(1, patch - overlap)
    multichannel = exemplar.ndim == 3
    shape = (height, width, exemplar.shape[2]) if multichannel else (height, width)
    canvas = np.zeros(shape, dtype=exemplar.dtype)
    filled = np.zeros((height, width), dtype=bool)

    ys = _positions(height, patch, step)
    xs = _positions(width, patch, step)
    for j, py in enumerate(ys):
        ph = min(patch, height - py)
        for i, px in enumerate(xs):
            pw = min(patch, width - px)
            src = _source(exemplar, ph, pw, i, j, seed)
            existing = filled[py:py + ph, px:px + pw]
            if not existing.any():
                canvas[py:py + ph, px:px + pw] = src
            else:
                region = canvas[py:py + ph, px:px + pw]
                sel_ex = existing[..., None] if multichannel else existing
                base = np.where(sel_ex, region, src)     # A defined everywhere
                force_b = ~existing                       # new-only pixels -> new
                force_a = np.zeros((ph, pw), dtype=bool)  # anchor committed side
                if px > 0:
                    force_a[:, 0] |= existing[:, 0]
                if py > 0:
                    force_a[0, :] |= existing[0, :]
                if not force_a.any():
                    force_a = existing & ~force_b
                take_new = graphcut_seam(base, src, force_a, force_b)
                sel = take_new[..., None] if multichannel else take_new
                canvas[py:py + ph, px:px + pw] = np.where(sel, src, base)
            filled[py:py + ph, px:px + pw] = True
    return canvas
