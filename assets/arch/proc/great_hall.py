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
        trim_width=0.24, masonry_uv=True, collection=col)  # coursed reveal/inset UVs
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
            plinth_height=0.5, plinth_project=0.11, masonry_uv=True, collection=col)
        pn.location = (i * bay, half - wall_t * 0.5, 0.0)
        pn.rotation_euler = (0, 0, math.pi)         # project toward -Y (into room)
        ps = pier.build_wall_pier(
            name=f"{name}_pier_S_{i}", width=0.7, depth=0.55, height=pier_h,
            plinth_height=0.5, plinth_project=0.11, masonry_uv=True, collection=col)
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
    # longitudinal purlins seated on the rafters, tying the trusses. The RIDGE
    # purlin hangs BELOW the apex (its top just under the peak) as an exposed ridge
    # beam running door-to-dais along the roof seam, VISIBLE from inside -- at the
    # apex it would sit inside the roof skin and read as nothing.
    for yc, zc, nm, sz in [
            (ncy + (midy - ncy) * 0.32, top + (apex - top) * 0.32, "N", 0.14),
            (scy + (midy - scy) * 0.32, top + (apex - top) * 0.32, "S", 0.14),
            (midy, apex - 0.18, "ridge", 0.30)]:
        _box(col, f"{name}_purlin_{nm}", 0.5 * (x0 + x1), yc, zc,
             (x1 - x0), 0.16, sz)

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
               "entrance", "dais_arch", "fp_")   # fireplace stone = wall masonry


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


# object-name stems that read as TIMBER beams (roof trusses).
_TIMBER_STEMS = ("collar", "king", "praf", "purlin", "tie")
# beams whose geometry spans a cube-ish bbox (compound / steeply diagonal), where
# the perimeter unwrap's frame is unreliable -- their UVs are fit to the image
# bounds instead of scaled by the (mis-measured) cross-perimeter.
_COMPOUND_BEAMS = ("king", "praf")

#: directory holding the stitched 3-beam wood maps (beam_albedo/rough/normal.png).
try:
    _TIMBER_DIR = os.path.normpath(os.path.join(
        os.path.dirname(__file__), "..", "..", "..", "assetsrc", "materials", "timber"))
except NameError:
    _TIMBER_DIR = "/home/kmd/rpg/assetsrc/materials/timber"


def _beam_uv(obj, aspect):
    """Perimeter-wrap UV for a single box beam: U runs ALONG the beam (longest
    edge is the axis), V wraps around the cross-section perimeter as one continuous
    band with a single seam along the length. Sized in texture space so grain is
    proportional (U scaled by the texture ``aspect``); a per-beam length offset
    keeps beams from looking identical. Works in the beam's own frame, so any
    orientation is handled without stretching."""
    import numpy as np
    import bmesh
    me = obj.data
    co = np.array([v.co for v in me.vertices])
    ev = np.array([(e.vertices[0], e.vertices[1]) for e in me.edges])
    d = co[ev[:, 1]] - co[ev[:, 0]]
    el = np.linalg.norm(d, axis=1)
    axis = d[int(np.argmax(el))].astype(float)
    axis /= np.linalg.norm(axis)
    bm = bmesh.new()
    bm.from_mesh(me)
    bm.normal_update()
    best = None
    for f in bm.faces:                       # e1 = normal of the widest long face
        n = np.array(f.normal)
        if abs(n @ axis) < 0.5:
            ar = f.calc_area()
            if best is None or ar > best[0]:
                best = (ar, n)
    e1 = best[1].astype(float)
    e1 /= np.linalg.norm(e1)
    e2 = np.cross(axis, e1)
    c = co.mean(0)
    X = co - c
    a = X @ e1
    b = X @ e2
    amin, amax, bmin, bmax = a.min(), a.max(), b.min(), b.max()
    wa = max(amax - amin, 1e-4)
    wb = max(bmax - bmin, 1e-4)
    perim = 2.0 * (wa + wb)
    off = float(np.random.default_rng(abs(hash(obj.name)) % (2 ** 32)).uniform(0, 1))
    uvl = bm.loops.layers.uv.verify()
    for f in bm.faces:
        n = np.array(f.normal)
        ne1, ne2, nax = n @ e1, n @ e2, n @ axis
        side = 'cap'
        if abs(nax) < 0.7:
            side = ('+e1' if ne1 > 0 else '-e1') if abs(ne1) >= abs(ne2) \
                else ('+e2' if ne2 > 0 else '-e2')
        for lp in f.loops:
            p = np.array(lp.vert.co) - c
            pa, pb, pl = p @ e1, p @ e2, p @ axis
            if side == '+e1':
                pos = pb - bmin
            elif side == '+e2':
                pos = wb + (amax - pa)
            elif side == '-e1':
                pos = wb + wa + (bmax - pb)
            elif side == '-e2':
                pos = 2 * wb + wa + (pa - amin)
            else:
                pos = pb - bmin
            lp[uvl].uv = (pl / (perim * aspect) + off, pos / perim)
    bm.to_mesh(me)
    bm.free()


