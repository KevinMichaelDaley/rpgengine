"""Assemble a small groin-vaulted hall demo from the arch primitives.

A 9 x 6 m hall: one central row of two limestone columns splits it into a
3 x 2 grid of six groin-vault bays; the long walls carry three interior-splay
windows each and the short ends have arched doorways, all with voussoir trim.
Columns/responds = limestone, vaults = limestone tinted to a mid brick/mortar
tone, walls = brick (stone_wall) with limestone sills.

    ns = {}
    exec(open("assets/arch/proc/scene_demo.py").read(), ns)
    ns["build_scene"]()
"""
import math
import os
import struct

import bpy
from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(__file__)) if "__file__" in globals() \
    else "/home/kmd/rpg/assets/arch/proc"
BAKE = os.path.join(HERE, "prefabs", "bake")
DMESH_DIR = "/home/kmd/rpg/datasets/hall_lm"


def _gen_lightmap_uv(obj):
    """Generate a clean, low-fragmentation lightmap UV in a 'lightmap' layer via
    angle-based Smart UV Project (keeps a column/vault/wall as a few large
    islands rather than lightmap_pack's per-face confetti), packed into [0,1].
    Leaves the material UV (layer 0) untouched."""
    me = obj.data
    if len(me.uv_layers) == 0:
        me.uv_layers.new(name="UVMap")  # ensure a material UV exists
    lm = me.uv_layers.get("lightmap")
    if lm is not None:
        me.uv_layers.remove(lm)
    lm = me.uv_layers.new(name="lightmap")
    me.uv_layers.active = lm

    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.smart_project(angle_limit=math.radians(66.0),
                             island_margin=0.02, area_weight=0.0,
                             correct_aspect=True, scale_to_bounds=False)
    bpy.ops.uv.pack_islands(margin=0.02)
    bpy.ops.object.mode_set(mode="OBJECT")


def _export_dmesh(obj, path):
    """Write the evaluated triangulated mesh as a dual-UV .dmesh: uint32
    corner-count, then per corner pos3/nrm3/uv0_2/uv1_2 little-endian floats,
    world-space and Y-up (Blender Z-up -> (x, z, -y)). uv0 = material (layer 0),
    uv1 = lightmap."""
    deps = bpy.context.evaluated_depsgraph_get()
    ev = obj.evaluated_get(deps)
    me = ev.to_mesh()
    me.calc_loop_triangles()
    try:
        me.calc_normals_split()
    except (AttributeError, RuntimeError):
        pass  # Blender 4.1+ computes split normals automatically
    mw = obj.matrix_world
    nmat = mw.to_3x3().inverted_safe().transposed()
    uv0 = me.uv_layers[0].data
    uv1 = (me.uv_layers.get("lightmap") or me.uv_layers[0]).data

    buf = bytearray()
    n = 0
    for tri in me.loop_triangles:
        for k in range(3):
            li = tri.loops[k]
            vi = tri.vertices[k]
            co = mw @ me.vertices[vi].co
            no = (nmat @ Vector(tri.split_normals[k])).normalized()
            a = uv0[li].uv
            b = uv1[li].uv
            buf += struct.pack("<10f", co.x, co.z, -co.y,
                               no.x, no.z, -no.y, a.x, a.y, b.x, b.y)
            n += 1
    ev.to_mesh_clear()
    with open(path, "wb") as f:
        f.write(struct.pack("<I", n))
        f.write(buf)
    return n


def export_scene(col, out_dir=DMESH_DIR):
    """Generate lightmap UVs and export every mesh in *col* to <name>.dmesh."""
    os.makedirs(out_dir, exist_ok=True)
    total = 0
    for obj in list(col.objects):
        if obj.type != "MESH":
            continue
        _gen_lightmap_uv(obj)
        n = _export_dmesh(obj, os.path.join(out_dir, f"{obj.name}.dmesh"))
        total += 1
        print(f"  exported {obj.name}: {n} corners")
    print(f"exported {total} meshes to {out_dir}")
    return total


def _load(name):
    ns = {}
    with open(os.path.join(HERE, name)) as f:
        exec(compile(f.read(), name, "exec"), ns)
    return ns


