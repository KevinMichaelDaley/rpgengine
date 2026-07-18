"""Romanesque great hall -- a demo COMPOSITION of the arch-primitive library
(ticket rpg-pm1c). Not a single primitive: it assembles arched windows
(``arch.build_arched_doorway``), engaged wall piers (``pier.build_wall_pier``),
a raised dais, and a high open TIMBER roof into one great hall.

Great-hall character (see the ref):
  * a large OPEN space -- no internal columns dividing it;
  * a high open TIMBER roof (king-post trusses + purlins), not a vault;
  * elaborate archwork -- an arcade of deep single-splayed round windows;
  * a raised DAIS at the lord's end, up two steps, with a blind arch behind.

Romanesque light windows are single-splayed: a NARROW aperture + voussoir
archivolt on the EXTERIOR, splaying WIDE into the interior. So each window panel
is oriented voussoir-face-out (the north panels are turned 180) and built with
``wide_side="outer"`` (wide reveal on the room side).

Runs through the Blender MCP bridge: ``build_great_hall()`` builds the scene into
a fresh collection. The timber is sized from the walls' ACTUAL world bounding
boxes (never assumed), so every tie/rafter lands exactly on its support.
"""
import math
import os

import bpy
import mathutils
import numpy as np


def _link(col, obj):
    for c in list(obj.users_collection):
        c.objects.unlink(obj)
    col.objects.link(obj)


def _box(col, name, cx, cy, cz, sx, sy, sz):
    """Axis-aligned box of EXACT dimensions (sx,sy,sz) centred at (cx,cy,cz).
    Uses a size=2 cube so the /2 scale gives the true size (a size=1 cube would
    come out half-size)."""
    bpy.ops.mesh.primitive_cube_add(size=2.0, location=(cx, cy, cz))
    o = bpy.context.active_object
    o.name = name
    o.scale = (sx / 2.0, sy / 2.0, sz / 2.0)
    bpy.ops.object.transform_apply(scale=True)
    _link(col, o)
    return o


def _strut(col, name, p0, p1, t):
    """A square-section beam of side @p t running exactly from p0 to p1 (its
    local Z axis is aligned to the p0->p1 direction, length = |p1-p0|)."""
    p0 = mathutils.Vector(p0)
    p1 = mathutils.Vector(p1)
    d = p1 - p0
    bpy.ops.mesh.primitive_cube_add(size=2.0, location=(p0 + p1) / 2.0)
    o = bpy.context.active_object
    o.name = name
    o.rotation_mode = 'QUATERNION'
    o.rotation_quaternion = d.to_track_quat('Z', 'Y')
    o.scale = (t / 2.0, t / 2.0, d.length / 2.0)
    _link(col, o)
    return o


def _world_bbox(objs):
    pts = []
    for o in objs:
        for c in o.bound_box:
            w = o.matrix_world @ mathutils.Vector(c)
            pts.append((w.x, w.y, w.z))
    a = np.array(pts)
    return a.min(0), a.max(0)


