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


def _wall_base_plinths(col, name, length, half, wall_t, dais_top, dais_w,
                       steps=((0.0, 0.10, 0.10), (0.10, 0.06, 0.06))):
    """A low, shallow STEPPED base plinth along the foot of each wall's inner
    face -- a wider bottom course with a set-back top course (skirting, not a
    bench). @p steps = list of (z0, height, projection). Side + entrance walls sit
    on the floor (z=0); the dais backdrop wall's plinth sits ON the dais platform
    (z=dais_top) since the platform covers its floor-level foot. Stem 'plinth' ->
    wall masonry."""
    hw = half - wall_t / 2.0                   # side-wall inner face |Y|
    ex = length - wall_t / 2.0                 # dais backdrop inner face X
    # (tag, run start, run end, (axis, face pos, room dir), z base)
    walls = [
        ("N", 0.0, length, ("y", hw, -1.0), 0.0),
        ("S", 0.0, length, ("y", -hw, 1.0), 0.0),
        ("W", -half, half, ("x", wall_t / 2.0, 1.0), 0.0),
        ("E", -dais_w / 2.0, dais_w / 2.0, ("x", ex, -1.0), dais_top),
    ]
    for tag, a0, a1, (axis, pos, rd), zb in walls:
        span = a1 - a0
        ctr = 0.5 * (a0 + a1)
        for si, (z0, h, proj) in enumerate(steps):
            cz = zb + z0 + h / 2.0
            if axis == "y":                    # wall runs along X, projects in Y
                cx, cy, sx, sy, sz = ctr, pos + rd * proj / 2.0, span, proj, h
            else:                              # wall runs along Y, projects in X
                cx, cy, sx, sy, sz = pos + rd * proj / 2.0, ctr, proj, span, h
            pl = _box(col, f"{name}_plinth_{tag}_{si}", cx, cy, cz, sx, sy, sz)
            # Same worked-arris treatment as the dais and steps: these skirting
            # courses meet the floor right where the eye sits (see _stone_bevel).
            _dais_bevel(pl)


def _arched_niche_cutter(col, name, x_front, depth, yc, half_w, straight_h, z0):
    """A solid arched prism used to BOOLEAN-cut a lamp niche into an end wall.

    The wall faces along X; the niche opening lives in the Y-Z plane. The profile
    is a rectangle (width ``2*half_w``, height ``straight_h`` from ``z0``) capped
    by a semicircle of radius ``half_w``, extruded along +X from ``x_front`` by
    ``depth`` into the wall. ``x_front`` is set just OUTSIDE the wall's room face
    so the cut opens cleanly onto the interior surface. Returns the cutter obj."""
    import bmesh
    seg = 10                                   # semicircle segments (crown)
    # 2-D arched outline (world Y,Z) traced CCW: up the left jamb, over the crown,
    # down the right jamb, across the sill.
    prof = [(yc - half_w, z0), (yc - half_w, z0 + straight_h)]
    cz = z0 + straight_h                        # spring line of the semicircle
    for s in range(1, seg):
        ang = math.pi * (1.0 - s / float(seg))  # pi -> 0 (left -> right over top)
        prof.append((yc + half_w * math.cos(ang), cz + half_w * math.sin(ang)))
    prof.append((yc + half_w, z0 + straight_h))
    prof.append((yc + half_w, z0))
    bm = bmesh.new()
    front = [bm.verts.new((x_front, y, z)) for (y, z) in prof]
    bm.faces.new(front)                         # front cap
    ret = bmesh.ops.extrude_face_region(bm, geom=bm.faces[:])
    verts = [e for e in ret["geom"] if isinstance(e, bmesh.types.BMVert)]
    bmesh.ops.translate(bm, vec=(depth, 0.0, 0.0), verts=verts)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    o = bpy.data.objects.new(name, me)
    _link(col, o)
    return o


def _cube_uv_faces_in_aabb(obj, aabb_min, aabb_max, scale=1.0):
    """World-scale cube-project ONLY the faces whose centroid lies within the
    world AABB [aabb_min, aabb_max] (the freshly carved niche pocket). Leaves the
    wall's authored arch UVs untouched. Mirrors ``world_cube_uv``'s per-face
    world-plane projection (1 UV unit == 1 m)."""
    import bmesh
    me = obj.data
    uvl = me.uv_layers.get("UVMap") or me.uv_layers.new(name="UVMap")
    me.uv_layers.active = uvl
    mw = obj.matrix_world
    mw3 = mw.to_3x3()
    bm = bmesh.new()
    bm.from_mesh(me)
    uv = bm.loops.layers.uv.get("UVMap")
    for f in bm.faces:
        c = mw @ f.calc_center_median()
        if not (aabb_min[0] <= c.x <= aabb_max[0] and
                aabb_min[1] <= c.y <= aabb_max[1] and
                aabb_min[2] <= c.z <= aabb_max[2]):
            continue
        n = mw3 @ f.normal
        ax = max(range(3), key=lambda i: abs(n[i]))
        for lp in f.loops:
            w = mw @ lp.vert.co
            if ax == 0:
                u, v = w.y, w.z
            elif ax == 1:
                u, v = w.x, w.z
            else:
                u, v = w.x, w.y
            lp[uv].uv = (u * scale, v * scale)
    bm.to_mesh(me)
    bm.free()


