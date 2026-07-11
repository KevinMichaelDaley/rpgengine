"""Bake a library of hewn-brick prefabs for the wall layout.

Generating a fresh brick for every brick in a wall is far too slow, so instead
we bake a modest library once and let the layout pick from it. For each of a
small discrete set of aspect ratios we build N seeded bricks, apply all
modifiers (so the micro-detail geometry is baked into the exported mesh),
export each to OBJ, and record per-side "colour" metadata that the layout uses
to butt bricks whose facing ends fit together.

Run headless (does not disturb a live Blender session):

    blender --background --factory-startup \
        --python assets/arch/proc/gen_brick_prefabs.py

Environment overrides (handy for quick validation runs):
    PREFAB_N=<int>        bricks per aspect ratio (default 60)
    PREFAB_ASPECTS=<int>  number of leading aspect ratios to bake (default all)
    PREFAB_OUT=<dir>      output directory (default assets/arch/proc/prefabs/bricks)

Side-colour metadata
--------------------
Bricks are laid with their length along X, so the two chiselled ends (+X, -X)
are the faces that butt against horizontal neighbours. For each end we take the
area-weighted mean outward normal of the end band and quantise its (y, z) tilt
into integer bins -> the end "colour". Two facing ends fit when their planes are
parallel, i.e. the right brick's -X tilt bins are the negatives of the left
brick's +X tilt bins. The raw normals are stored too for a fine fallback.
"""
import bpy
import json
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

# Load build_brick from the sibling module without importing it as a package.
_brick_ns = {}
with open(os.path.join(HERE, "brick.py")) as _f:
    exec(compile(_f.read(), "brick.py", "exec"), _brick_ns)
build_brick = _brick_ns["build_brick"]

# (length, height, width) in metres. Height and width are held constant so every
# course is the same bed depth and stacks evenly; only the length (aspect ratio)
# varies across the discrete set.
ASPECTS = [
    (0.32, 0.09, 0.11),
    (0.27, 0.09, 0.11),
    (0.22, 0.09, 0.11),
    (0.17, 0.09, 0.11),
    (0.13, 0.09, 0.11),
]

N_PER = int(os.environ.get("PREFAB_N", "10"))
ASPECT_LIMIT = int(os.environ.get("PREFAB_ASPECTS", str(len(ASPECTS))))
OUT = os.environ.get("PREFAB_OUT", os.path.join(HERE, "prefabs", "bricks"))

# Bin width for quantising end-normal tilt into discrete side "colours".
TILT_BIN = 0.07

# Per-brick sub-centimetre jitter on height and width so no two bricks are the
# exact same bed depth/thickness; the mortar bed absorbs the small differences.
HW_JITTER = 0.004  # metres (max +/- deviation, i.e. under 1 cm total range)


def _end_color(mesh, sign):
    """Area-weighted mean outward normal of the +/-X end band and its quantised
    (ny_bin, nz_bin) tilt colour. ``sign`` is +1 for the +X end, -1 for -X."""
    co = np.array([v.co for v in mesh.vertices], dtype=np.float64)
    xmin, xmax = float(co[:, 0].min()), float(co[:, 0].max())
    length = max(xmax - xmin, 1e-6)
    thresh = xmax - 0.35 * length if sign > 0 else xmin + 0.35 * length

    n_acc = np.zeros(3)
    a_acc = 0.0
    for p in mesh.polygons:
        cx = p.center[0]
        nx = p.normal[0]
        in_band = cx > thresh if sign > 0 else cx < thresh
        faces_out = nx > 0.3 if sign > 0 else nx < -0.3
        if in_band and faces_out:
            n_acc += np.array(p.normal) * p.area
            a_acc += p.area

    if a_acc <= 0.0:
        n = np.array([float(sign), 0.0, 0.0])
    else:
        n = n_acc / a_acc
        norm = np.linalg.norm(n)
        n = n / norm if norm > 0 else np.array([float(sign), 0.0, 0.0])

    color = [int(round(n[1] / TILT_BIN)), int(round(n[2] / TILT_BIN))]
    return n.tolist(), color