def _fireplace(col, name, wx, wall_inner_y, chimney_top_z, sign):
    """A hooded wall fireplace against a side wall at x=@p wx. @p wall_inner_y is
    the wall's inner (room) face; @p sign is +1 when the room is on the -Y side
    of that face (north wall). The fire OPENING is a real arched niche built with
    the arch primitive (round arch + voussoir archivolt); the projecting hood +
    chimney stack are ONE merged, all-quad lofted shell (rectangular section
    rings bridged: wide hood mouth -> narrow throat -> stack through the roof)."""
    import arch
    import bmesh
    yin = wall_inner_y
    proj = -sign                       # into-room direction along Y
    fw = 3.0                           # surround width (X): room for the orders
    surround_h = 2.7                    # taller surround panel
    surround_t = 0.7

    def yb(off):                       # Y `off` m into the room from the wall face
        return yin + proj * off

    # hearth slab
    _box(col, f"{name}_fp_hearth", wx, yb(0.75), 0.09, fw + 0.5, 1.5, 0.18)

    # arched fire opening: a blind arched niche (the firebox), voussoirs on the
    # ROOM face. Panel back sits on the wall; it projects into the room.
    fire = arch.build_arched_doorway(
        name=f"{name}_fp_opening", panel_width=fw, panel_height=surround_h,
        wall_thickness=surround_t, arch_shape="flat", opening_width=1.5,
        opening_height=1.75, head_rise=0.0, sill_height=0.0, splay=0.0,
        blind=True, blind_recess=surround_t * 0.7, voussoir_trim=True,
        trim_width=0.24, collection=col)   # square-topped opening, WIDE voussoir
    # front (voussoir) face is -Y; for the north wall the room is at -Y, so no
    # rotation. Seat the back on the wall, projecting `surround_t` into the room.
    fire.location = (wx, yin + proj * (surround_t * 0.5), 0.0)
    if sign < 0:
        fire.rotation_euler = (0, 0, math.pi)   # south wall: face +Y (its room)

    # merged all-quad hood + chimney: bridge rectangular section rings.
    yface = yin                        # back of the mass sits on the wall face
    rings_spec = [                     # (z, half_width X, projection into room)
        (surround_h,             fw / 2,  0.75),   # hood mouth (wide, projecting)
        (surround_h + 1.7,       0.48,    0.30),   # throat
        (chimney_top_z,          0.48,    0.30),   # stack top (through the roof)
    ]
    bm = bmesh.new()
    rings = []
    for z, hw, p in rings_spec:
        yf = yface + proj * p
        rings.append([bm.verts.new((wx - hw, yface, z)),
                      bm.verts.new((wx + hw, yface, z)),
                      bm.verts.new((wx + hw, yf, z)),
                      bm.verts.new((wx - hw, yf, z))])
    for i in range(len(rings) - 1):
        for j in range(4):
            k = (j + 1) % 4
            bm.faces.new((rings[i][j], rings[i][k], rings[i + 1][k], rings[i + 1][j]))
    bm.faces.new(tuple(rings[-1]))     # cap the stack top
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    me = bpy.data.meshes.new(f"{name}_fp_hood"); bm.to_mesh(me); bm.free()
    hood = bpy.data.objects.new(f"{name}_fp_hood", me); col.objects.link(hood)

    # concentric ⊓ orders around the square opening, all built by the arch script
    # so they register exactly with the flat head and mitre crisply at the corners,
    # then transformed onto the fireplace like `fire`:
    #   1. a carved CHEVRON order between the voussoir and the label (the
    #      "decorative thing" framing the arch), then
    #   2. the outer LABEL MOULD (hood mould) with label-stop returns.
    def _order(obj):
        obj.location = fire.location.copy()
        obj.rotation_euler = fire.rotation_euler.copy()
        return obj

    # thin inner roll moulding framing the inner edge of the chevron order
    _order(arch.build_arch_label(
        name=f"{name}_fp_roll", opening_width=1.5, opening_height=1.75,
        arch_shape="flat", head_rise=0.0, sill_height=0.0,
        wall_thickness=surround_t, inner_radius=0.22, width=0.05, depth=0.09,
        rim_style="ovolo", rim_size=0.02, jamb_extension=0.55,
        face="inner", collection=col))

    _order(arch.build_arch_label(
        name=f"{name}_fp_chevron", opening_width=1.5, opening_height=1.75,
        arch_shape="flat", head_rise=0.0, sill_height=0.0,
        wall_thickness=surround_t, inner_radius=0.28, width=0.16, depth=0.05,
        pattern="chevron", pattern_width=0.16, pattern_relief=0.05,
        pattern_count=12, jamb_extension=0.55, face="inner", collection=col))

    _order(arch.build_arch_label(
        name=f"{name}_fp_label", opening_width=1.5, opening_height=1.75,
        arch_shape="flat", head_rise=0.0, sill_height=0.0,
        wall_thickness=surround_t, inner_radius=0.48, width=0.18, depth=0.14,
        rim_style="ovolo", rim_size=0.04, jamb_extension=0.55,
        face="inner", collection=col))