def _niche_voussoirs(col, name, x_room_face, yc, z_spring, r_in, r_out,
                     protrude, segs=24, u_scale=1.0, v_scale=1.0):
    """One swept VOUSSOIR BAND (a raised archivolt) framing the SEMICIRCULAR head
    of a lamp niche -- angles 0..pi over the top only, NOT down the vertical
    jambs. Built as a single object: a rectangular cross-section (on the wall room
    face at @p x_room_face, protruding @p protrude into the room, spanning radius
    @p r_in..@p r_out) swept round the arc in @p segs steps.

    The wall/voussoir texture is TILED AROUND THE CURVE like the window arches
    (arch._voussoir_strip_uvs convention): U = ARC LENGTH along the opening,
    V = the unrolled cross-section, both in metres, so the brick head-joints wrap
    the arch as uniform radial voussoir joints (no per-brick apex fan). @p u_scale
    / @p v_scale tune how many stones / courses show. Registered stem 'lamp_vous'
    (wall masonry material, arch-authored UVs). Returns the object."""
    import bmesh
    bm = bmesh.new()
    uvl = bm.loops.layers.uv.new("UVMap")
    x_back = x_room_face                          # flush with the wall room face
    x_front = x_room_face - protrude              # protrudes into the room (-X)
    r_mid = 0.5 * (r_in + r_out)
    band = r_out - r_in
    # Cross-section, unrolled along V (metres): outer edge (0..protrude), then the
    # front face (protrude..protrude+band), so U=arc,V=section wraps continuously.
    def yz(a, r):
        return (yc + r * math.cos(a), z_spring + r * math.sin(a))
    rings = []                                    # per-angle vert rings (4 section pts)
    for j in range(segs + 1):
        a = math.pi * j / segs
        yo, zo = yz(a, r_out)
        yi, zi = yz(a, r_in)
        # section verts: outer-back, outer-front, inner-front, inner-back
        vb_o = bm.verts.new((x_back, yo, zo))
        vf_o = bm.verts.new((x_front, yo, zo))
        vf_i = bm.verts.new((x_front, yi, zi))
        vb_i = bm.verts.new((x_back, yi, zi))
        rings.append((vb_o, vf_o, vf_i, vb_i))
    # arc length (U) per angular station, and the unrolled section V coordinates.
    def u_of(j):
        return (math.pi * j / segs) * r_mid * u_scale
    v_sec = [0.0, protrude, protrude + band, 2.0 * protrude + band]  # around the section
    v_sec = [v * v_scale for v in v_sec]
    for j in range(segs):
        a_ring, b_ring = rings[j], rings[j + 1]
        ua, ub = u_of(j), u_of(j + 1)
        for k in range(3):                        # outer / front / inner faces
            f = bm.faces.new((a_ring[k], a_ring[k + 1], b_ring[k + 1], b_ring[k]))
            uv = [(ua, v_sec[k]), (ua, v_sec[k + 1]),
                  (ub, v_sec[k + 1]), (ub, v_sec[k])]
            for lp, c in zip(f.loops, uv):
                lp[uvl].uv = c
        # close the back onto the wall (hidden) + this segment gets no back face
        fb = bm.faces.new((a_ring[3], a_ring[0], b_ring[0], b_ring[3]))
        for lp in fb.loops:
            lp[uvl].uv = (ua, 0.0)
    # end caps at the two springs (angle 0 and pi)
    for r, sgn in ((rings[0], 1), (rings[-1], -1)):
        f = bm.faces.new(r if sgn > 0 else list(reversed(r)))
        for lp in f.loops:
            lp[uvl].uv = (0.0, 0.0)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    o = bpy.data.objects.new(name, me)
    _link(col, o)
    return o


def _carve_lamp_niches(col, wall, x_room_face, centre_z, y_offsets,
                       half_w, straight_h, recess):
    """Boolean-cut arched lamp niches into @p wall (the dais backdrop). Each niche
    opens on the room face (@p x_room_face, interior at smaller X) and recesses
    @p recess into the wall. @p centre_z is the opening's vertical centre; the
    profile is a @p straight_h rectangle (half-width @p half_w) under a semicircle.
    Re-UVs only the carved pocket faces so they read as masonry."""
    # z0 (sill) chosen so the opening centre (mid of straight + crown) lands on
    # centre_z: opening spans z0 .. z0 + straight_h + half_w, centre = z0 + (that)/2.
    z0 = centre_z - (straight_h + half_w) / 2.0
    # The interior face is at x_room_face; the wall body extends toward +X. Start
    # the cutter a hair PROUD of that face (-X) and extrude +X into the wall by
    # `recess`, so the pocket opens on the interior surface and stops short of the
    # far face (recess < wall_t -> solid backing, no leak).
    x_front = x_room_face - 0.02
    cutters = []
    for i, yc in enumerate(y_offsets):
        cut = _arched_niche_cutter(col, f"{wall.name}_niche_cut_{i}", x_front,
                                   recess + 0.02, yc, half_w, straight_h, z0)
        cutters.append((cut, yc))
    for cut, _yc in cutters:
        mod = wall.modifiers.new(f"{cut.name}_bool", 'BOOLEAN')
        mod.operation = 'DIFFERENCE'
        mod.object = cut
        mod.solver = 'EXACT'
        bpy.context.view_layer.objects.active = wall
        bpy.ops.object.modifier_apply(modifier=mod.name)
    # Re-UV each carved pocket's INTERIOR only (the jambs + soffit + blind back,
    # which the boolean created fresh). The pocket runs from the room face INTO the
    # wall by `recess` (+X); start the AABB just PAST the room face so the front
    # wall faces around the opening keep the boolean-interpolated authored wall UV
    # -- re-projecting them to world-cube left a long seam where they met the rest
    # of the (authored-UV) backdrop wall around the arch.
    for _cut, yc in cutters:
        amin = (x_room_face + 0.02, yc - half_w - 0.06, z0 - 0.06)
        amax = (x_room_face + recess + 0.06, yc + half_w + 0.06,
                z0 + straight_h + half_w + 0.06)
        _cube_uv_faces_in_aabb(wall, amin, amax)
    for cut, _yc in cutters:
        bpy.data.objects.remove(cut, do_unlink=True)
    # Frame each niche's arch head with protruding voussoirs (over the semicircle
    # only, not the jambs). z_spring = jamb top = z0 + straight_h. Name off the
    # hall stem (wall is "<name>_dais_arch") so the stem-matched material assign
    # picks them up regardless of the collection's (possibly suffixed) name.
    hall = wall.name[:-len("_dais_arch")] if wall.name.endswith("_dais_arch") else col.name
    for i, yc in enumerate(y_offsets):
        # r_in a touch INSIDE the opening radius so the archivolt lip overhangs
        # the reveal, covering the opening-to-wall arris (no exposed edge).
        _niche_voussoirs(col, f"{hall}_lamp_vous_{i}", x_room_face, yc,
                         z0 + straight_h, r_in=half_w - 0.02, r_out=half_w + 0.14,
                         protrude=0.05, segs=24, u_scale=1.0, v_scale=1.0)


