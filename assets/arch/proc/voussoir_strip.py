"""Lay baked hewn-brick prefabs into a stack-bonded VOUSSOIR strip, then bake it
to tileable material maps for the arch-trim (archivolt) band.

Where ``brick_wall`` builds a running-bond wall, this builds the strip that wraps
the voussoir trim band: a single ring of stones, each spanning the band's
cross-section (V, from the inner support loop across to the outer) and STACKED
along the arch (U, the arc length) with aligned radial joints -- a stack bond, as
real voussoirs are laid. The band's strip UV (see ``arch._voussoir_strip_uvs``)
is U = arc length, V = unrolled cross-section, so the baked strip tiles straight
around the arch and sill.

The stones are the same prefab library as the wall, rotated 90 degrees about Y so
their LENGTH spans V (the band) and their HEIGHT steps along U (the voussoir
width). The bake reuses ``bake_wall``'s emission-material + orthographic-render
machinery, producing mask / tint / height / normal / ao over ``[0, U] x [0, V]``.

Typical use inside a running Blender session::

    ns = {}
    exec(open("assets/arch/proc/voussoir_strip.py").read(), ns)
    ns["bake_voussoir"](band=0.21, u_tile=2.0, res=1024,
                        out_dir="assetsrc/materials/voussoir/bake")
"""
import math
import os

import bpy
import numpy as np

try:
    HERE = os.path.dirname(os.path.abspath(__file__))
except NameError:
    HERE = "/home/kmd/rpg/assets/arch/proc"
PREFAB_DIR = os.path.join(HERE, "prefabs", "bricks")


def _load_helpers():
    """Exec brick_wall + bake_wall and return their namespaces (their prefab and
    bake machinery is reused verbatim)."""
    bw, bk = {}, {}
    with open(os.path.join(HERE, "brick_wall.py")) as f:
        exec(compile(f.read(), "brick_wall.py", "exec"), bw)
    with open(os.path.join(HERE, "bake_wall.py")) as f:
        exec(compile(f.read(), "bake_wall.py", "exec"), bk)
    return bw, bk


# --------------------------------------------------------------------------
# Strip assembly
# --------------------------------------------------------------------------
def build_voussoir_strip(band=0.21, u_tile=4.0, seed=0, mortar=0.0015,
                         mortar_depth=0.02, depth_jitter=0.022, tilt_deg=2.0,
                         tilt_frac=0.5, seat_jitter=0.0015, mortar_cell=0.006,
                         clench=0.01, name="voussoir_strip", prefab_dir=PREFAB_DIR,
                         collection=None):
    """Assemble a stack-bonded voussoir strip ``u_tile`` (U, arc length) by
    ``band`` (V, cross-section) metres, plus a recessed mortar plane.

    Each stone is a prefab rotated 90 degrees about Y so its LENGTH spans the band
    (Z) and its HEIGHT steps along the arc (X); a stone whose length ~ ``band`` is
    chosen so it reaches across, its hewn ends reading as the joints against the
    inner / outer support loops. Stones are butted along U at ``mortar`` joints
    with NO stagger (a stack bond) and the strip tiles seamlessly along U (the
    first stone is duplicated one tile later). Returns the same dict shape as
    ``build_wall``: collection / mortar / tile_width (== u_tile) / height (band)."""
    bw, _ = _load_helpers()
    manifest = bw["load_manifest"](prefab_dir)
    orientations = bw["build_orientations"](manifest)
    # Prefer the aspect whose length best spans the band, so a single stone reaches
    # across the whole cross-section.
    aspects = manifest["aspects"]
    best_a = min(range(len(aspects)), key=lambda a: abs(aspects[a]["length"] - band))
    pool = [i for i, o in enumerate(orientations)
            if o["brick"]["aspect"] == best_a]
    rng = np.random.default_rng(seed)

    if collection is None:
        collection = bpy.data.collections.new(name)
        bpy.context.scene.collection.children.link(collection)

    mesh_cache = {}
    cursor = 0.0                       # running right edge of the last stone (X = U)
    count = 0
    first_tf = None
    guard = 0
    while cursor < u_tile and guard < 4096:
        guard += 1
        oi = int(rng.choice(pool))
        b = orientations[oi]["brick"]
        mesh = bw["_import_mesh"](b, mesh_cache, prefab_dir)
        lo, hi = b["bbox"]
        h_step = hi[2] - lo[2]         # local Z (height) -> steps along U (world X)
        depth = hi[1] - lo[1]          # local Y (width)  -> relief depth (world Y)
        cx = 0.5 * (lo[0] + hi[0])     # local X (length) -> spans the band (world Z)
        tol = 0.0 if count == 0 else max(
            0.001, mortar * (1.0 + float(rng.uniform(-0.3, 0.3))))
        tilt = bw["_rand_tilt"](rng, tilt_deg, tilt_frac)
        # Rotate 90 about Y: local (x,y,z) -> world (z, y, -x). So local Z runs along
        # world +X (U) and local X (length) along world -Z (the band, V). Place so
        # the stone's min world-X butts to cursor+tol and its band-span centres on V.
        loc_x = (cursor + tol) - lo[2]                     # min world-X = cursor+tol
        # Vary depth for a rich, jagged relief, but keep the front PROUD of the
        # mortar: the recess side is capped below mortar_depth so no stone drops
        # behind the mortar plane and vanishes (occluded) in the front bake; the
        # proud side is free. Clenched neighbours still interpenetrate at the back.
        recess_cap = min(depth_jitter, mortar_depth - 0.006)
        loc_y = -0.5 * (lo[1] + hi[1]) + float(
            rng.uniform(-depth_jitter, recess_cap))         # front face proud toward -Y
        loc_z = 0.5 * band + cx + float(
            rng.uniform(-seat_jitter, seat_jitter))         # band-centre on V
        inst = bpy.data.objects.new(f"{name}_00_{count:04d}", mesh)
        collection.objects.link(inst)
        inst.rotation_euler = (tilt[0], math.pi / 2.0 + tilt[1], tilt[2])
        inst.location = (loc_x, loc_y, loc_z)
        if first_tf is None:
            first_tf = (mesh, inst.rotation_euler.copy(), inst.location.copy())
        # Advance along U by the height extent MINUS `clench`: the U-facing faces are
        # the stones' rounded top/bottom (their tight ends span the band instead), so
        # pulling neighbours together lets their flat middles meet at a thin joint
        # while the rounded corners just interpenetrate.
        cursor = (cursor + tol) + h_step - clench
        count += 1

    # Seamless closer: duplicate the first stone one tile along U.
    if first_tf is not None:
        m0, rot0, loc0 = first_tf
        inst = bpy.data.objects.new(f"{name}_00_{count:04d}_seam", m0)
        collection.objects.link(inst)
        inst.rotation_euler = rot0
        inst.location = (loc0.x + u_tile, loc0.y, loc0.z)
        count += 1

    front_y = -0.5 * manifest["aspects"][0]["width"]   # brick front face (world -Y)
    mortar_obj = bw["_build_mortar"](f"{name}_mortar", 0.0, u_tile, band,
                                     front_y, mortar_depth, mortar_cell, collection)
    return {"collection": collection, "mortar": mortar_obj,
            "tile_width": u_tile, "height": band}


