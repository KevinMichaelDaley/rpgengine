"""Bake a library of SQUARE dressed-stone TILE prefabs (flagstones / facing
tiles), the flat-slab cousin of the hewn bricks in ``gen_brick_prefabs.py``.

Differences from the brick baker:
  * dimensions are SQUARE in the visible face (length == height) with only a
    small depth -- a slab, not a block;
  * the FLATTEN kernel is cranked up (``flatten_boost``) so the faces read as
    broad, planar dressed surfaces (a worked flagstone), not chiselled facets;
  * far LESS micro displacement -- a smoother stone, only faint tooling.

Everything else (boxy silhouette with crisp arrises, the shared bake/OBJ export
and per-side colour metadata) matches the brick baker, so the same wall / weave
layout machinery can lay these tiles.

Run headless (does not disturb a live Blender session)::

    blender --background --factory-startup \
        --python assets/arch/proc/gen_tile_prefabs.py

Environment overrides:
    PREFAB_N=<int>        tiles per aspect ratio (default 10)
    PREFAB_ASPECTS=<int>  number of leading aspect ratios to bake (default all)
    PREFAB_OUT=<dir>      output directory (default assets/arch/proc/prefabs/tiles)
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

# (length, height, depth) in METRES. SQUARE faces (length == height), a shallow
# depth. Dressed flagstones ~30-50 cm square, ~4-6 cm thick.
ASPECTS = [
    (0.50, 0.50, 0.06),
    (0.42, 0.42, 0.05),
    (0.36, 0.36, 0.05),
    (0.30, 0.30, 0.045),
]

N_PER = int(os.environ.get("PREFAB_N", "10"))
ASPECT_LIMIT = int(os.environ.get("PREFAB_ASPECTS", str(len(ASPECTS))))
OUT = os.environ.get("PREFAB_OUT", os.path.join(HERE, "prefabs", "tiles"))

# Bin width for quantising end-normal tilt into discrete side "colours".
TILT_BIN = 0.07

# Sub-cm jitter on the square edge length + depth so no two tiles are identical.
LH_JITTER = 0.012  # metres (+/- on the square face length/height)
D_JITTER = 0.006   # metres (+/- on the depth)


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

    for ai, (length, height, depth) in enumerate(ASPECTS[:ASPECT_LIMIT]):
        adir = os.path.join(OUT, f"a{ai}")
        os.makedirs(adir, exist_ok=True)
        manifest["aspects"].append(
            {"id": ai, "length": length, "height": height, "width": depth})

        for j in range(N_PER):
            seed = ai * 10000 + j
            name = f"tile_a{ai}_{j:03d}"
            jr = np.random.default_rng(seed * 7 + 13)
            # Keep the face SQUARE: jitter length + height together, depth apart.
            e = float(jr.uniform(-LH_JITTER, LH_JITTER))
            L = length + e
            H = height + e
            D = depth + float(jr.uniform(-D_JITTER, D_JITTER))
            jf = lambda base, lo, hi: float(base * jr.uniform(lo, hi))
            # boxy=1.0 -> crisp square slab; flatten_boost -> broad planar dressed
            # faces; low micro -> only faint tooling (a worked flagstone).
            # Much LIGHTER than a brick: aggressive decimation, coarser remesh
            # (fine_div / micro_voxel_div), and half the sculpt/diffusion steps.
            # BUILD SMALL then SCALE UP: all sculpt/micro kernels are sized in
            # metres, so building at 1/3 scale makes the tooling detail read
            # PROPORTIONALLY COARSER (lower spatial frequency -- a worked flagstone,
            # not a peppered one); we then scale the finished slab back to full size.
            #   disp_scale=0.30  -> deeper MACRO sculpt relief
            #   pit_boost=1.9    -> deeper INWARD micro carves
            #   edge_min=0.14    -> a MILD share of displacement/flatten on the arrises
            #   face_smooth      -> extra broad-face smoothing, jittered per tile so
            #                       some slabs read more worn than others
            S = 1.0 / 3.0
            fsm = float(jr.uniform(0.24, 0.31))   # ~0.275 +/- : worn broad face
            obj = build_brick(name=name, seed=seed,
                              length=L * S, height=H * S, width=D * S,
                              boxy=1.0, flatten_boost=2.3, steps_scale=0.5,
                              disp_scale=0.30, pit_boost=1.9, edge_min=0.14,
                              face_smooth=fsm,
                              decimate=0.55, decimate0=0.22, final_decimate=0.55,
                              fine_div=70.0, micro_voxel_div=120.0,
                              cracks=jf(0.4, 0.55, 1.35),
                              cracks2=jf(0.35, 0.6, 1.35),
                              micro=jf(0.22, 0.8, 1.2),
                              micro_env=min(0.97, max(0.8, 0.9 + float(
                                  jr.uniform(-0.06, 0.06)))),
                              collection=scene.collection)
            # Scale the small slab back up to its true dimensions and bake it in.
            for o in bpy.context.selected_objects:
                o.select_set(False)
            obj.scale = (1.0 / S,) * 3
            obj.select_set(True)
            bpy.context.view_layer.objects.active = obj
            bpy.ops.object.transform_apply(location=False, rotation=False,
                                           scale=True)
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
            bpy.ops.wm.obj_export(
                filepath=path, export_selected_objects=True,
                apply_modifiers=False, export_materials=False,
                export_normals=True, export_uv=False,
                forward_axis='Y', up_axis='Z')

            manifest["bricks"].append({
                "id": idx, "aspect": ai, "name": name, "obj": rel,
                "dims": [L, H, D], "bbox": bbox,
                "endL": {"n": n_left, "c": c_left},
                "endR": {"n": n_right, "c": c_right},
            })
            idx += 1

            leftover = obj.data
            bpy.data.objects.remove(obj, do_unlink=True)
            if leftover.users == 0:
                bpy.data.meshes.remove(leftover)
            _purge_micro_groups()

        print(f"[tiles] aspect {ai} ({length:.2f}m sq) done: {N_PER} tiles",
              flush=True)

    with open(os.path.join(OUT, "manifest.json"), "w") as f:
        json.dump(manifest, f, indent=1)
    print(f"[tiles] wrote {idx} tiles -> {OUT}", flush=True)


if __name__ == "__main__":
    main()