def build_great_hall(name="great_hall", nbay=5, bay=3.6, width=8.0, wall_h=6.5,
                     wall_t=0.5, roof_rise=3.6, collection=None):
    """Build the great hall into (a fresh child of) @p collection. Returns the
    collection. @p nbay bays of @p bay metres give the length; @p width is the
    clear span; @p wall_h the wall height; @p roof_rise the timber-roof apex
    above the walls."""
    import arch
    import pier

    length = nbay * bay
    half = width / 2.0

    if collection is None:
        col = bpy.data.collections.new(name)
        bpy.context.scene.collection.children.link(col)
    else:
        col = collection

    # --- floor ---
    _box(col, f"{name}_floor", length / 2.0, 0.0, -0.1,
         length + 2 * wall_t, width + 2 * wall_t, 0.2)

    # --- side walls: a bay-by-bay arcade of deep single-splayed light windows.
    #     Each panel's voussoir/front face points OUTWARD (north panels turned
    #     180); wide_side="outer" splays the reveal wide on the room side. ---
    def window(nm):
        return arch.build_arched_doorway(
            name=nm, panel_width=bay, panel_height=wall_h, wall_thickness=wall_t,
            arch_shape="round", opening_width=1.15, opening_height=2.5,
            head_rise=0.58, sill_height=2.0, splay=0.34, wide_side="outer",
            reveal_bevel=0.025, voussoir_trim=True, trim_width=0.12,
            trim_extrude=0.05, masonry_uv=True, collection=col)

    # The fireplace occupies the last north bay (by the dais) -- skip that window
    # so they don't intersect.
    fp_bay = nbay - 1
    fp_wx = (fp_bay + 0.5) * bay
    for i in range(nbay):
        if i != fp_bay:
            n = window(f"{name}_win_N_{i}")
            n.location = ((i + 0.5) * bay, half, 0.0)
            n.rotation_euler = (0, 0, math.pi)      # voussoir face -> +Y (exterior)
        s = window(f"{name}_win_S_{i}")
        s.location = ((i + 0.5) * bay, -half, 0.0)
        s.rotation_euler = (0, 0, 0.0)              # voussoir face -> -Y (exterior)
    # solid wall behind the fireplace bay (a plain box on the same bay grid /
    # thickness as the window panels), so the flue has a wall to back onto.
    _box(col, f"{name}_wall_N_{fp_bay}", fp_wx, half, wall_h / 2.0,
         bay, wall_t, wall_h)

    # --- engaged piers at every bay division, projecting INTO the hall, rising
    #     nearly to the wall head where the roof trusses land. ---
    pier_h = wall_h - 0.1
    for i in range(nbay + 1):
        pn = pier.build_wall_pier(
            name=f"{name}_pier_N_{i}", width=0.7, depth=0.55, height=pier_h,
            plinth_height=0.5, plinth_project=0.11, collection=col)
        pn.location = (i * bay, half - wall_t * 0.5, 0.0)
        pn.rotation_euler = (0, 0, math.pi)         # project toward -Y (into room)
        ps = pier.build_wall_pier(
            name=f"{name}_pier_S_{i}", width=0.7, depth=0.55, height=pier_h,
            plinth_height=0.5, plinth_project=0.11, collection=col)
        ps.location = (i * bay, -half + wall_t * 0.5, 0.0)

    # --- end walls: grand arched entrance (near) + a blind-arch dais backdrop. ---
    d = arch.build_arched_doorway(
        name=f"{name}_entrance", panel_width=width, panel_height=wall_h,
        wall_thickness=wall_t, arch_shape="round", opening_width=2.2,
        opening_height=3.4, head_rise=1.1, sill_height=0.0, splay=0.16,
        wide_side="inner", reveal_bevel=0.03, voussoir_trim=True, trim_width=0.14,
        masonry_uv=True, collection=col)
    d.location = (0.0, 0.0, 0.0)
    d.rotation_euler = (0, 0, math.pi / 2.0)
    b = arch.build_arched_doorway(
        name=f"{name}_dais_arch", panel_width=width, panel_height=wall_h,
        wall_thickness=wall_t, arch_shape="round", opening_width=3.0,
        opening_height=3.0, head_rise=1.5, sill_height=0.9, splay=0.10,
        wide_side="inner", blind=True, blind_recess=0.22, voussoir_trim=True,
        trim_width=0.14, masonry_uv=True, collection=col)
    b.location = (length, 0.0, 0.0)
    b.rotation_euler = (0, 0, math.pi / 2.0)

    # --- dais + two steps at the far (lord's) end ---
    # The dais runs all the way back to the end wall (back face at x=length) and
    # its top (z=0.46) sits just BELOW the pier plinth tops (z=0.5) so the two
    # surfaces do not z-fight where the plinths meet the platform.
    dais_d = 3.6
    dais_w = width - 1.0
    dais_top = 0.46
    dais_front = length - dais_d                    # room-side edge of the dais
    _box(col, f"{name}_dais", length - dais_d / 2.0, 0.0, dais_top / 2.0,
         dais_d, dais_w, dais_top)
    for s2 in range(2):
        _box(col, f"{name}_dais_step_{s2}",
             dais_front - 0.4 * (s2 + 0.5), 0.0, 0.09 * (2 - s2),
             0.4, dais_w - 1.0, 0.18 * (2 - s2))

    # --- high open timber roof, sized from the ACTUAL wall bounding boxes ---
    wN = [o for o in col.objects if o.name.startswith(f"{name}_win_N_")]
    wS = [o for o in col.objects if o.name.startswith(f"{name}_win_S_")]
    Nlo, Nhi = _world_bbox(wN)
    Slo, Shi = _world_bbox(wS)
    Alo, Ahi = _world_bbox(wN + wS)
    top = float(max(Nhi[2], Shi[2]))               # true wall top
    nO, sO = float(Nhi[1]), float(Slo[1])          # wall outer faces (+/-Y)
    ncy = 0.5 * (float(Nhi[1]) + float(Nlo[1]))    # wall centrelines
    scy = 0.5 * (float(Slo[1]) + float(Shi[1]))
    midy = 0.5 * (ncy + scy)
    x0, x1 = 0.0, length              # full length (a bay's N window is skipped)
    apex = top + roof_rise
    tie_z, rt = 0.34, 0.26

    # The roof SPRINGS FROM THE WALL TOP (eave = wall top), so the walls and roof
    # meet with no exposed gap: rafters foot on the wall head, the roof skin's
    # eave sits at the wall top, and the gable triangles close onto the end walls.
    for tt in range(nbay + 1):
        x = x0 + tt * bay
        _box(col, f"{name}_tie_{tt}", x, 0.5 * (nO + sO), top + tie_z / 2.0,
             0.30, (nO - sO), tie_z)             # tie bears on both wall heads
        fN = (x, ncy, top)                       # rafters foot ON the wall top
        fS = (x, scy, top)
        ap = (x, midy, apex)
        _strut(col, f"{name}_praf_{tt}_L", fN, ap, rt)   # wall head -> ridge
        _strut(col, f"{name}_praf_{tt}_R", fS, ap, rt)
        _strut(col, f"{name}_king_{tt}", (x, midy, top + tie_z), ap, 0.20)
        fr = 0.55
        zc = top + (apex - top) * fr
        yN = ncy + (midy - ncy) * fr
        yS = scy + (midy - scy) * fr
        _box(col, f"{name}_collar_{tt}", x, 0.5 * (yN + yS), zc,
             0.22, (yN - yS), 0.20)
    # longitudinal purlins seated on the rafters, tying the trusses
    for yc, zc, nm in [
            (ncy + (midy - ncy) * 0.32, top + (apex - top) * 0.32, "N"),
            (scy + (midy - scy) * 0.32, top + (apex - top) * 0.32, "S"),
            (midy, apex, "ridge")]:
        _box(col, f"{name}_purlin_{nm}", 0.5 * (x0 + x1), yc, zc,
             (x1 - x0), 0.14, 0.14)

    # roof skin: eave AT the wall top (over the wall outer face), ridge at apex.
    bm_verts = [
        (x0, nO, top), (x0, sO, top), (x0, midy, apex),
        (x1, nO, top), (x1, sO, top), (x1, midy, apex)]
    import bmesh
    bm = bmesh.new()
    v = [bm.verts.new(p) for p in bm_verts]
    bm.faces.new((v[0], v[3], v[5], v[2]))         # +Y slope
    bm.faces.new((v[1], v[2], v[5], v[4]))         # -Y slope
    bm.faces.new((v[0], v[2], v[1]))               # near gable
    bm.faces.new((v[3], v[4], v[5]))               # far gable
    me = bpy.data.meshes.new(f"{name}_roof")
    bm.to_mesh(me)
    bm.free()
    roof = bpy.data.objects.new(f"{name}_roof", me)
    col.objects.link(roof)
    sol = roof.modifiers.new("solid", 'SOLIDIFY')
    sol.thickness = 0.12
    sol.offset = 1.0

    # --- hooded wall fireplace against the north wall, in the last (dais) bay ---
    yin = float(Nlo[1])
    _fireplace(col, name, wx=fp_wx, wall_inner_y=yin,
               chimney_top_z=apex + 1.6, sign=+1)
    # punch the flue hole so the chimney exits through the roof (boolean cutter
    # over the stack footprint: wx +/- 0.48 in X, yin-0.3..yin in Y).
    _punch_flue(col, roof, f"{name}_flue", fp_wx, yin - 0.15, 0.66, 0.20)
    return col