def _fit_uv_bounds(obj, rotate90=False):
    """Rescale the active UVs so their bounding box exactly fills the image [0,1].
    Used for the compound beams, whose perimeter frame is unreliable -- fitting to
    the image gives a clean, correctly-scaled result. ``rotate90`` swaps U/V first:
    on those beams the longest edge picks a CROSS direction, so the grain comes out
    90 deg rotated -- swapping puts it back along the beam length."""
    import numpy as np
    uvl = obj.data.uv_layers.active.data
    if rotate90:
        for lp in uvl:
            lp.uv = (lp.uv[1], lp.uv[0])
    uv = np.array([lp.uv for lp in uvl])
    lo = uv.min(0)
    span = np.maximum(uv.max(0) - lo, 1e-9)
    # per-beam U offset so fitted beams don't all show the identical crop (the
    # wood map is X-tileable, so a U shift just wraps to a different stretch).
    off = float(np.random.default_rng(abs(hash(obj.name)) % (2 ** 32)).uniform(0, 1))
    for lp in uvl:
        lp.uv = ((lp.uv[0] - lo[0]) / span[0] + off, (lp.uv[1] - lo[1]) / span[1])


def assign_floor_material(col, bake_dir, name="great_hall_floor_stone"):
    """Build + assign the big dressed-flagstone FLOOR material (the square stone
    tile bake): larger tiles, a cooler/less-saturated grey stone and SUPER-DARK
    joints between the flags. Assigned to the ``floor`` slab."""
    import material_nodes
    old = bpy.data.materials.get(name)
    if old:
        bpy.data.materials.remove(old)
    mat = material_nodes.build_masonry_material(
        name, mask=os.path.join(bake_dir, "mask.png"),
        normal=os.path.join(bake_dir, "normal.png"),
        ao=os.path.join(bake_dir, "ao.png"),
        height=os.path.join(bake_dir, "height.png"),
        tint_map=os.path.join(bake_dir, "tint.png"), tile=(4.2, 4.2),
        brick_contrast=0.62, mortar_contrast=0.25,
        mortar_tint=(0.014, 0.013, 0.012),           # super-dark joints
        brick_tint=(0.22, 0.24, 0.28), brick_sat=0.55,  # darker cool grey stone
        rough_brick=0.52, rough_mortar=0.78,         # less rough (a touch polished)
        normal_strength=1.6,                          # slightly more bumpy relief
        ao_strength=0.8)
    fl = bpy.data.objects.get(col.name + "_floor")
    if fl:
        fl.data.materials.clear()
        fl.data.materials.append(mat)
    return mat


def assign_reveal_weave(col, bake_dir, name="great_hall_reveal_weave"):
    """Build the toothed window-splay WEAVE material and assign it (as a second
    slot) to the window REVEAL faces -- the splayed jambs + arch soffit -- so the
    coursing there reads as the interlocking reveal weave rather than plain wall.
    Reveal faces are the inward-facing ones (|n.y|<0.65) within the opening's
    X/Z extent; the flat wall faces keep slot 0 (the wall material)."""
    import material_nodes
    old = bpy.data.materials.get(name)
    if old:
        bpy.data.materials.remove(old)
    mat = material_nodes.build_masonry_material(
        name, mask=os.path.join(bake_dir, "mask.png"),
        normal=os.path.join(bake_dir, "normal.png"),
        ao=os.path.join(bake_dir, "ao.png"),
        height=os.path.join(bake_dir, "height.png"),
        tint_map=os.path.join(bake_dir, "tint.png"), tile=(1.2, 1.0))
    pre = col.name + "_"
    for o in col.objects:
        if o.type != "MESH":
            continue
        stem = o.name[len(pre):] if o.name.startswith(pre) else o.name
        if not (stem.startswith("win_N") or stem.startswith("win_S")):
            continue
        me = o.data
        if mat.name not in [m.name for m in me.materials]:
            me.materials.append(mat)
        wslot = [m.name for m in me.materials].index(mat.name)
        for p in me.polygons:
            n, c = p.normal, p.center
            if abs(n.y) < 0.65 and abs(c.x) < 0.9 and 1.85 < c.z < 5.25:
                p.material_index = wslot
        me.update()
    return mat


