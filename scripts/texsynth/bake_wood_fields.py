"""Bake X-tileable, grain-aligned timber fields from the wood seed textures.

Groups ``assetsrc/materials/timber/wood*_{albedo,normal,rough}.webp`` into
variants (``wood``, ``wood2``, ``wood3`` ...), then synthesises one large field
per channel with :func:`wood_synth.synth_wood_aligned` -- seamless when tiled
along X, with the grain flowing across seams (neighbour-aligned patch selection).
All three channels share the same patch geometry. Writes to
``assetsrc/materials/timber/fields/``.

Usage (in the uv venv):
    .venv/bin/python -m scripts.texsynth.bake_wood_fields \
        --width 8192 --height 2048 --patch 208 --overlap 104
"""

import argparse
import glob
import os
import re
import sys
import time

import numpy as np
from PIL import Image

if __package__ in (None, ""):
    sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    from texsynth.wood_synth import synth_wood_aligned
else:
    from .wood_synth import synth_wood_aligned

_CHANNELS = ("albedo", "normal", "rough")


def _load_rgb(path):
    """Load an image as an (H, W, 3) float array in [0, 1]."""
    img = Image.open(path).convert("RGB")
    return np.asarray(img, dtype=np.float32) / np.float32(255.0)


def _save_rgb(path, arr):
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    u8 = np.clip(arr * 255.0 + 0.5, 0, 255).astype(np.uint8)
    Image.fromarray(u8).save(path)


def discover_variants(timber_dir):
    """Group ``<name>_<channel>.<ext>`` files into ``{name: {channel: path}}``,
    keeping only variants that carry every channel in ``_CHANNELS``."""
    variants = {}
    for ext in ("webp", "png", "jpg", "jpeg"):
        for p in glob.glob(os.path.join(timber_dir, f"*_*.{ext}")):
            m = re.match(r"(.+)_(albedo|normal|rough)$",
                         os.path.splitext(os.path.basename(p))[0])
            if m:
                variants.setdefault(m.group(1), {})[m.group(2)] = p
    return {n: ch for n, ch in sorted(variants.items())
            if all(c in ch for c in _CHANNELS)}


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--root", default="assetsrc/materials")
    ap.add_argument("--material", default="timber")
    ap.add_argument("--width", type=int, default=8192)
    ap.add_argument("--height", type=int, default=2048)
    ap.add_argument("--patch", type=int, default=240)
    ap.add_argument("--overlap", type=int, default=150)
    ap.add_argument("--candidates", type=int, default=24)
    ap.add_argument("--tol", type=float, default=0.02)
    ap.add_argument("--band-lo", type=float, default=0.32,
                    help="top of the central beam band (fraction of seed height)")
    ap.add_argument("--band-hi", type=float, default=0.76,
                    help="bottom of the central beam band (fraction of seed height)")
    ap.add_argument("--splits", type=int, default=3,
                    help="sub-tiles to cut each beam into (source-pool variety)")
    ap.add_argument("--run-min", type=int, default=1,
                    help="min columns read from one tile before jumping")
    ap.add_argument("--run-max", type=int, default=1,
                    help="max columns read from one tile before jumping")
    ap.add_argument("--local-norm", type=float, default=150.0,
                    help="sigma (px) for final-field albedo trend removal")
    ap.add_argument("--source-hp", type=float, default=150.0,
                    help="sigma (px) for per-source albedo trend removal")
    ap.add_argument("--density-weight", type=float, default=6.0,
                    help="weight of the grain-density match vs the pixel match")
    ap.add_argument("--density-sigma", type=float, default=12.0,
                    help="sigma (px) of the grain-density measurement")
    ap.add_argument("--no-normalize", dest="normalize", action="store_false",
                    help="skip cross-beam brightness normalisation")
    ap.add_argument("--seed", type=int, default=0)
    args = ap.parse_args(argv)

    tdir = os.path.join(args.root, args.material)
    found = discover_variants(tdir)
    if not found:
        print(f"no complete {_CHANNELS} variants under {tdir}", flush=True)
        return 1
    print(f"variants: {', '.join(found)}", flush=True)

    variants = [{c: _load_rgb(paths[c]) for c in _CHANNELS}
                for paths in found.values()]

    t0 = time.monotonic()
    fields, info = synth_wood_aligned(
        variants, args.width, args.height, patch=args.patch,
        overlap=args.overlap, seed=args.seed, candidates=args.candidates,
        tol=args.tol, band=(args.band_lo, args.band_hi),
        normalize=args.normalize, splits=args.splits,
        run_min=args.run_min, run_max=args.run_max, local_norm=args.local_norm,
        source_hp=args.source_hp, density_weight=args.density_weight,
        density_sigma=args.density_sigma)
    fdir = os.path.join(tdir, "fields")
    for c in _CHANNELS:
        _save_rgb(os.path.join(fdir, f"{args.material}_{c}_field.png"), fields[c])
    print(f"{args.material}: {info['tile_width']}x{info['height']} "
          f"({info['ncols']} cols, patch {info['patch']}"
          f"/ov {info['overlap']}, {len(variants)} variants) "
          f"in {time.monotonic() - t0:.1f}s -> {fdir}", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