def _dais_doorway(col, wall, name, length, wall_t, blind_recess,
                  half_w=0.60, z0=0.70, straight_h=1.30,
                  dais_top=0.46, step_rise=0.12, step_tread=0.30,
                  step_half_w=0.80):
    """Cut a REAL arched doorway THROUGH the back of the dais blind arch.

    The blind arch is a recess: its front (room) face is at ``length - wall_t/2``
    and its blind cap sits ``blind_recess`` deeper. This punches an arched opening
    through the masonry REMAINING behind that cap (``wall_t - blind_recess``), so
    the niche stops being blind and reads as a doorway set within the arch -- the
    lord's door off the dais. Same boolean machinery as the lamp niches, but the
    cutter runs past the far face so the hole goes all the way through.

    @p half_w / @p straight_h / @p z0 shape the opening: a ``2*half_w`` wide,
    ``straight_h`` tall rectangle from @p z0, capped by a semicircle of radius
    @p half_w (crown at ``z0 + straight_h + half_w``). @p z0 is the THRESHOLD, set
    below the blind arch's own sill (0.9) so the door is walkable rather than a
    high niche -- the cut therefore takes the sill out across the door's width,
    and a short flight of @p step_rise steps climbs the remaining
    ``z0 - dais_top`` from the dais platform up to it.

    Returns the wall (mutated in place)."""
    x_room = length - wall_t / 2.0          # blind arch front (room) face
    x_cap = x_room + blind_recess           # blind cap: back of the recess
    # Run the cutter from just PROUD of the room face all the way past the far
    # face: it must remove the blind cap, the masonry behind it AND the arch's own
    # sill band across the opening, so the threshold is continuous from the recess
    # floor through the wall (starting at the cap would leave a 0.2 m sill lip
    # standing in front of a lower threshold -- a step down, then up).
    cut = _arched_niche_cutter(col, f"{name}_dais_door_cut", x_room - 0.02,
                               wall_t + 0.08, 0.0,
                               half_w, straight_h, z0)
    mod = wall.modifiers.new(f"{cut.name}_bool", 'BOOLEAN')
    mod.operation = 'DIFFERENCE'
    mod.object = cut
    mod.solver = 'EXACT'
    bpy.context.view_layer.objects.active = wall
    bpy.ops.object.modifier_apply(modifier=mod.name)
    # Re-UV only the fresh reveal (jambs + soffit through the wall). Start the
    # AABB past the cap so the niche's authored arch UVs around the opening are
    # left alone -- same reasoning as the lamp-niche pockets.
    _cube_uv_faces_in_aabb(wall,
                           (x_room + 0.02, -half_w - 0.06, z0 - 0.06),
                           (x_room + wall_t + 0.08, half_w + 0.06,
                            z0 + straight_h + half_w + 0.06))
    bpy.data.objects.remove(cut, do_unlink=True)

    # Steps from the dais platform up to the threshold, projecting out of the
    # recess into the room. Built top-down so the highest tread is the one
    # against the wall. Stem "dais_step_<n>" so assign_dais_material picks them
    # up with the platform's own stone (it matches on startswith).
    n_steps = max(0, int(round((z0 - dais_top) / step_rise)))
    for s in range(n_steps):
        top = z0 - s * step_rise                    # this tread's top surface
        x_out = x_room - s * step_tread             # its room-side edge
        _box(col, f"{name}_dais_step_door_{s}",
             x_out - step_tread / 2.0, 0.0, (top + dais_top) / 2.0,
             step_tread, step_half_w * 2.0, top - dais_top)
    # Archivolt over the head, standing proud on the NICHE side (facing -X down
    # the hall) so the doorway reads as dressed rather than a bare punched hole.
    # Named off the "lamp_vous" stem so prepare_uvs keeps its authored voussoir
    # UVs and the wall-material pass picks it up as masonry (see _ARCH_AUTHORED /
    # _WALL_STEMS -- both match on startswith).
    _niche_voussoirs(col, f"{name}_lamp_vous_door", x_cap, 0.0,
                     z0 + straight_h, r_in=half_w - 0.02, r_out=half_w + 0.14,
                     protrude=0.05, segs=24, u_scale=1.0, v_scale=1.0)
    return wall


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
        (surround_h + 1.7,       0.95,    0.30),   # throat -> broad chimney breast
        (chimney_top_z,          0.95,    0.30),   # stack top (through the roof)
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
    bm.faces.new(tuple(rings[0]))      # CLOSE the hood: cap the wide bottom mouth
    # Chamfer the hood's exposed arrises (a soft masonry edge, not knife-sharp).
    # SKIP (a) edges on the wall back face (yface) -- keep the mass flush to the
    # wall -- and (b) the throat transition ring (z_throat), whose junction between
    # the tapering hood and the vertical chimney is CONCAVE: beveling it cut an
    # inset notch into the sides. Leaving it unbeveled keeps a clean crease.
    # Only chamfer the vertical arrises: skip the wall-back edges (keep flush) and
    # BOTH horizontal transition rings -- the throat (concave hood->chimney) and
    # the mouth (where the hood sits on the surround). Beveling either cut inset
    # notches at the side corners that don't bridge to the neighbouring mass.
    z_rings = (surround_h, surround_h + 1.7)
    def _on_wall(e):
        return (abs(e.verts[0].co.y - yface) < 1e-5
                and abs(e.verts[1].co.y - yface) < 1e-5)
    def _ring_edge(e):
        return any(abs(e.verts[0].co.z - z) < 1e-4 and abs(e.verts[1].co.z - z) < 1e-4
                   for z in z_rings)
    chamfer_edges = [e for e in bm.edges if not _on_wall(e) and not _ring_edge(e)]
    if chamfer_edges:
        bmesh.ops.bevel(bm, geom=chamfer_edges, offset=0.04, segments=1,
                        affect='EDGES', clamp_overlap=True)
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

    # a DISTINCT ornamental band (BILLET blocks, not the chevron) running FLAT
    # ACROSS THE TOP ONLY -- no jambs, no corner returns -- above the label. Built
    # straight from the arch ornament-band primitive on a horizontal path (this is
    # exactly its head run, without the jamb/corner runs a full label adds).
    fr_hw, fr_zs, fr_in, fr_out = 0.75, 1.75, 0.70, 0.90
    fr_n = max(3, arch._ORNAMENT_STATIONS["billet"](13) + 1)
    fr_bm = bmesh.new()
    arch._ornament_band(
        fr_bm, arch._line_stations((-fr_hw - fr_in, fr_zs), (fr_hw + fr_in, fr_zs), fr_n),
        [(0.0, 1.0)] * fr_n, fr_in, fr_out, -surround_t * 0.5, -1.0, 0.10,
        "billet", 0.05, cap_start=True, cap_end=True)
    bmesh.ops.remove_doubles(fr_bm, verts=list(fr_bm.verts), dist=1e-5)
    bmesh.ops.recalc_face_normals(fr_bm, faces=fr_bm.faces[:])
    fr_me = bpy.data.meshes.new(f"{name}_fp_frieze"); fr_bm.to_mesh(fr_me); fr_bm.free()
    frieze = bpy.data.objects.new(f"{name}_fp_frieze", fr_me); col.objects.link(frieze)
    _order(frieze)

    # TALL PLINTHS under the ornament: the carved orders' jambs stop ~1.2 m up, so
    # seat each side's jamb on a plain pedestal running from the hearth top to that
    # foot (a pedestal beside the fire on each side). Spans the ornament band out
    # to the opening edge, projecting a touch proud of the label.
    hw_op, orn_out = 0.75, 0.66             # opening half-width; ornament outer radius
    pz0, pz1 = 0.18, 1.20                   # hearth top -> ornament jamb foot
    py0 = yin + proj * surround_t           # surround front face
    py1 = yin + proj * (surround_t + 0.20)  # plinth front (proud of the label)
    for sx, tag in ((-1.0, "L"), (1.0, "R")):
        x_in = wx + sx * hw_op              # fire-opening edge
        x_out = wx + sx * (hw_op + orn_out)  # ornament outer
        _box(col, f"{name}_fp_plinth_{tag}",
             0.5 * (x_in + x_out), 0.5 * (py0 + py1), 0.5 * (pz0 + pz1),
             abs(x_out - x_in), abs(py1 - py0), pz1 - pz0)