def _punch_flue(col, roof, name, wx, yc, sx, sy):
    """Cut a rectangular flue hole through @p roof for the chimney: apply the
    roof's solidify, then boolean-difference a tall cutter over the stack
    footprint (centre X=@p wx, Y=@p yc; size @p sx x @p sy)."""
    cutter = _box(col, name + "_cut", wx, yc, 6.0, sx, sy, 12.0)
    bpy.context.view_layer.objects.active = roof
    for o in bpy.context.selected_objects:
        o.select_set(False)
    roof.select_set(True)
    for m in list(roof.modifiers):
        if m.type == 'SOLIDIFY':
            bpy.ops.object.modifier_apply(modifier=m.name)
    bm = roof.modifiers.new("flue", 'BOOLEAN')
    bm.operation = 'DIFFERENCE'
    bm.object = cutter
    bm.solver = 'EXACT'
    bpy.ops.object.modifier_apply(modifier="flue")
    bpy.data.objects.remove(cutter, do_unlink=True)
    roof.select_set(False)


# ---------------------------------------------------------------------------
# UV finalize + material assignment (prepare the hall for texturing)
# ---------------------------------------------------------------------------
def world_cube_uv(obj, scale=1.0):
    """Replace @p obj's material UV (``UVMap``) with a WORLD-SPACE cube
    projection: each face is projected onto the world plane perpendicular to its
    dominant world-normal axis, so one UV unit == one metre. The baked masonry
    tile and the aperiodic material fields then sit at real-world size on EVERY
    surface -- boxes, struts and swept arch geometry alike -- instead of the
    per-face 0..1 unwrap that ``primitive_cube_add`` leaves on the boxes (which
    would stretch one tile across a whole face regardless of its size)."""
    import bmesh
    me = obj.data
    if not me.polygons:
        return
    uvl = me.uv_layers.get("UVMap") or me.uv_layers.new(name="UVMap")
    me.uv_layers.active = uvl
    mw = obj.matrix_world
    mw3 = mw.to_3x3()
    bm = bmesh.new()
    bm.from_mesh(me)
    uv = bm.loops.layers.uv.get("UVMap")
    for f in bm.faces:
        n = mw3 @ f.normal
        ax = max(range(3), key=lambda i: abs(n[i]))   # dominant world axis
        for lp in f.loops:
            co = mw @ lp.vert.co
            if ax == 0:
                u, v = co.y, co.z
            elif ax == 1:
                u, v = co.x, co.z
            else:
                u, v = co.x, co.y
            lp[uv].uv = (u * scale, v * scale)
    bm.to_mesh(me)
    bm.free()


