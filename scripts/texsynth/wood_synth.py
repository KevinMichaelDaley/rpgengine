"""X-tileable, grain-aligned patch synthesis for timber.

A variant of :mod:`patch_synth` specialised for long timber, where the grain must
run smoothly LEFT-TO-RIGHT with no sudden jumps in height/value. It differs from
the stock sampler in three ways:

1. **Single full-height band.** A beam is one board: its grain spans the whole
   height and only extends along its length. So the field is ONE row of
   full-height patches -- there are no horizontal (vertical-band) seams to jump
   the grain vertically. Each patch is a full-height slice of a seed beam.

2. **Continuation-biased selection.** To keep the grain flowing, each next patch
   is preferentially the *literal continuation* of the current source board
   (same variant, same row, one stride to the right): its left overlap then
   matches the neighbour's right overlap exactly, so the grain carries through
   with zero discontinuity. Only when a board runs out (or at the wrap) do we
   fall back to an Efros-Freeman best-match search across all variants for the
   window whose overlap best continues the grain -- the smoothest available jump.

3. **Tiles along X only.** Columns sit on a *cyclic* lattice (``ncols`` strides
   covering exactly ``ncols*step`` px): the last column's right band wraps onto
   the first column's left band, min-cut stitched, so the field repeats
   seamlessly left-to-right. It does not wrap vertically.

Albedo, normal and roughness stay in lockstep: the window + seam is chosen from
the albedo channel and applied identically to every channel.
"""

import numpy as np
from scipy.ndimage import gaussian_filter, gaussian_filter1d

from .error_surface import ssd
from .min_cut import min_vertical_cut


def _grain_density(luma, sigma):
    """Local grain DENSITY map: the gradient magnitude (how fast the wood value
    changes -- high in tight grain, low in clear wood) smoothed to ``sigma`` px, so
    it measures the coarseness of the figure rather than individual grain lines.
    Used to match a candidate's grain frequency to its neighbour's, not only the
    pixel values -- otherwise a fine window can butt a coarse one at a matching
    seam yet jump in density just past it."""
    gy, gx = np.gradient(luma.astype(np.float64))
    return gaussian_filter(np.hypot(gx, gy), sigma)


def _local_normalize(img, sigma, x_mode="wrap", target_mean=None):
    """Flatten low-frequency brightness TRENDS while keeping grain detail: subtract
    a large-sigma blur (the local mean) and re-add a flat mean -- a high-pass. The
    blur is separable; along X it uses ``x_mode`` ("wrap" for the final tileable
    field, "reflect" for a stand-alone source tile) and reflects along Y. ``sigma``
    px is the trend scale removed; grain (fine, high frequency) passes untouched.
    ``target_mean`` (per-channel) sets the flat level, else the image's own mean."""
    out = img.astype(np.float64)
    mean = out.reshape(-1, out.shape[2]).mean(0) if target_mean is None \
        else np.asarray(target_mean, dtype=np.float64)
    lp = out.copy()
    for c in range(out.shape[2]):
        lp[..., c] = gaussian_filter1d(lp[..., c], sigma, axis=1, mode=x_mode)
        lp[..., c] = gaussian_filter1d(lp[..., c], sigma, axis=0, mode="reflect")
    return np.clip(out - lp + mean, 0.0, 1.0).astype(img.dtype)


def _luma(rgb):
    """Luminance of an (H, W, 3) block (mean of channels)."""
    return rgb[..., :3].mean(axis=2)


def _knot_map(luma, sigma):
    """A "knottiness" map: knots read as blobs markedly DARKER than the surrounding
    clear wood, so we measure how far below a broad local background each pixel
    sits, then smooth to ``sigma`` px (the knot scale). High over knots, ~0 over
    clear grain -- used to keep splits and seams OUT of knots (which look broken
    when cut) and to frame knots inside a patch instead of on its edge."""
    lu = luma.astype(np.float64)
    background = gaussian_filter(lu, sigma * 3.0, mode="reflect")
    darker = np.clip(background - lu, 0.0, None)
    return gaussian_filter(darker, sigma, mode="reflect")


def _crop_band(variants, keys, sel_key, band):
    """Crop every channel of every variant to the central beam ``band`` (a
    fractional ``(lo, hi)`` vertical range), so only real timber is sampled."""
    out = []
    for v in variants:
        h = v[sel_key].shape[0]
        lo, hi = int(round(band[0] * h)), int(round(band[1] * h))
        out.append({k: v[k][lo:hi] for k in keys})
    return out