def _dais_banner(col, name, length, wall_t, width):
    """A red heraldic CLOTH banner hung in the dais' central blind arch (rpg GI
    color-bleed test). A subdivided quad given a gentle cloth billow + a rippled,
    pointed (pennant) bottom edge, facing down the hall (-X). Flat red Principled
    material -> the exporter reads its Base Color as albedo, so it voxelises RED
    into the SDF and the warm dais lamps bleed red onto the surrounding stone."""
    import bmesh
    x0 = length - wall_t / 2.0 - 0.10        # just proud of the backdrop wall face
    # Hangs above the doorway, in the TYMPANUM: the head is carried UP INTO the
    # blind arch's semicircular head (springing 3.9, crown 5.4) rather than
    # stopping at the springing, so the banner fills the arch the way a hung
    # heraldic cloth would. At z_top the arch is still 1.33 m half-width, so a
    # 0.62 half-width clears it. The hem stops above the doorway's archivolt
    # (crown 0.70 + 1.30 + 0.60 = 2.60, lip to ~2.74), leaving the door legible.
    z_top, z_bot = 4.60, 3.00                # 1.6 m drop into the arch head
    half_w = 0.62                            # 1.24 m wide -- matches the doorway
    nu, nv = 12, 20
    me = bpy.data.meshes.new(f"{name}_banner")
    bm = bmesh.new()
    grid = [[None] * (nv + 1) for _ in range(nu + 1)]
    for i in range(nu + 1):
        u = i / nu
        y = (u - 0.5) * 2.0 * half_w
        for j in range(nv + 1):
            v = j / nv
            z = z_top + (z_bot - z_top) * v
            # cloth billow toward the hall (grows toward the free bottom edge) +
            # a vertical drape wave; the last row dips to a central pennant point.
            billow = 0.06 * math.sin(u * math.pi * 2.0) * (0.25 + 0.75 * v)
            billow += 0.03 * math.sin(v * math.pi * 3.0)
            zz = z - (0.18 * (1.0 - abs(u - 0.5) * 2.0)) * (1.0 if j == nv else 0.0)
            grid[i][j] = bm.verts.new((x0 - billow, y, zz))
    bm.verts.ensure_lookup_table()
    for i in range(nu):
        for j in range(nv):
            bm.faces.new((grid[i][j], grid[i + 1][j], grid[i + 1][j + 1], grid[i][j + 1]))
    bm.normal_update()
    uv = bm.loops.layers.uv.new("UVMap")
    for f in bm.faces:
        for lp in f.loops:
            co = lp.vert.co
            lp[uv].uv = ((co.y + half_w) / (2.0 * half_w),
                         (z_top - co.z) / (z_top - z_bot))
    bm.to_mesh(me)
    bm.free()
    o = bpy.data.objects.new(f"{name}_banner", me)
    _link(col, o)
    # DYNAMIC: cloth moves, so it is excluded from the offline bake entirely (no
    # lightmap slot, not in the baked voxel albedo). The runtime instead voxelises
    # it into the sparse dynamic albedo volume each probe update, which is what
    # makes its red bleed into the probe GI. The exporter emits this flag.
    o["ferrum_dynamic"] = 1
    o["ferrum_lightmap_res"] = 0
    mat = bpy.data.materials.get(f"{name}_banner")
    if mat:
        bpy.data.materials.remove(mat)
    mat = bpy.data.materials.new(f"{name}_banner")
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes.get("Principled BSDF")
    if bsdf is not None:
        bsdf.inputs["Base Color"].default_value = (0.88, 0.012, 0.012, 1.0)  # deep saturated red
        bsdf.inputs["Roughness"].default_value = 0.9
    me.materials.append(mat)
    return o


# --- stained glass (translucency demo): simple colored, segmented glazing ---
_GLASS_COLORS = [
    ("gh_glass_red",    (0.55, 0.05, 0.05)),
    ("gh_glass_blue",   (0.06, 0.10, 0.52)),
    ("gh_glass_gold",   (0.80, 0.58, 0.10)),
    ("gh_glass_green",  (0.06, 0.38, 0.10)),
    ("gh_glass_violet", (0.34, 0.08, 0.44)),
    ("gh_glass_pale",   (0.62, 0.68, 0.72)),
]


def _glass_material(name, rgb, opacity=0.65):
    """Flat-tint glass material: the exporter emits solid materials as tint +
    roughness with no maps, and ``ferrum_opacity`` marks translucency (feeds
    the CSM translucency mask + the bake's transmission channel)."""
    mat = bpy.data.materials.get(name)
    if mat is not None:
        return mat
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes.get("Principled BSDF")
    if bsdf is not None:
        bsdf.inputs["Base Color"].default_value = (*rgb, 1.0)
        bsdf.inputs["Roughness"].default_value = 0.12
    mat["ferrum_opacity"] = opacity
    return mat