# object-name stems whose UVs are AUTHORED by the arch / pier generators:
# course-continuous reveal UVs (that WRAP the splayed reveal + arch soffit) and
# voussoir-strip UVs. A world-cube projection would smear the coursing off the
# curved reveal and flatten the voussoir band, so prepare_uvs leaves these alone.
_ARCH_AUTHORED = ("win_N", "win_S", "entrance", "dais_arch", "pier_N", "pier_S",
                  "fp_opening", "fp_roll", "fp_chevron", "fp_label")


def prepare_uvs(col, scale=1.0):
    """Give the PLAIN box/strut/bmesh geometry a world-scale cube UV (1 UV unit
    == 1 m) so tiling materials sit at real-world size. Objects generated by the
    arch / pier scripts (see ``_ARCH_AUTHORED``) keep their own purpose-built
    course-continuous / voussoir UVs -- those wrap the reveal and arch correctly,
    so we must NOT overwrite them with a flat projection."""
    pre = col.name + "_"
    for o in col.objects:
        if o.type != "MESH":
            continue
        stem = o.name[len(pre):] if o.name.startswith(pre) else o.name
        if any(stem.startswith(s) for s in _ARCH_AUTHORED):
            continue
        world_cube_uv(o, scale)


# object-name stems (after the ``<hall>_`` prefix) that read as masonry WALL:
# the arched window bays, the solid fireplace bay, the engaged piers and the two
# end-wall arches. Everything else (floor, dais, timber roof, fireplace stone) is
# left UV-ready for its own material.
_WALL_STEMS = ("win_N", "win_S", "wall_N", "pier_N", "pier_S",
               "entrance", "dais_arch")


def assign_wall_material(col, bake_dir, name="great_hall_stone_wall"):
    """Build the baked hewn-brick MASONRY material (limestone brick + mortar,
    selected by the baked mask, with the baked normal/AO/height and tint) from
    the chimera bake maps in @p bake_dir and assign it to the hall's wall
    objects (see ``_WALL_STEMS``). Returns the material."""
    import material_nodes
    mat = bpy.data.materials.get(name)
    if mat:
        bpy.data.materials.remove(mat)
    mat = material_nodes.build_masonry_material(
        name,
        mask=os.path.join(bake_dir, "mask.png"),
        normal=os.path.join(bake_dir, "normal.png"),
        ao=os.path.join(bake_dir, "ao.png"),
        height=os.path.join(bake_dir, "height.png"),
        tint_map=os.path.join(bake_dir, "tint.png"))
    pre = col.name + "_"
    for o in col.objects:
        if o.type != "MESH":
            continue
        stem = o.name[len(pre):] if o.name.startswith(pre) else o.name
        if any(stem.startswith(s) for s in _WALL_STEMS):
            me = o.data
            me.materials.clear()
            me.materials.append(mat)
    return mat