def assign_timber_material(col, timber_dir=None, name="great_hall_timber"):
    """Build + assign the rustic TIMBER material to the roof-truss beams. The base
    colour + roughness come from the stitched THREE-BEAM wood maps
    (``beam_albedo/rough.png`` in ``timber_dir``, from
    ``texsynth.wood_synth.synth_wood_beams``) plus the normal map, sampled through a
    per-beam perimeter-wrap UV (:func:`_beam_uv`) so the grain runs along each beam
    with one length seam. Beam scale is APPLIED first so unit-cube-plus-scale beams
    unwrap correctly in local space. The maps tile (REPEAT) so long beams repeat."""
    timber_dir = timber_dir or _TIMBER_DIR
    old = bpy.data.materials.get(name)
    if old:
        bpy.data.materials.remove(old)

    def _img(fname, non_color):
        im = bpy.data.images.get(fname)
        if im:
            bpy.data.images.remove(im)
        im = bpy.data.images.load(os.path.join(timber_dir, fname))
        im.colorspace_settings.name = 'Non-Color' if non_color else 'sRGB'
        return im

    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    nt = mat.node_tree
    nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    out.location = (600, 0)
    bsdf = nt.nodes.new("ShaderNodeBsdfPrincipled")
    bsdf.location = (300, 0)
    nt.links.new(bsdf.outputs["BSDF"], out.inputs["Surface"])
    tc = nt.nodes.new("ShaderNodeTexCoord")
    tc.location = (-600, 0)
    alb = nt.nodes.new("ShaderNodeTexImage")
    alb.location = (-350, 150)
    alb.image = _img("beam_albedo.png", False)
    alb.extension = 'REPEAT'
    rgh = nt.nodes.new("ShaderNodeTexImage")
    rgh.location = (-350, -180)
    rgh.image = _img("beam_rough.png", True)
    rgh.extension = 'REPEAT'
    nrm = nt.nodes.new("ShaderNodeTexImage")
    nrm.location = (-350, -480)
    nrm.image = _img("beam_normal.png", True)
    nrm.extension = 'REPEAT'
    nmap = nt.nodes.new("ShaderNodeNormalMap")
    nmap.location = (-80, -480)
    nt.links.new(tc.outputs["UV"], alb.inputs["Vector"])
    nt.links.new(tc.outputs["UV"], rgh.inputs["Vector"])
    nt.links.new(tc.outputs["UV"], nrm.inputs["Vector"])
    nt.links.new(alb.outputs["Color"], bsdf.inputs["Base Color"])
    nt.links.new(rgh.outputs["Color"], bsdf.inputs["Roughness"])
    nt.links.new(nrm.outputs["Color"], nmap.inputs["Color"])
    nt.links.new(nmap.outputs["Normal"], bsdf.inputs["Normal"])
    bsdf.inputs["Specular IOR Level"].default_value = 0.3

    aspect = alb.image.size[0] / max(alb.image.size[1], 1)
    pre = col.name + "_"
    beams = [o for o in col.objects if o.type == "MESH"
             and any((o.name[len(pre):] if o.name.startswith(pre) else o.name)
                     .startswith(s) for s in _TIMBER_STEMS)]
    # Several beams are unit cubes shaped only by a non-uniform object SCALE, so
    # APPLY the scale first (single-user the mesh) -> the real beam proportions live
    # in the LOCAL mesh and the plain local unwrap is correct for every beam.
    for o in beams:
        if o.data.users > 1:
            o.data = o.data.copy()
    for x in list(bpy.context.selected_objects):
        x.select_set(False)
    for o in beams:
        o.select_set(True)
    if beams:
        bpy.context.view_layer.objects.active = beams[0]
        bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    for o in beams:
        _beam_uv(o, aspect)
        o.data.materials.clear()
        o.data.materials.append(mat)
    return mat