def build_stained_glass(name, opening_width=1.15, sill=2.0, jamb_h=2.5,
                        wall_t=0.5, cols=3, rows=4, arcs=5, thickness=0.10,
                        collection=None):
    """Segmented stained glazing for one arched light: a cols x rows grid of
    tinted panes over the rectangular aperture plus an arcs-wedge ring in the
    round head, every pane a thin box carrying one of the ``_GLASS_COLORS``
    glass materials (flat tint + ferrum_opacity translucency); a dark lead
    boss closes the wedge fan's hub. Origin matches the doorway panel (opening
    centre on the floor); the sheet sits just behind the NARROW exterior
    aperture so every edge buries inside the splayed reveal. Returns the new
    object (linked into @p collection)."""
    import bmesh
    me = bpy.data.meshes.new(name)
    ob = bpy.data.objects.new(name, me)
    (collection if collection is not None
     else bpy.context.scene.collection).objects.link(ob)
    mats = [_glass_material(n, c) for (n, c) in _GLASS_COLORS]
    mats.append(_glass_material("gh_lead_came", (0.07, 0.07, 0.08),
                                opacity=1.0))
    for m in mats:
        me.materials.append(m)

    bm = bmesh.new()
    y0 = -(wall_t * 0.5) + 0.05     # just behind the exterior (voussoir) face
    over = 0.05                     # bury margin into the splayed reveal
    hw = opening_width * 0.5 + over
    z0, z1 = sill - over, sill + jamb_h        # z1 = spring line

    def pane(x0, x1, za, zb, mi):
        vs = []
        for y_ in (y0 - thickness * 0.5, y0 + thickness * 0.5):
            for (x_, z_) in ((x0, za), (x1, za), (x1, zb), (x0, zb)):
                vs.append(bm.verts.new((x_, y_, z_)))
        for f in ((0, 1, 2, 3), (7, 6, 5, 4), (0, 4, 5, 1),
                  (1, 5, 6, 2), (2, 6, 7, 3), (3, 7, 4, 0)):
            fc = bm.faces.new([vs[k] for k in f])
            fc.material_index = mi

    for i in range(cols):
        for j in range(rows):
            x0 = -hw + 2.0 * hw * i / cols
            x1 = -hw + 2.0 * hw * (i + 1) / cols
            za = z0 + (z1 - z0) * j / rows
            zb = z0 + (z1 - z0) * (j + 1) / rows
            pane(x0, x1, za, zb, (i * 5 + j * 3 + (i + j) // 2) % 6)

    # round head: a ring of wedge panes (chorded quads) about the spring point
    r0, rr = 0.16, opening_width * 0.5 + over
    for k in range(arcs):
        a0 = math.pi * k / arcs
        a1 = math.pi * (k + 1) / arcs
        vs = []
        for y_ in (y0 - thickness * 0.5, y0 + thickness * 0.5):
            for (r_, a_) in ((r0, a0), (rr, a0), (rr, a1), (r0, a1)):
                vs.append(bm.verts.new((r_ * math.cos(a_), y_,
                                        z1 + r_ * math.sin(a_))))
        for f in ((0, 1, 2, 3), (7, 6, 5, 4), (0, 4, 5, 1),
                  (1, 5, 6, 2), (2, 6, 7, 3), (3, 7, 4, 0)):
            fc = bm.faces.new([vs[k2] for k2 in f])
            fc.material_index = (k * 7 + 2) % 6
    # lead boss over the fan hub (opaque -- closes the ring's centre)
    pane(-r0 - 0.02, r0 + 0.02, z1 - 0.03, z1 + r0 + 0.02, 6)

    bm.to_mesh(me)
    bm.free()
    return ob


def build_great_hall(name="great_hall", nbay=5, bay=3.6, width=8.0, wall_h=6.5,
                     wall_t=0.5, roof_rise=3.6,
                     lamp_niches=True, niche_above_dais=2.4384, niche_spacing=2.6,
                     niche_width=0.5, niche_straight_h=0.55, niche_recess=0.26,
                     collection=None):
    """Build the great hall into (a fresh child of) @p collection. Returns the
    collection. @p nbay bays of @p bay metres give the length; @p width is the
    clear span; @p wall_h the wall height; @p roof_rise the timber-roof apex
    above the walls.

    Lamp niches (arched recesses in the dais backdrop wall, one each side of the
    lord's seat): @p lamp_niches toggles them; @p niche_above_dais is the opening
    centre height above the dais platform (default 8 ft); @p niche_spacing is each
    niche's Y offset from centre; @p niche_width / @p niche_straight_h size the
    opening (round-arch crown adds niche_width/2 of rise); @p niche_recess is the
    cut depth into the wall (kept < @p wall_t so a solid backing remains)."""
    import arch
    import pier
    import column

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

    def glazing(nm):
        # simple colored, segmented stained glass in every light (translucency)
        return build_stained_glass(nm, opening_width=1.15, sill=2.0,
                                   jamb_h=2.5, wall_t=wall_t, collection=col)

    # The fireplace occupies the last north bay (by the dais) -- skip that window
    # so they don't intersect.
    fp_bay = nbay - 1
    fp_wx = (fp_bay + 0.5) * bay
    for i in range(nbay):
        if i != fp_bay:
            n = window(f"{name}_win_N_{i}")
            n.location = ((i + 0.5) * bay, half, 0.0)
            n.rotation_euler = (0, 0, math.pi)      # voussoir face -> +Y (exterior)
            gn = glazing(f"{name}_glass_N_{i}")
            gn.location = n.location
            gn.rotation_euler = n.rotation_euler
        s = window(f"{name}_win_S_{i}")
        s.location = ((i + 0.5) * bay, -half, 0.0)
        s.rotation_euler = (0, 0, 0.0)              # voussoir face -> -Y (exterior)
        gs = glazing(f"{name}_glass_S_{i}")
        gs.location = s.location
        gs.rotation_euler = s.rotation_euler
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
        # Soften the upright arrises (see _pier_bevel): the piers are the closest
        # dressed stone to the camera down the length of the hall.
        _pier_bevel(pn)
        _pier_bevel(ps)

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
        trim_width=0.14, masonry_uv=True, sill_square=True, sill_extrude=0.08,
        collection=col)
    b.location = (length, 0.0, 0.0)
    # Face the INTERIOR (-X, down the hall). The entrance at x=0 faces +X into the
    # room with rot +pi/2; the dais wall is the OPPOSITE end, so its blind recess +
    # voussoirs must face -X -- the opposite rotation (-pi/2), or they point into
    # the wall behind. (The wall body stays centred on x=length either way, so the
    # lamp niches carved on the -X room face are unaffected.)
    b.rotation_euler = (0, 0, -math.pi / 2.0)

    # --- dais blind arch dressing (following scene_demo.py): a patterned
    #     ARCHIVOLT stack (rolls framing carved billet + chevron bands + a plain
    #     fascia) around the head, and an engaged RESPOND with a cushion capital
    #     on each jamb. Named without "dais" so they read as wall masonry (group
    #     0), and placed in the arch's own frame. ---
    dais_top = 0.46                                  # dais platform top (see below)
    def _place_on_arch(obj):
        obj.location = b.location.copy()
        obj.rotation_euler = b.rotation_euler.copy()
        return obj

    _place_on_arch(arch.build_archivolt(
        name=f"{name}_dvolt", opening_width=3.0, opening_height=3.0,
        arch_shape="round", head_rise=1.5, sill_height=0.9, wall_thickness=wall_t,
        inner_radius=0.16, face="inner", collection=col))

    # a cushion-capital column, embedded as an engaged half-shaft on each jamb.
    _respsrc = column.build_column(
        name=f"{name}_respsrc", total_height=3.0, shaft_radius=0.24,
        capital_style="cushion", capital_width=0.58, capital_depth=0.58,
        capital_block_height=0.14, capital_flare=0.5, capital_flare_height=0.26,
        base_block=True, base_width=0.66, base_depth=0.66, base_block_height=0.22,
        base_flare=0.25, base_flare_height=0.10, collection=col)
    _respL, _respR = arch.embed_jamb_columns(
        _respsrc, opening_width=3.0, opening_height=3.0, sill_height=dais_top,
        wall_thickness=wall_t, inset=0.12, flatten=0.75, project=0.5,
        height=0.9 + 3.0 - dais_top, face="inner", name=f"{name}_dresp",
        collection=col)
    bpy.data.objects.remove(_respsrc, do_unlink=True)
    _place_on_arch(_respL)
    _place_on_arch(_respR)

    # --- twin arched LAMP NICHES carved INTO the dais backdrop wall, flanking the
    #     central blind arch. Sited ~8 ft (2.44 m) above the dais platform (a lamp
    #     glowing over the lord's head), one to each side of the seat at the wall
    #     centre. They are true recesses BOOLEAN-CUT into the backdrop wall (the
    #     dais arch object), opening toward the INTERIOR (-X, down the hall), and
    #     stop short of the far face (recess < wall_t) so no light leaks. ---
    dais_top = 0.46                                  # dais platform top (see below)
    if lamp_niches:
        _carve_lamp_niches(
            col, b, x_room_face=length - wall_t / 2.0,
            centre_z=dais_top + niche_above_dais,
            y_offsets=(niche_spacing, -niche_spacing),
            half_w=niche_width / 2.0, straight_h=niche_straight_h,
            recess=niche_recess)

    # --- the lord's DOOR: an arched opening cut clean through the BACK of the
    #     central blind arch, so the niche becomes a real doorway off the dais.
    #     Must run AFTER the lamp niches -- both boolean the same wall object, and
    #     each apply rebuilds its mesh. The banner is re-hung above this head. ---
    _dais_doorway(col, b, name, length, wall_t, blind_recess=0.22)

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

    # low stepped base plinth along the foot of every wall
    _wall_base_plinths(col, name, length, half, wall_t, dais_top, dais_w)

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

    # roof skin: eave AT the wall top (over the wall outer face), ridge LIFTED
    # above the rafters. The rafters (praf) foot on the wall head and rise to
    # `apex` on the same plane as the skin, so a skin drawn to `apex` would be
    # pierced by the 0.26-thick rafters (they poke through -> overlap + leak).
    # Raising ONLY the ridge tilts the skin up so its underside clears the rafter
    # tops, while the eave stays seated on the wall head (no gap to leak through).
    apex_skin = apex + 0.40
    bm_verts = [
        (x0, nO, top), (x0, sO, top), (x0, midy, apex_skin),
        (x1, nO, top), (x1, sO, top), (x1, midy, apex_skin)]
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
    # Thick enough to span many SDF voxels (~0.06 m) so the lightmap/GI SDF bake
    # doesn't leak sun/sky through the roof skin. 0.12 (~2 voxels) and 0.35 both
    # left thin spots along the ridge/gable seams; 0.55 fully seals it.
    # Extrude UPWARD/outward (-normal here, since the skin faces wind
    # inward/down): the inner face stays on the lifted skin plane clearing the
    # rafters, and the shell thickens away from the room.
    sol.thickness = 0.55
    sol.offset = -1.0

    # --- hooded wall fireplace against the north wall, in the last (dais) bay ---
    yin = float(Nlo[1])
    _fireplace(col, name, wx=fp_wx, wall_inner_y=yin,
               chimney_top_z=apex + 1.6, sign=+1)
    # punch the flue hole so the chimney exits through the roof (boolean cutter
    # over the stack footprint: wx +/- 0.48 in X, yin-0.3..yin in Y).
    _punch_flue(col, roof, f"{name}_flue", fp_wx, yin - 0.15, 2.05, 0.20)

    # --- red heraldic cloth banner in the dais' central blind arch. Built LAST on
    #     purpose: it is a DYNAMIC object (tagged below), so it takes no lightmap
    #     slot, and appending it keeps every existing mesh's index -- and therefore
    #     the already-baked lightmap's per-mesh atlas rects -- valid. No re-bake. ---
    _dais_banner(col, name, length, wall_t, width)
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
_ARCH_AUTHORED = ("win_N", "win_S", "entrance", "dais_arch", "lamp_vous",
                  "pier_N", "pier_S",
                  "fp_opening", "fp_roll", "fp_chevron", "fp_label")
# (fp_frieze is a raw ornament band -> world-cube UV via prepare_uvs, not authored)


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
               "entrance", "dais_arch", "lamp_vous",  # niche voussoirs = masonry
               "dvolt", "dresp", "plinth",             # dais dressing + wall plinths
               "fp_")                                  # fireplace stone = wall masonry


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
        tint_map=os.path.join(bake_dir, "tint.png"),
        # MEDIEVAL ASHLAR, not fired brick. The baked mask holds 17 courses x 8
        # blocks per tile, so `tile` metres divided by those counts IS the block
        # size. The old (4.5, 2.6) gave 0.56 x 0.15 m blocks -- a 3.7:1 aspect,
        # essentially a fired brick (3.3:1) -- which read far too small and wide
        # for coursed stone. (4.4, 7.15) gives 0.55 x 0.42 m blocks at ~1.3:1 --
        # squarish dressed stones with deep courses, the way coursed ashlar reads.
        # All four maps share this tile, so mask/normal/ao/height stay in
        # registration whatever it is set to; but note U and V are scaled by
        # different factors off the source bake, so the baked stone relief is
        # squashed ~1.6x horizontally. If that shows as visibly stretched stones,
        # the real fix is re-baking the wall with a squarer bond, not more UV.
        tile=(4.4, 7.15))
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
        # Cut from GRANITE, not the default limestone: a real dark crystalline
        # paving stone rather than pale blocks tinted grey.
        brick="granite",
        brick_contrast=0.62, mortar_contrast=0.25,
        mortar_tint=(0.010, 0.010, 0.011),           # super-dark joints
        # MUCH darker: worn flagstone in a hall lit by fire, not showroom stone.
        brick_tint=(0.085, 0.090, 0.105), brick_sat=0.60,
        # MUCH less rough: centuries of footfall polish flagstone to a sheen, and
        # that low roughness is what makes the fire read across the floor.
        rough_brick=0.22, rough_mortar=0.55,
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