# --------------------------------------------------------------------------
# Bake
# --------------------------------------------------------------------------
def bake_voussoir(band=0.21, u_tile=4.0, seed=0, res=4096, out_dir=None,
                  clump=0.7, mortar_depth=0.02, prefab_dir=PREFAB_DIR,
                  name="voussoir_strip"):
    """Build a voussoir strip then bake mask / tint / height / normal / ao over
    its tile ``[0, u_tile] x [0, band]``, reusing bake_wall's machinery. Returns a
    dict of written file paths."""
    _, bk = _load_helpers()
    if out_dir is None:
        out_dir = os.path.join(HERE, "prefabs", "bake_voussoir")
    os.makedirs(out_dir, exist_ok=True)

    strip = build_voussoir_strip(band=band, u_tile=u_tile, seed=seed,
                                 mortar_depth=mortar_depth, prefab_dir=prefab_dir,
                                 name=name)
    col, mortar = strip["collection"], strip["mortar"]
    W, Hh = strip["tile_width"], strip["height"]
    bricks = [o for o in col.objects if o is not mortar]

    wall_objs = set(col.objects)
    for o in bpy.data.objects:
        o.hide_render = o not in wall_objs
    bk["_enable_gpu"]()
    cam = bk["_ortho_cam"](W, Hh)
    rx, ry = bk["_res"](W, Hh, res)
    out = {}

    bk["_tint_bricks"](bricks)
    bk["_assign"]([mortar], bk["_flat_mat"]("bake_black", 0.0))
    out["mask"] = bk["_render"](cam, rx, ry, os.path.join(out_dir, "mask.png"),
                                engine='CYCLES', samples=16)

    trng = np.random.default_rng(seed * 131 + 7)
    for o in bricks:
        v = float(trng.uniform(0.12, 1.0))
        o.color = (v, v, v, 1.0)
    bk["_assign"](bricks, bk["_objcolor_mat"]("bake_objcolor"))
    bk["_assign"]([mortar], bk["_flat_mat"]("bake_black", 0.0))
    out["tint"] = bk["_render"](cam, rx, ry, os.path.join(out_dir, "tint.png"),
                                engine='CYCLES', samples=8)

    mask = bk["_load_gray"](out["mask"])
    bed_px = max(4.0, (mortar_depth * 3.0) / Hh * ry)
    field = bk["compute_clump"](mask, run_thresh_px=bed_px * 1.6, propagate=3,
                                seed=seed, base_cells=6)
    bk["displace_mortar"](mortar, field, W, Hh, amp=mortar_depth * 0.8 * clump)

    from mathutils import Vector
    y_front = min((o.matrix_world @ Vector(corner)).y
                  for o in bricks for corner in o.bound_box)
    y_back = float(mortar.data.vertices[0].co.y)

    bk["_assign"](col.objects, bk["_height_mat"]("bake_height", y_front, y_back))
    out["height"] = bk["_render"](cam, rx, ry, os.path.join(out_dir, "height.png"),
                                  engine='CYCLES', samples=16)
    bk["_assign"](col.objects, bk["_normal_mat"]("bake_normal"))
    out["normal"] = bk["_render"](cam, rx, ry, os.path.join(out_dir, "normal.png"),
                                  engine='CYCLES', samples=16)
    bk["_assign"](col.objects, bk["_ao_mat"]("bake_ao", distance=0.06, samples=8))
    out["ao"] = bk["_render"](cam, rx, ry, os.path.join(out_dir, "ao.png"),
                              engine='CYCLES', samples=64, raw=False)
    return out


if __name__ == "__main__":
    bake_voussoir()
