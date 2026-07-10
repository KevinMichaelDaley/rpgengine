"""Paired diffuse + roughness field synthesis with correlated tile selection.

The roughness map should correspond spatially to the diffuse map, not be
independent noise. So we synthesise them together: for each placed diffuse
patch we search — by sliding-window normalised cross-correlation over low-res
copies of all the roughness seed variants, then a full-res refinement — for the
roughness window whose structure best matches the diffuse patch (either
polarity: bright-polished or dark-rough both count). That roughness patch is
laid into the roughness field at the same location, under the *same* seam mask
as the diffuse patch, so the two fields share geometry exactly.

Each roughness tile is then histogram-normalised (matched to the material's
overall mean and spread) so tiles differ only in high-frequency detail — the
overall roughness level stays uniform (no tonal blocks), and the level itself is
set later by the material's roughness parameter.
"""

import numpy as np
from skimage.feature import match_template

from .patch_synth import _merge_dp, _merge_graphcut, _positions, _source


def _luma(img):
    """Luminance (mean of RGB) or the image itself if already 2-D."""
    return img[..., :3].mean(axis=2) if img.ndim == 3 else img


def _downscale(gray, ds):
    """Mean-pool a 2-D array by integer factor *ds* (crops to a multiple)."""
    if ds <= 1:
        return gray
    h, w = (gray.shape[0] // ds) * ds, (gray.shape[1] // ds) * ds
    return gray[:h, :w].reshape(h // ds, ds, w // ds, ds).mean(axis=(1, 3))


def _normalize_tile(patch, tmean, tstd):
    """Match a tile's per-channel mean and spread to the material targets.

    Removes the tile's low-frequency level/contrast (the cause of roughness tonal
    blocks) while preserving its high-frequency detail — the part that carries
    the correspondence with the diffuse. A std floor avoids amplifying flat tiles.
    """
    m = patch.mean(axis=(0, 1), keepdims=True)
    s = np.maximum(patch.std(axis=(0, 1), keepdims=True), 0.02)
    return np.clip((patch - m) * (tstd / s) + tmean, 0.0, 1.0)


def _match_rough(src_a, rough_exs, rough_low, ph, pw, ds):
    """Roughness patch (ph,pw,·) best cross-correlating with the diffuse patch.

    Coarse pass: sliding-window |NCC| of the low-res diffuse-patch luminance over
    each low-res roughness variant picks the variant and approximate location.
    Fine pass: a full-res |NCC| (via match_template on a small crop around the
    coarse peak) snaps to pixel-exact alignment.
    """
    tl = _luma(src_a)
    tmpl_low = _downscale(tl, ds)
    best = None
    if tmpl_low.shape[0] >= 2 and tmpl_low.shape[1] >= 2:
        for k, low in enumerate(rough_low):
            if low.shape[0] < tmpl_low.shape[0] or low.shape[1] < tmpl_low.shape[1]:
                continue
            corr = np.abs(match_template(low, tmpl_low))
            r, c = np.unravel_index(int(np.argmax(corr)), corr.shape)
            if best is None or corr[r, c] > best[0]:
                best = (float(corr[r, c]), k, r, c)
    if best is None:                                     # degenerate fallback
        return rough_exs[0][:ph, :pw]
    _, k, r, c = best
    ex = rough_exs[k]
    cy, cx = r * ds, c * ds
    y0, x0 = max(0, cy - ds), max(0, cx - ds)
    y1, x1 = min(ex.shape[0], cy + ph + ds), min(ex.shape[1], cx + pw + ds)
    crop = _luma(ex[y0:y1, x0:x1])
    if crop.shape[0] > ph and crop.shape[1] > pw:
        res = np.abs(match_template(crop, tl))
        rr, cc = np.unravel_index(int(np.argmax(res)), res.shape)
        by, bx = y0 + rr, x0 + cc
    else:
        by = min(max(cy, 0), ex.shape[0] - ph)
        bx = min(max(cx, 0), ex.shape[1] - pw)
    return ex[by:by + ph, bx:bx + pw]


def synth_paired_field(albedo_exs, rough_exs, width, height, patch=192,
                       overlap=None, seed=0, engine="dp", corr_downscale=8,
                       rough_normalize=True):
    """Synthesise matching (albedo_field, rough_field) of size (height,width,3).

    albedo_exs / rough_exs: lists of same-channel seed variants (each RGB).
    corr_downscale: factor for the low-res correlation search.
    rough_normalize: histogram-match each roughness tile to the material targets
        (uniform level, HF detail kept). Disable for raw correspondence tests.
    """
    if isinstance(albedo_exs, np.ndarray):
        albedo_exs = [albedo_exs]
    if isinstance(rough_exs, np.ndarray):
        rough_exs = [rough_exs]
    smallest = min(min(e.shape[0], e.shape[1]) for e in albedo_exs + rough_exs)
    patch = min(patch, smallest)
    overlap = patch // 2 if overlap is None else min(overlap, patch - 1)
    step = max(1, patch - overlap)
    ds = max(1, corr_downscale)
    rough_low = [_downscale(_luma(r), ds) for r in rough_exs]
    tmean = np.median([r.reshape(-1, r.shape[2]).mean(0) for r in rough_exs], axis=0)
    tstd = float(np.median([r.std() for r in rough_exs]))

    ch = albedo_exs[0].shape[2]
    alb = np.zeros((height, width, ch), dtype=albedo_exs[0].dtype)
    rgh = np.zeros((height, width, ch), dtype=rough_exs[0].dtype)
    filled = np.zeros((height, width), dtype=bool)

    for j, py in enumerate(_positions(height, patch, step)):
        ph = min(patch, height - py)
        for i, px in enumerate(_positions(width, patch, step)):
            pw = min(patch, width - px)
            src_a = _source(albedo_exs, ph, pw, i, j, seed)
            src_r = _match_rough(src_a, rough_exs, rough_low, ph, pw, ds)
            if rough_normalize:
                src_r = _normalize_tile(src_r, tmean, tstd)
            existing = filled[py:py + ph, px:px + pw]
            if not existing.any():
                alb[py:py + ph, px:px + pw] = src_a
                rgh[py:py + ph, px:px + pw] = src_r
            else:
                sel_ex = existing[..., None]
                base_a = np.where(sel_ex, alb[py:py + ph, px:px + pw], src_a)
                base_r = np.where(sel_ex, rgh[py:py + ph, px:px + pw], src_r)
                if engine == "graphcut":
                    mask = _merge_graphcut(base_a, src_a, existing, px > 0, py > 0)
                else:
                    ov_l = min(overlap, pw) if px > 0 else 0
                    ov_t = min(overlap, ph) if py > 0 else 0
                    mask = _merge_dp(base_a, src_a, existing, ov_l, ov_t)
                sel = mask[..., None]
                alb[py:py + ph, px:px + pw] = np.where(sel, src_a, base_a)
                rgh[py:py + ph, px:px + pw] = np.where(sel, src_r, base_r)
            filled[py:py + ph, px:px + pw] = True
    return alb, rgh