def _stone_bevel(obj, width=0.014, segments=2, vertical_only=False,
                 angle_deg=40.0):
    """Bevel a dressed-stone block's arrises and give it true FACE-WEIGHTED
    NORMALS. A razor arris is the giveaway CGI edge; real cut stone always has a
    slight worked chamfer that catches a highlight.

    @p vertical_only True marks only Z-parallel edges (bevel limited to WEIGHT),
    for pieces whose horizontal arrises should stay crisp -- an engaged pier's bed
    joints, say. False bevels every edge sharper than @p angle_deg, which is what
    a free-standing block wants (dais, step nosings, plinth courses).

    The bevel is APPLIED, so it is real geometry and the exporter's evaluated mesh
    is unambiguous.

    Face-weighted normals are then computed HERE rather than left to modifiers:
    each vertex normal is the sum of its adjacent face normals weighted by face
    AREA, written as custom split normals. Large flat faces dominate completely,
    so the broad faces stay perfectly flat while the narrow bevel strips inherit
    their neighbours' normals and read as a highlight on the arris. ("Smooth by
    Angle" is a different thing entirely -- it splits normals at an angle
    threshold and rounds the faces off.)"""
    for m in [m for m in obj.modifiers
              if m.name in ("stone_bevel", "pier_bevel", "pier_wn", "dais_bevel",
                            "dais_wn") or
              (m.type == 'NODES' and m.name.startswith("Smooth by Angle"))]:
        obj.modifiers.remove(m)
    me = obj.data
    if vertical_only:
        # Blender 4.1+ keeps edge bevel weight in a named FLOAT attribute on the
        # EDGE domain (the old edge.bevel_weight field is gone).
        attr = me.attributes.get("bevel_weight_edge")
        if attr is None:
            attr = me.attributes.new("bevel_weight_edge", 'FLOAT', 'EDGE')
        vs = me.vertices
        for e in me.edges:
            d = vs[e.vertices[1]].co - vs[e.vertices[0]].co
            upright = (abs(d.z) > 1e-5 and abs(d.x) < 1e-5 and abs(d.y) < 1e-5)
            attr.data[e.index].value = 1.0 if upright else 0.0
    for x in list(bpy.context.selected_objects):
        x.select_set(False)
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj

    bev = obj.modifiers.new("stone_bevel", 'BEVEL')
    bev.width = width
    bev.segments = segments
    if vertical_only:
        bev.limit_method = 'WEIGHT'
    else:
        bev.limit_method = 'ANGLE'
        bev.angle_limit = math.radians(angle_deg)
    bev.harden_normals = False           # normals are set by hand, below
    bpy.ops.object.modifier_apply(modifier=bev.name)

    # --- face-weighted normals, computed directly on the applied mesh ---
    me = obj.data                        # re-fetch: modifier_apply rebuilt it
    bpy.ops.object.shade_smooth()        # custom split normals need smooth shading
    acc = [mathutils.Vector((0.0, 0.0, 0.0)) for _ in me.vertices]
    for poly in me.polygons:
        contrib = poly.normal * poly.area          # AREA weighting is the point
        for vi in poly.vertices:
            acc[vi] += contrib
    for i, n in enumerate(acc):
        if n.length > 1e-9:
            acc[i] = n.normalized()
        else:                            # degenerate: fall back to the mesh normal
            acc[i] = me.vertices[i].normal.copy()
    me.normals_split_custom_set_from_vertices(acc)
    return obj