def _normalize_beams(variants, keys, tmean, tstd, hp_sigma):
    """Match every beam's per-channel mean and spread to the material targets, so
    no beam reads brighter/darker than another. Additionally HIGH-PASS the albedo
    at the source (``hp_sigma`` px, reflect) so each beam is flat in its low
    frequencies -- removing the uneven-lighting brightness blobs the photos carry
    before they can enter the field. NORMAL is left untouched (encodes direction);
    ROUGH gets the mean/spread match but not the high-pass."""
    out = []
    for v in variants:
        nv = {}
        for k in keys:
            if k == "normal":
                nv[k] = v[k]
                continue
            x = v[k].astype(np.float64)
            m = x.reshape(-1, 3).mean(0)
            s = np.maximum(x.reshape(-1, 3).std(0), 1e-3)
            matched = np.clip((x - m) * (tstd[k] / s) + tmean[k], 0.0, 1.0)
            if k == "albedo" and hp_sigma > 0.0:
                matched = _local_normalize(matched, hp_sigma, x_mode="reflect",
                                           target_mean=tmean[k])
            nv[k] = matched.astype(v[k].dtype)
        out.append(nv)
    return out


def _split_beams(variants, keys, sel_key, splits, knot_sigma, patch):
    """Cut each beam into ``splits`` tiles along X for a richer source pool (three
    whole beams are too few to fill a wide field without obvious repetition). Cuts
    are placed in the CLEAREST columns near each even target -- local minima of
    column knottiness -- so a tile boundary never bisects a knot. Every tile is
    kept at least ``patch`` wide (so it can still supply a full window); ``splits``
    is reduced if the beam is too narrow to hold that many."""
    if splits <= 1:
        return list(variants)
    tiles = []
    for v in variants:
        w = v[keys[0]].shape[1]
        s = min(splits, max(1, w // patch))        # tiles must stay >= patch wide
        if s <= 1:
            tiles.append({k: v[k] for k in keys})
            continue
        base = w // s
        colk = _knot_map(_luma(v[sel_key]), knot_sigma).mean(axis=0)
        win = max(0, min(w // (s * 3), (base - patch) // 2))   # jitter, bounded
        cuts = [0]
        for i in range(1, s):
            t = int(round(i * w / s))
            lo = max(cuts[-1] + patch, t - win)    # left tile stays >= patch
            hi = min(w - patch, t + win)           # remaining stays >= patch
            cut = lo + int(np.argmin(colk[lo:hi])) if hi > lo else min(lo, w - patch)
            cuts.append(cut)
        cuts.append(w)
        for a, b in zip(cuts[:-1], cuts[1:]):
            tiles.append({k: v[k][:, a:b] for k in keys})
    return tiles


def _overlap_error(cand_luma, region_luma, existing, overlap, pw, has_right,
                   cand_dens=None, region_dens=None, density_w=0.0,
                   cand_knot=None, knot_w=0.0):
    """How well a candidate continues the existing grain over the filled LEFT band
    (and, for the closing column, the wrapped RIGHT band): the mean pixel SSD PLUS
    a grain-DENSITY term (squared difference of the smoothed density maps over the
    band, weight ``density_w``) so grain coarseness lines up too, PLUS a KNOT
    penalty (weight ``knot_w``) on the candidate's OWN overlap bands so knots are
    framed inside the patch and never land on a seam (where they would be cut)."""
    band = np.zeros(existing.shape, dtype=bool)
    band[:, :overlap] = True
    if has_right:
        band[:, pw - overlap:] = True
    band &= existing
    if not band.any():
        base = 0.0
    else:
        base = float(ssd(cand_luma, region_luma)[band].mean())
        if density_w > 0.0 and cand_dens is not None:
            d = cand_dens - region_dens
            base += density_w * float((d[band] * d[band]).mean())
    if knot_w > 0.0 and cand_knot is not None:
        # both overlap bands (this seam AND the next) should sit in clear wood.
        base += knot_w * float(cand_knot[:, :overlap].mean()
                               + cand_knot[:, pw - overlap:].mean())
    return base


def _seam_mask(region, src, existing, overlap, ph, pw, has_left, has_right):
    """New-patch mask: Efros-Freeman min-error cut on the filled left band and,
    for the closing column, on the wrapped right band. No top band (single row)."""
    new_mask = np.ones((ph, pw), dtype=bool)
    if has_left:
        seam = min_vertical_cut(ssd(region[:, :overlap], src[:, :overlap]))
        new_mask[:, :overlap] &= np.arange(overlap)[None, :] > seam[:, None]
    if has_right:
        seam = min_vertical_cut(ssd(region[:, pw - overlap:], src[:, pw - overlap:]))
        new_mask[:, pw - overlap:] &= np.arange(overlap)[None, :] < seam[:, None]
    new_mask |= ~existing
    return new_mask


def synth_wood_aligned(variants, width, height=None, patch=256, overlap=None,
                       seed=0, candidates=32, tol=0.04, band=(0.32, 0.76),
                       normalize=True, splits=6, run_min=1, run_max=1,
                       local_norm=150.0, source_hp=150.0,
                       density_weight=6.0, density_sigma=12.0,
                       knot_weight=4.0, knot_sigma=24.0):
    """Synthesise X-tileable, grain-aligned timber fields (one full-height band).

    Args:
        variants: list of dicts ``{channel: (H, W, 3) float array}`` (all channels
            of a variant share its geometry). Selection uses ``"albedo"``.
        width: requested field width; snapped DOWN to a whole number of strides so
            it tiles exactly (true width reported in ``info``).
        height: output height. Defaults to the cropped beam-band height (a single
            full-height band). If given and smaller, the band is centre-cropped to
            it; larger values are clamped (a beam is only so tall).
        patch, overlap: horizontal patch / overlap (px). overlap default patch//2.
        seed: deterministic RNG seed.
        candidates: random windows sampled per column for the best-match fallback.
        tol: best-match tolerance for the fallback jump (small -> smoothest).
        band: central beam vertical range (fraction) the seeds are cropped to.

    Returns:
        ``(fields, info)`` -- ``fields`` is ``{channel: (H, Wt, 3)}``; ``info`` has
        ``tile_width``, ``ncols``, ``height``, ``patch``, ``overlap``.
    """
    keys = list(variants[0].keys())
    sel_key = "albedo" if "albedo" in keys else keys[0]
    dtype = variants[0][sel_key].dtype
    variants = _crop_band(variants, keys, sel_key, band)

    # Normalise brightness ACROSS beams (once, at the source), then split each beam
    # into sub-tiles for a richer pool. Doing the level match at the source -- not
    # per placed patch -- means the shifts are absolute, so there is no cumulative
    # drift/sawtooth, and every tile already shares the material's tone.
    if normalize:
        tmean = {k: np.median([v[k].reshape(-1, 3).mean(0) for v in variants],
                              axis=0) for k in keys}
        tstd = {k: np.median([v[k].reshape(-1, 3).std(0) for v in variants],
                             axis=0) for k in keys}
        variants = _normalize_beams(variants, keys, tmean, tstd, source_hp)

    # Clamp the patch to the (whole-beam) width BEFORE splitting, then split so each
    # tile stays >= patch -- otherwise a narrow knot-avoiding tile would shrink the
    # patch and explode the column count.
    patch = min(patch, min(v[sel_key].shape[1] for v in variants))
    variants = _split_beams(variants, keys, sel_key, splits, knot_sigma, patch)

    beam_h = min(v[sel_key].shape[0] for v in variants)
    ph = beam_h if height is None else min(int(height), beam_h)
    overlap = patch // 2 if overlap is None else min(overlap, patch - 1)
    step = max(1, patch - overlap)

    ncols = max(2, int(round(width / step)))
    tile_w = ncols * step
    # Centre each variant's full-height slice vertically (grain at a consistent
    # height across every patch -> no vertical wander).
    sy0 = [(v[sel_key].shape[0] - ph) // 2 for v in variants]
    lumas = [_luma(v[sel_key]) for v in variants]
    dmaps = [_grain_density(lu, density_sigma) for lu in lumas]
    kmaps = [_knot_map(lu, knot_sigma) for lu in lumas]

    fields = {k: np.zeros((ph, tile_w, 3), dtype=dtype) for k in keys}
    filled = np.zeros((ph, tile_w), dtype=bool)

    nvar = len(variants)

    sx_stride = max(1, patch // 48)

    def _bestmatch_sx(vi, region_luma, region_dens, existing, has_right, rng):
        """The source x within tile *vi* whose overlap best continues the grain.
        EXHAUSTIVE over all positions (stride ``sx_stride``) so the seam is as
        invisible as the tile allows, then near-strict (small ``tol``) so we take
        essentially the best match -- diversity comes from the tile bag, not from a
        loose sx (which would reintroduce boundary jumps)."""
        hi = lumas[vi].shape[1] - patch
        if hi < 0:
            return 0
        scored = []
        for sx in range(0, hi + 1, sx_stride):
            cl = lumas[vi][sy0[vi]:sy0[vi] + ph, sx:sx + patch]
            cd = dmaps[vi][sy0[vi]:sy0[vi] + ph, sx:sx + patch]
            ck = kmaps[vi][sy0[vi]:sy0[vi] + ph, sx:sx + patch]
            e = _overlap_error(cl, region_luma, existing, overlap, patch, has_right,
                               cd, region_dens, density_weight, ck, knot_weight)
            scored.append((e, sx))
        scored.sort(key=lambda t: t[0])
        emin = scored[0][0]
        pool = [s for s in scored if s[0] <= emin * (1.0 + tol) + 1e-9]
        return pool[int(rng.integers(0, len(pool)))][1]

    # A shuffled "bag" of source tiles enforces diversity: every tile is drawn
    # before any repeats, so no single board dominates the field. Each drawn tile
    # is read for a short random RUN (run_min..run_max columns) via grain
    # continuation, then we jump to the next bag tile (best-matched at the seam).
    grng = np.random.default_rng((seed ^ 0xA5A5A5) & 0xFFFFFFFF)
    bag = []

    def _next_tile(exclude):
        if not bag:
            order = list(range(nvar))
            grng.shuffle(order)
            bag.extend(order)
        if bag[0] == exclude and nvar > 1:
            for j in range(1, len(bag)):
                if bag[j] != exclude:
                    bag[0], bag[j] = bag[j], bag[0]
                    break
        return bag.pop(0)

    prev = None                                    # (variant_index, source_x)
    run_len = 0
    run_target = 0
    for i in range(ncols):
        px = i * step
        cols = (px + np.arange(patch)) % tile_w
        existing = filled[:, cols]
        has_left = bool(existing[:, :overlap].any())
        is_last = i == ncols - 1
        has_right = is_last and bool(existing[:, patch - overlap:].any())
        region_sel = fields[sel_key][:, cols]
        region_luma = _luma(region_sel)
        region_dens = _grain_density(region_luma, density_sigma)
        rng = np.random.default_rng((seed * 2654435761 ^ (i << 8)) & 0xFFFFFFFF)

        if is_last:
            # Closing column: search EVERY tile for the window that best bridges the
            # left neighbour AND the wrapped first column (pixel + density match).
            best = None
            for vi in range(nvar):
                hi = lumas[vi].shape[1] - patch
                if hi < 0:
                    continue
                for sx in range(0, hi + 1, sx_stride):
                    cl = lumas[vi][sy0[vi]:sy0[vi] + ph, sx:sx + patch]
                    cd = dmaps[vi][sy0[vi]:sy0[vi] + ph, sx:sx + patch]
                    ck = kmaps[vi][sy0[vi]:sy0[vi] + ph, sx:sx + patch]
                    e = _overlap_error(cl, region_luma, existing, overlap, patch,
                                       True, cd, region_dens, density_weight,
                                       ck, knot_weight)
                    if best is None or e < best[0]:
                        best = (e, vi, sx)
            vi, sx = best[1], best[2]
        else:
            # Continue the current board while the run lasts and it hasn't run out;
            # otherwise draw the next diverse tile from the bag and seam onto it.
            cont = False
            if prev is not None and run_len < run_target:
                nsx = prev[1] + step
                if nsx <= lumas[prev[0]].shape[1] - patch:
                    vi, sx = prev[0], nsx
                    run_len += 1
                    cont = True
            if not cont:
                vi = _next_tile(prev[0] if prev is not None else -1)
                sx = _bestmatch_sx(vi, region_luma, region_dens, existing, False,
                                   rng)
                run_len = 1
                run_target = int(grng.integers(run_min, run_max + 1))
        sy = sy0[vi]

        src_sel = variants[vi][sel_key][sy:sy + ph, sx:sx + patch]
        if existing.any():
            base = np.where(existing[..., None], region_sel, src_sel)
            mask = _seam_mask(base, src_sel, existing, overlap, ph, patch,
                              has_left, has_right)
        else:
            mask = np.ones((ph, patch), dtype=bool)
        sel3 = mask[..., None]
        for k in keys:
            src = variants[vi][k][sy:sy + ph, sx:sx + patch]
            region = fields[k][:, cols]
            fields[k][:, cols] = np.where(sel3, src, region)
        filled[:, cols] = True
        prev = (vi, sx)

    # Locally normalise the albedo to remove any residual brightness trend across
    # the field, leaving only grain detail (the level itself is set by the
    # material). Normal/rough are left alone.
    if local_norm > 0.0 and "albedo" in keys:
        fields["albedo"] = _local_normalize(fields["albedo"], local_norm)

    return fields, {"tile_width": tile_w, "ncols": ncols, "height": ph,
                    "patch": patch, "overlap": overlap}