def assign_roof_material(col, uv_scale=0.2, name="great_hall_roof_limestone"):
    """The flat roof planes read as LIMESTONE (not timber): an aperiodic limestone
    field material on a world-cube UV. ``uv_scale`` multiplies the world-cube UVs
    -- the field feature size is INVERSELY proportional to it, so the default 0.2
    (< the 1 m default) scales the UVs DOWN and makes the limestone blocks LARGER
    (at 1.0 the detail reads too small on the big roof). Applied to ``roof``."""
    import material_nodes
    old = bpy.data.materials.get(name)
    if old:
        bpy.data.materials.remove(old)
    try:
        mat = material_nodes.build_field_material(name, "limestone")
    except Exception:                              # no limestone fields -> flat stone
        mat = bpy.data.materials.new(name)
        mat.use_nodes = True
        b = mat.node_tree.nodes.get("Principled BSDF")
        b.inputs["Base Color"].default_value = (0.62, 0.60, 0.55, 1.0)
        b.inputs["Roughness"].default_value = 0.85
    pre = col.name + "_"
    for o in col.objects:
        if o.type != "MESH":
            continue
        stem = o.name[len(pre):] if o.name.startswith(pre) else o.name
        if stem.startswith("roof"):
            world_cube_uv(o, uv_scale)
            o.data.materials.clear()
            o.data.materials.append(mat)
    return mat


def _dais_bevel(obj, width=0.006, segments=2, angle_deg=40.0):
    """Give a dais block a THIN double-bevel (2 segments) on every edge plus
    FACE-WEIGHTED normals, so the arrises catch a soft highlight instead of a
    razor-sharp CGI knife edge. Idempotent (refreshes its own modifiers)."""
    import math
    for m in [m for m in obj.modifiers if m.name in ("dais_bevel", "dais_wn")]:
        obj.modifiers.remove(m)
    for x in list(bpy.context.selected_objects):
        x.select_set(False)
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    try:                                 # smooth + auto-smooth so weighted normals read
        bpy.ops.object.shade_auto_smooth(angle=math.radians(angle_deg))
    except (AttributeError, RuntimeError):
        bpy.ops.object.shade_smooth()
    bev = obj.modifiers.new("dais_bevel", 'BEVEL')
    bev.width = width
    bev.segments = segments
    bev.limit_method = 'ANGLE'
    bev.angle_limit = math.radians(angle_deg)
    bev.harden_normals = True
    wn = obj.modifiers.new("dais_wn", 'WEIGHTED_NORMAL')
    wn.mode = 'FACE_AREA_WITH_ANGLE'
    wn.keep_sharp = True
    wn.weight = 50


def assign_dais_material(col, uv_scale=0.5, name="great_hall_dais_marble"):
    """Dark polished MARBLE for the raised dais + its steps: an aperiodic marble
    field, tinted dark and made low-roughness (polished), on a world-cube UV. The
    dais ARCH keeps the wall masonry (handled by ``assign_wall_material``)."""
    import material_nodes
    old = bpy.data.materials.get(name)
    if old:
        bpy.data.materials.remove(old)
    try:
        mat = material_nodes.build_field_material(
            name, "marble", rough_base=0.30, tint=(0.11, 0.11, 0.14))
    except Exception:                              # no marble fields -> flat polish
        mat = bpy.data.materials.new(name)
        mat.use_nodes = True
        b = mat.node_tree.nodes.get("Principled BSDF")
        b.inputs["Base Color"].default_value = (0.05, 0.05, 0.07, 1.0)
        b.inputs["Roughness"].default_value = 0.18
    pre = col.name + "_"
    for o in col.objects:
        if o.type != "MESH":
            continue
        stem = o.name[len(pre):] if o.name.startswith(pre) else o.name
        if stem == "dais" or stem.startswith("dais_step"):
            world_cube_uv(o, uv_scale)
            o.data.materials.clear()
            o.data.materials.append(mat)
            _dais_bevel(o)
    return mat


def build_hall_scene(bake_root, collection=None, name="great_hall"):
    """Regenerate the whole great-hall scene: geometry (``build_great_hall``),
    world-scale UVs (``prepare_uvs``), then every material -- wall, floor, reveal
    weave and timber -- from the bake maps under ``bake_root`` (which holds
    ``bake`` / ``bake_floor`` / ``bake_weave``). One call reproduces the scene, so
    it can be passed as the regeneration callback to the exporter/baker. Returns
    the collection."""
    col = build_great_hall(name=name, collection=collection)
    prepare_uvs(col)
    assign_wall_material(col, os.path.join(bake_root, "bake"))
    assign_floor_material(col, os.path.join(bake_root, "bake_floor"))
    assign_reveal_weave(col, os.path.join(bake_root, "bake_weave"))
    assign_timber_material(col)
    assign_roof_material(col)
    assign_dais_material(col)
    return col