def _pier_bevel(obj, width=0.014, segments=2):
    """An engaged pier: bevel the UPRIGHT arrises only (see @ref _stone_bevel),
    leaving the horizontal bed joints crisp."""
    return _stone_bevel(obj, width=width, segments=segments, vertical_only=True)


def _dais_bevel(obj, width=0.008, segments=2, angle_deg=40.0):
    """A free-standing dressed block -- the dais platform, a step, a plinth
    course: bevel EVERY arris (the tread nosings matter as much as the uprights)
    with the same applied-bevel + face-weighted-normal treatment as the piers."""
    return _stone_bevel(obj, width=width, segments=segments,
                        vertical_only=False, angle_deg=angle_deg)


def assign_dais_material(col, bake_dir, name="great_hall_floor_stone"):
    """The raised dais + its steps read as the SAME big STONE FLAGS as the floor
    (not marble): reuse the floor flagstone material at WORLD scale (1 uv unit ==
    1 m, matching the floor) with a small edge bevel. The dais ARCH keeps the wall
    masonry (handled by ``assign_wall_material``)."""
    mat = bpy.data.materials.get(name)             # built by assign_floor_material
    if mat is None:                                # standalone call -> build it
        mat = assign_floor_material(col, bake_dir, name)
    pre = col.name + "_"

    def _level(stem):                              # ascending: lower step -> dais top
        if stem == "dais":
            return 3
        if stem.startswith("dais_step_"):
            return 2 - int(stem.rsplit("_", 1)[-1])   # step_0 higher(2), step_1(1)
        return 0

    for o in col.objects:
        if o.type != "MESH":
            continue
        stem = o.name[len(pre):] if o.name.startswith(pre) else o.name
        if stem == "dais" or stem.startswith("dais_step"):
            _dais_bevel(o)
            # Scale the world-cube UV up ever so slightly per step level so the
            # flag pattern does NOT continue unbroken across the step edges (the
            # tiles would otherwise line up straight up the risers/treads).
            world_cube_uv(o, 1.0 + 0.02 * _level(stem))
            o.data.materials.clear()
            o.data.materials.append(mat)
    return mat


