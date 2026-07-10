"""Bake large aperiodic texture fields from the AI seed images.

Reads a per-material JSON config, synthesises one or more large fields per
material/channel from the seed textures with the Wang-tile quilting sampler, and
writes them under ``assetsrc/materials/<mat>/fields/``. The baked fields are what
the Blender material graph (rpg-lbky) samples like a noise texture.

Usage (in the uv venv):
    .venv/bin/python -m scripts.texsynth.bake_fields \
        --root assetsrc/materials --materials limestone,marble
    # or run the file directly:
    .venv/bin/python scripts/texsynth/bake_fields.py --materials limestone
"""

import argparse
import glob
import json
import os
import sys
import time

import numpy as np
from PIL import Image
from scipy.ndimage import gaussian_filter

if __package__ in (None, ""):
    sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    from texsynth.field_api import synth_field
else:
    from .field_api import synth_field

_CONFIG_DEFAULT = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                               "materials_synth.json")


def merge_config(defaults, override):
    """Return defaults updated with the material-specific override keys."""
    cfg = dict(defaults)
    cfg.update(override or {})
    return cfg


def highpass(exemplar, sigma):
    """Flatten variation coarser than *sigma* px, re-centred on the global mean.

    Quilting reproduces detail seamlessly but leaves independent tiles at
    slightly different overall brightness, which reads as faint tonal blocks on
    seeds with broad low-frequency variation (marble cloud, brick colour drift).
    High-passing removes that large-scale variation so only the crisp detail —
    which the seams hide perfectly — is tiled. sigma <= 0 is a no-op.
    """
    if sigma <= 0:
        return exemplar
    if exemplar.ndim == 3:
        lp = np.stack([gaussian_filter(exemplar[..., c], sigma)
                       for c in range(exemplar.shape[2])], axis=2)
        mean = exemplar.reshape(-1, exemplar.shape[2]).mean(0)
    else:
        lp = gaussian_filter(exemplar, sigma)
        mean = exemplar.mean()
    return np.clip(exemplar - lp + mean, 0.0, 1.0)


def bake_field(exemplar, cfg, seed):
    """Synthesise one square field of side cfg['field_size'] from *exemplar*."""
    n = int(cfg["field_size"])
    exemplar = highpass(exemplar, float(cfg.get("highpass_sigma", 0) or 0))
    overlap = cfg.get("overlap")
    return synth_field(exemplar, n, n, patch=int(cfg["patch"]),
                       overlap=None if overlap is None else int(overlap),
                       seed=seed)


def load_image(path):
    """Load an image as a float array in [0, 1] (grayscale 2-D or RGB 3-D)."""
    img = Image.open(path)
    img = img.convert("L" if img.mode in ("L", "I", "I;16") else "RGB")
    return np.asarray(img, dtype=np.float32) / np.float32(255.0)


def save_image(path, arr):
    """Save a float [0,1] array as an 8-bit PNG, creating parent dirs."""
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    u8 = np.clip(arr * 255.0 + 0.5, 0, 255).astype(np.uint8)
    Image.fromarray(u8).save(path)


def _seeds_for(root, material, channel):
    """Seed image paths for a material/channel (excluding the fields/ output)."""
    hits = []
    for ext in ("png", "jpg", "jpeg"):
        hits += glob.glob(os.path.join(root, material, f"{material}_{channel}_*.{ext}"))
    return sorted(p for p in hits if os.sep + "fields" + os.sep not in p)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--config", default=_CONFIG_DEFAULT)
    ap.add_argument("--root", default="assetsrc/materials")
    ap.add_argument("--materials", default="", help="comma-separated subset")
    ap.add_argument("--channels", default="", help="comma-separated subset")
    ap.add_argument("--base-seed", type=int, default=0)
    ap.add_argument("--field-size", type=int, default=0, help="override field_size")
    ap.add_argument("--num-fields", type=int, default=0, help="override num_fields")
    args = ap.parse_args(argv)

    with open(args.config) as fh:
        conf = json.load(fh)
    defaults = conf.get("defaults", {})
    mats = conf.get("materials", {})
    wanted = [m.strip() for m in args.materials.split(",") if m.strip()] or list(mats)
    chan_filter = {c.strip() for c in args.channels.split(",") if c.strip()}

    for material in wanted:
        cfg = merge_config(defaults, mats.get(material, {}))
        if args.field_size:
            cfg["field_size"] = args.field_size
        num_fields = args.num_fields or int(cfg.get("num_fields", 2))
        channels = [c for c in cfg.get("channels", ["albedo", "rough"])
                    if not chan_filter or c in chan_filter]
        for channel in channels:
            seeds = _seeds_for(args.root, material, channel)
            if not seeds:
                print(f"  skip {material}/{channel}: no seed images", flush=True)
                continue
            for k in range(num_fields):
                exemplar = load_image(seeds[k % len(seeds)])
                t0 = time.monotonic()
                field = bake_field(exemplar, cfg, seed=args.base_seed + k)
                out = os.path.join(args.root, material, "fields",
                                   f"{material}_{channel}_field_{k:02d}.png")
                save_image(out, field)
                print(f"  {out}  {field.shape[1]}x{field.shape[0]}  "
                      f"({time.monotonic() - t0:.1f}s)", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