def _purge_micro_groups():
    for g in list(bpy.data.node_groups):
        if g.name.endswith("_micro"):
            bpy.data.node_groups.remove(g)


def main():
    os.makedirs(OUT, exist_ok=True)
    scene = bpy.context.scene
    manifest = {"tilt_bin": TILT_BIN, "aspects": [], "bricks": []}
    idx = 0

    for ai, (length, height, width) in enumerate(ASPECTS[:ASPECT_LIMIT]):
        adir = os.path.join(OUT, f"a{ai}")
        os.makedirs(adir, exist_ok=True)
        manifest["aspects"].append(
            {"id": ai, "length": length, "height": height, "width": width})

        for j in range(N_PER):
            seed = ai * 10000 + j
            name = f"brick_a{ai}_{j:03d}"
            # Sub-cm jitter on height/width (own rng so it doesn't perturb the
            # brick's internal shape seed). Length stays exact per aspect.
            jr = np.random.default_rng(seed * 7 + 13)
            h = height + float(jr.uniform(-HW_JITTER, HW_JITTER))
            w = width + float(jr.uniform(-HW_JITTER, HW_JITTER))
            # Slightly jitter the dressing/weathering params per brick so no two
            # stones in the wall are worked identically (crack amount, chamfer,
            # deformation, micro strength, chip sizes).
            jf = lambda base, lo, hi: float(base * jr.uniform(lo, hi))
            obj = build_brick(name=name, seed=seed, length=length,
                              height=h, width=w,
                              disp_scale=jf(0.16, 0.85, 1.18),
                              chamfer_frac=min(0.85, max(0.3, 0.6 + float(
                                  jr.uniform(-0.15, 0.15)))),
                              edge_chip=jf(0.007, 0.7, 1.4),
                              corner_chip=jf(0.02, 0.75, 1.3),
                              cracks=jf(0.6, 0.55, 1.35),
                              cracks2=jf(0.5, 0.6, 1.35),
                              micro=jf(0.65, 0.8, 1.2),
                              micro_env=min(0.97, max(0.8, 0.9 + float(
                                  jr.uniform(-0.06, 0.06)))),
                              collection=scene.collection)
            mesh = obj.data
            n_left, c_left = _end_color(mesh, -1)
            n_right, c_right = _end_color(mesh, +1)
            co = np.array([v.co for v in mesh.vertices], dtype=np.float64)
            bbox = [co.min(0).tolist(), co.max(0).tolist()]

            rel = os.path.join(f"a{ai}", name + ".obj")
            path = os.path.join(OUT, rel)
            for o in bpy.context.selected_objects:
                o.select_set(False)
            obj.select_set(True)
            bpy.context.view_layer.objects.active = obj
            # Identity axes (Y forward, Z up) so OBJ coords match Blender coords
            # and the metadata computed here round-trips on import.
            bpy.ops.wm.obj_export(
                filepath=path, export_selected_objects=True,
                apply_modifiers=False, export_materials=False,
                export_normals=True, export_uv=False,
                forward_axis='Y', up_axis='Z')

            manifest["bricks"].append({
                "id": idx, "aspect": ai, "name": name, "obj": rel,
                "dims": [length, h, w], "bbox": bbox,
                "endL": {"n": n_left, "c": c_left},
                "endR": {"n": n_right, "c": c_right},
            })
            idx += 1

            leftover = obj.data
            bpy.data.objects.remove(obj, do_unlink=True)
            if leftover.users == 0:
                bpy.data.meshes.remove(leftover)
            _purge_micro_groups()

        print(f"[prefabs] aspect {ai} ({length:.2f}m) done: {N_PER} bricks",
              flush=True)

    with open(os.path.join(OUT, "manifest.json"), "w") as f:
        json.dump(manifest, f, indent=1)
    print(f"[prefabs] wrote {idx} bricks -> {OUT}", flush=True)


if __name__ == "__main__":
    main()