def _setup_lighting(name="great_hall"):
    """Explicit sun + sky so the bake lighting is deterministic (not dependent on
    session/factory defaults). Direction is the design's raking sun; brightness is
    DOUBLED (sun energy 20, sky ~0.31,0.38,0.52) per the hall's lit look."""
    import mathutils
    for o in list(bpy.data.objects):
        if o.type == 'LIGHT' and o.data.type == 'SUN':
            bpy.data.objects.remove(o, do_unlink=True)
    sd = bpy.data.lights.new(name + "_sun", 'SUN')
    sd.energy = 20.0                       # doubled (was 10)
    sd.color = (1.0, 1.0, 1.0)
    so = bpy.data.objects.new(name + "_sun", sd)
    bpy.context.scene.collection.objects.link(so)
    # Blender sun points along local -Z; orient it to the design travel direction
    # (engine SUN_DIR (-0.557,-0.602,-0.572) -> Blender fwd (-0.557, 0.572, -0.602)).
    fwd = mathutils.Vector((-0.557, 0.572, -0.602)).normalized()
    so.rotation_euler = fwd.to_track_quat('-Z', 'Y').to_euler()
    w = bpy.context.scene.world or bpy.data.worlds.new("World")
    bpy.context.scene.world = w
    w.use_nodes = True
    bg = w.node_tree.nodes.get("Background")
    if bg is not None:
        bg.inputs[0].default_value = (0.3078, 0.3770, 0.5176, 1.0)  # doubled sky
        bg.inputs[1].default_value = 1.0


def _add_point(name, loc, color, energy, rng, radius=0.12, shadow=True):
    d = bpy.data.lights.new(name, 'POINT')
    d.color = color; d.energy = energy
    d.use_custom_distance = True; d.cutoff_distance = rng
    d.shadow_soft_size = radius
    try: d.use_shadow = shadow
    except AttributeError: pass
    o = bpy.data.objects.new(name, d); o.location = loc
    bpy.context.scene.collection.objects.link(o)
    return o


def _add_spot_up(name, loc, color, energy, rng, outer_deg, inner_deg):
    d = bpy.data.lights.new(name, 'SPOT')
    d.color = color; d.energy = energy
    d.use_custom_distance = True; d.cutoff_distance = rng
    d.spot_size = math.radians(2.0 * outer_deg)          # full cone angle
    d.spot_blend = max(0.0, 1.0 - inner_deg / max(outer_deg, 1e-3))
    o = bpy.data.objects.new(name, d); o.location = loc
    o.rotation_euler = (math.pi, 0.0, 0.0)               # local -Z -> world +Z (up)
    bpy.context.scene.collection.objects.link(o)
    return o


def _mesh_group_bbox(col, substr):
    """World-space (found, center, min, max) of the meshes whose name contains
    ``substr`` (empty min/max if none found)."""
    import mathutils
    mn = mathutils.Vector(( 1e30,  1e30,  1e30))
    mx = mathutils.Vector((-1e30, -1e30, -1e30))
    found = False
    for o in col.objects:
        if o.type != 'MESH' or substr not in o.name:
            continue
        found = True
        for c in o.bound_box:
            w = o.matrix_world @ mathutils.Vector(c)
            mn = mathutils.Vector((min(mn.x, w.x), min(mn.y, w.y), min(mn.z, w.z)))
            mx = mathutils.Vector((max(mx.x, w.x), max(mx.y, w.y), max(mx.z, w.z)))
    return found, (mn + mx) * 0.5, mn, mx


def _setup_dynamic_lights(name, col):
    """Add the hall's DYNAMIC punctual lights, PLACED AT the architecture: a warm
    fire light inside the fireplace opening and a warm lamp in each carved arched
    niche (lamp_vous). Realtime + SDF-probe-GI + shadow-casting; NOT baked (no
    lightmap re-bake). Energies are in the engine's radiance units (a few hundred,
    the exporter passes Blender `energy` straight to descriptor `intensity`), tuned
    to read as strongly as hall_lit_dynamic. Each light is nudged toward the hall
    centre so it spills into the room rather than sitting buried in the recess."""
    _, hall_c, _, _ = _mesh_group_bbox(col, name)   # whole-hall centre.

    def place_toward_room(cen, dist=0.35):
        import mathutils
        d = hall_c - cen
        if d.length > 1e-4:
            return cen + d.normalized() * dist
        return cen

    # Fireplace: a warm fire light in the hearth opening.
    ok, c, _, _ = _mesh_group_bbox(col, name + "_fp_opening")
    if not ok:
        ok, c, _, _ = _mesh_group_bbox(col, name + "_fp_hearth")
    if ok:
        p = place_toward_room(c, 0.4)
        _add_point(name + "_fire", (p.x, p.y, p.z), (1.0, 0.45, 0.15),
                   280.0, 6.0, radius=0.2, shadow=True)

    # Lamp niches (arched wall niches): a warm lamp in each.
    for i in range(8):
        ok, c, _, _ = _mesh_group_bbox(col, name + "_lamp_vous_%d" % i)
        if not ok:
            break
        p = place_toward_room(c, 0.3)
        _add_point(name + "_niche_%d" % i, (p.x, p.y, p.z), (1.0, 0.62, 0.28),
                   210.0, 5.0, radius=0.1, shadow=True)


def build_hall_scene(bake_root, collection=None, name="great_hall"):
    """Regenerate the whole great-hall scene: geometry (``build_great_hall``),
    world-scale UVs (``prepare_uvs``), then every material -- wall, floor, reveal
    weave and timber -- from the bake maps under ``bake_root`` (which holds
    ``bake`` / ``bake_floor`` / ``bake_weave``). One call reproduces the scene, so
    it can be passed as the regeneration callback to the exporter/baker. Returns
    the collection."""
    col = build_great_hall(name=name, collection=collection)
    # Probe-shell refinement (see the exporter's ferrum_building handling):
    # tag every hall mesh so the offline placer densifies bricks around ALL
    # the interior geometry, not just the coarse air lattice.
    for o in col.objects:
        if o.type == "MESH":
            o["ferrum_building"] = 1
    prepare_uvs(col)
    assign_wall_material(col, os.path.join(bake_root, "bake"))
    assign_floor_material(col, os.path.join(bake_root, "bake_floor"))
    assign_reveal_weave(col, os.path.join(bake_root, "bake_weave"))
    assign_timber_material(col)
    assign_roof_material(col)
    assign_dais_material(col, os.path.join(bake_root, "bake_floor"))
    _setup_lighting(name)
    _setup_dynamic_lights(name, col)   # carried lantern + sconces (dynamic, unbaked)
    # Level probe-density tuning, authored on the scene so the exporter emits it
    # into the descriptor's probe spec (not hardcoded in the exporter). These are
    # the hall's tuned grid steps (denser than the engine default).
    scn = bpy.context.scene
    scn["ferrum_probe_spacing"] = 1.1
    scn["ferrum_probe_vspacing"] = 0.8
    return col