def _fresh_collection(name):
    old = bpy.data.collections.get(name)
    if old:
        for o in list(old.objects):
            m = o.data
            bpy.data.objects.remove(o, do_unlink=True)
        bpy.data.collections.remove(old)
    col = bpy.data.collections.new(name)
    bpy.context.scene.collection.children.link(col)
    return col


def _to(col, obj, loc, rot_z=0.0):
    """Move an object into *col* at *loc* with an optional Z rotation."""
    for c in list(obj.users_collection):
        c.objects.unlink(obj)
    col.objects.link(obj)
    obj.location = loc
    obj.rotation_euler = (0.0, 0.0, rot_z)
    return obj


def build_scene(materials=True, name="hall_demo"):
    arch = _load("arch.py")
    column = _load("column.py")
    vault = _load("vault.py")
    matn = _load("material_nodes.py") if materials else None
    col = _fresh_collection(name)

    # --- materials -----------------------------------------------------------
    mats = {}
    if materials:
        for n in ("ashlar", "column_stone", "vault_stone", "stone_wall",
                  "voussoir", "sill_limestone"):
            m = bpy.data.materials.get(n)
            if m:
                bpy.data.materials.remove(m)
        # responds = dressed limestone ASHLAR (not brick); interior columns a
        # DARKER stone so they read against the paler vault ceiling.
        mats["ashlar"] = matn["build_field_material"]("ashlar", "limestone")
        mats["column"] = matn["build_field_material"](
            "column_stone", "limestone", tint=(0.56, 0.51, 0.45))
        mats["vault"] = matn["build_field_material"](
            "vault_stone", "limestone", tint=(0.82, 0.74, 0.62))
        mats["wall"] = matn["build_masonry_material"](
            "stone_wall", mask=f"{BAKE}/mask.png", normal=f"{BAKE}/normal.png",
            ao=f"{BAKE}/ao.png", height=f"{BAKE}/height.png",
            tint_map=f"{BAKE}/tint.png")
        mats["voussoir"] = matn["build_voussoir_material"]("voussoir")
        mats["sill"] = matn["build_field_material"]("sill_limestone", "limestone")

    def apply(o, slots):
        me = o.data
        while len(me.materials) < len(slots):
            me.materials.append(None)
        for i, key in enumerate(slots):
            me.materials[i] = mats.get(key) if materials else None

    # --- geometry constants --------------------------------------------------
    BAY = 3.0                    # bay size (m)
    NX, NY = 3, 2               # bays along X, Y
    W, D = NX * BAY, NY * BAY   # interior 9 x 6
    COL_H = 3.5                # column height (capital top == vault springing)
    JAMB = 0.0
    BASE_CLIP = 0.18          # clip the vault base above the springing (kills the
    SPRING = COL_H + JAMB      # corner divots; the clipped rim seats on the impost)
    WALL_T = 0.4
    PANEL_H = SPRING + 1.4     # wall height (up past the crown)

    # --- floor ---------------------------------------------------------------
    bpy.ops.mesh.primitive_plane_add(size=1.0)
    floor = bpy.context.active_object
    floor.name = f"{name}_floor"
    floor.scale = (W + 2.0, D + 2.0, 1.0)
    _to(col, floor, (W * 0.5, D * 0.5, 0.0))
    if materials:
        apply(floor, ["ashlar"])

    # --- columns (2, central row at y = D/2) --------------------------------
    for i, cx in enumerate((BAY, 2 * BAY)):
        c = column["build_column"](
            name=f"{name}_col_{i}", total_height=COL_H, shaft_radius=0.20,
            capital_style="cushion", capital_width=0.60, capital_depth=0.60,
            capital_block_height=0.14, capital_flare=0.5, capital_flare_height=0.34,
            base_block=True, base_width=0.80, base_depth=0.80,
            base_block_height=0.22, base_flare=0.25, base_flare_height=0.10,
            base_moulding="attic", base_moulding_projection=0.5, collection=col)
        _to(col, c, (cx, D * 0.5, 0.0))
        if materials:
            apply(c, ["column"])

    # --- wall responds (thick brick engaged piers at the perimeter grid points) -
    # Romanesque responds are massive: a thick brick shaft engaged in the wall,
    # projecting into the room, catching each long-wall bay corner. Offset inward so
    # the thick half-shaft reads; the two short-wall midpoints are the doorways.
    INSET = 0.22
    # Corner responds sit at a wall-wall intersection, so they inset in BOTH axes
    # (diagonally into the room); mid-wall responds inset only perpendicular.
    def xoff(x):
        return INSET if x == 0.0 else -INSET if x == W else 0.0
    resp_pts = [(x + xoff(x), INSET) for x in (0.0, BAY, 2 * BAY, W)] \
        + [(x + xoff(x), D - INSET) for x in (0.0, BAY, 2 * BAY, W)]
    for i, (rx, ry) in enumerate(resp_pts):
        c = column["build_column"](
            name=f"{name}_resp_{i}", total_height=COL_H, shaft_radius=0.28,
            capital_style="cushion", capital_width=0.74, capital_depth=0.74,
            capital_block_height=0.14, capital_flare=0.5, capital_flare_height=0.30,
            base_block=True, base_width=0.86, base_depth=0.86,
            base_block_height=0.22, base_flare=0.25, base_flare_height=0.10,
            base_moulding="attic", base_moulding_projection=0.5, collection=col)
        _to(col, c, (rx, ry, 0.0))
        if materials:
            apply(c, ["ashlar"])   # dressed limestone ashlar

    # --- groin vaults (6 bays) ----------------------------------------------
    for iy in range(NY):
        for ix in range(NX):
            v = vault["build_groin_vault"](
                name=f"{name}_vault_{ix}_{iy}", bay_width=BAY, bay_depth=BAY,
                arch_shape="round", rise=BAY * 0.5, thickness=0.18,
                jamb_height=JAMB, base_clip=BASE_CLIP, collection=col)
            # seat the clipped base on the impost (base sits at z = SPRING)
            _to(col, v, ((ix + 0.5) * BAY, (iy + 0.5) * BAY, SPRING - BASE_CLIP))
            if materials:
                apply(v, ["vault"])

    # --- long walls (y = 0 and y = D): 3 splayed windows each ---------------
    def wall_panel(nm, opening_kw):
        return arch["build_arched_doorway"](
            name=nm, panel_width=BAY, panel_height=PANEL_H,
            wall_thickness=WALL_T, arch_shape="round",
            reveal_bevel=0.02, masonry_uv=True, voussoir_trim=True,
            trim_width=0.11, trim_extrude=0.05, trim_bevel=0.012,
            collection=col, **opening_kw)

    win_kw = dict(opening_width=0.9, opening_height=1.1, head_rise=0.45,
                  sill_height=1.0, splay=0.18, wide_side="inner",
                  sill_square=True, sill_extrude=0.05)
    for iy_wall, y in ((0, 0.0), (1, D)):
        for ix in range(NX):
            p = wall_panel(f"{name}_win_{ix}_{iy_wall}", win_kw)
            _to(col, p, ((ix + 0.5) * BAY, y, 0.0))
            if materials:
                apply(p, ["wall", "voussoir", "sill"])

    # --- short walls (x = 0 and x = W): one arched doorway each -------------
    door_kw = dict(opening_width=1.1, opening_height=2.1, head_rise=0.5,
                   sill_height=0.0, splay=0.12, wide_side="inner")
    for ix_wall, x in ((0, 0.0), (1, W)):
        p = arch["build_arched_doorway"](
            name=f"{name}_door_{ix_wall}", panel_width=D, panel_height=PANEL_H,
            wall_thickness=WALL_T, arch_shape="round", reveal_bevel=0.02,
            masonry_uv=True, voussoir_trim=True, trim_width=0.11,
            trim_extrude=0.05, trim_bevel=0.012, collection=col, **door_kw)
        _to(col, p, (x, D * 0.5, 0.0), rot_z=math.pi / 2.0)
        if materials:
            apply(p, ["wall", "voussoir", "sill"])

    return col


if __name__ == "__main__":
    _col = build_scene()
    export_scene(_col)
