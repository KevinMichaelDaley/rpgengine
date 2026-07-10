"""Parametric vault mesh generators (bpy/bmesh).

Standalone generators for vaulted ceilings, under the parametric architectural
mesh epic (rpg-pm1c, ticket rpg-imlo). Each takes a handful of geometric
parameters and returns one watertight quad-shell mesh:

- build_barrel_vault -- a tunnel vault with any doorway head shape.
- build_groin_vault  -- a single cross-vault bay (two barrels crossing).
- build_dome         -- a hemispherical dome cap.

Conventions (shared with arch.py / column.py): Z up, the piece's local origin
sits at the centre of its footprint on the springing plane (Z = 0), and shells
have an explicit thickness (intrados + extrados + closed edges). Surfaces are
unwrapped along marked seams and scaled to a uniform UV_SCALE texel density so a
shared tiling material reads consistently across the whole library.
"""

import math
from collections import defaultdict

import bmesh
import bpy


# ---------------------------------------------------------------------------
# UV seams and uniform texel density (shared convention with arch.py/column.py)
# ---------------------------------------------------------------------------

UV_SCALE = 1.0


def _mark_seam(v0, v1):
    """Mark the edge between two verts as a UV seam (no-op if not adjacent)."""
    for e in v0.link_edges:
        if e.other_vert(v0) is v1:
            e.seam = True
            return


def _normalize_island_density(obj, density):
    """Scale each UV island so its texel density equals *density* UV/metre."""
    me = obj.data
    if not me.uv_layers.active:
        return
    uvl = me.uv_layers.active.data
    seam = {frozenset(e.vertices): e.use_seam for e in me.edges}
    edge_faces = defaultdict(list)
    for p in me.polygons:
        vs = list(p.vertices)
        for a in range(len(vs)):
            edge_faces[frozenset((vs[a], vs[(a + 1) % len(vs)]))].append(p.index)
    parent = list(range(len(me.polygons)))

    def find(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x

    for key, fs in edge_faces.items():
        if len(fs) == 2 and not seam.get(key, False):
            ra, rb = find(fs[0]), find(fs[1])
            if ra != rb:
                parent[ra] = rb
    islands = defaultdict(list)
    for p in me.polygons:
        islands[find(p.index)].append(p.index)
    for faces in islands.values():
        a3 = sum(me.polygons[f].area for f in faces)
        auv = cx = cy = cnt = 0.0
        for f in faces:
            li = list(me.polygons[f].loop_indices)
            sh = 0.0
            for a in range(len(li)):
                u0, u1 = uvl[li[a]].uv, uvl[li[(a + 1) % len(li)]].uv
                sh += u0.x * u1.y - u1.x * u0.y
                cx += u0.x
                cy += u0.y
                cnt += 1
            auv += abs(sh) * 0.5
        if a3 < 1e-9 or auv < 1e-12 or cnt < 1.0:
            continue
        factor = density / (auv / a3) ** 0.5
        cx /= cnt
        cy /= cnt
        for f in faces:
            for li in me.polygons[f].loop_indices:
                u = uvl[li].uv
                uvl[li].uv = ((u.x - cx) * factor + cx, (u.y - cy) * factor + cy)


def _finalize_uvs(obj, density=UV_SCALE):
    """Unwrap along the marked seams, then equalise every island to *density*."""
    win = bpy.context.window
    area = next((a for a in (win.screen.areas if win else [])
                 if a.type == 'VIEW_3D'), None)
    if area is None:
        return
    region = next(r for r in area.regions if r.type == 'WINDOW')
    prev_sel = list(bpy.context.selected_objects)
    prev_act = bpy.context.view_layer.objects.active
    for o in prev_sel:
        o.select_set(False)
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    ov = dict(window=win, area=area, region=region,
              active_object=obj, object=obj, edit_object=obj)
    bpy.ops.object.mode_set(mode='EDIT')
    with bpy.context.temp_override(**ov):
        bpy.ops.mesh.select_all(action='SELECT')
        bpy.ops.uv.unwrap(method='ANGLE_BASED', margin=0.001)
    bpy.ops.object.mode_set(mode='OBJECT')
    _normalize_island_density(obj, density)
    obj.select_set(False)
    for o in prev_sel:
        o.select_set(True)
    if prev_act:
        bpy.context.view_layer.objects.active = prev_act


def _finish(bm, name, collection, smooth_angle=0.0):
    """Recalculate outward normals, apply angle-based smoothing (0 = faceted),
    build the object, link it, unwrap it, and return it."""
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    if smooth_angle > 0.0:
        thr = math.radians(smooth_angle)
        for f in bm.faces:
            f.smooth = True
        for e in bm.edges:
            if len(e.link_faces) == 2:
                e.smooth = e.calc_face_angle() <= thr
    mesh = bpy.data.meshes.new(name)
    bm.to_mesh(mesh)
    bm.free()
    obj = bpy.data.objects.new(name, mesh)
    (collection or bpy.context.scene.collection).objects.link(obj)
    _finalize_uvs(obj)
    return obj


# ---------------------------------------------------------------------------
# Arch head shapes (shared vocabulary with arch.py's doorway heads)
# ---------------------------------------------------------------------------

def _head_point(shape, a, half_w, spring_h, head_rise):
    """One point (x, z) on the arch head for parameter *a* in [0, 1], left
    spring (a=0) over the top to the right spring (a=1). Shapes: "round"
    (elliptical; semicircle when head_rise == half_w), "flat" (lintel at the
    spring line), "triangular" (gabled peak), "pointed" (two-centred gothic)."""
    if shape == "round":
        return (-half_w * math.cos(math.pi * a),
                spring_h + head_rise * math.sin(math.pi * a))
    if shape == "flat":
        return (-half_w + 2.0 * half_w * a, spring_h)
    if shape == "triangular":
        return (-half_w + 2.0 * half_w * a,
                spring_h + head_rise * (1.0 - abs(2.0 * a - 1.0)))
    if shape == "pointed":
        xc = (head_rise * head_rise - half_w * half_w) / (2.0 * half_w)
        radius = xc + half_w
        left = a <= 0.5
        s = a / 0.5 if left else (1.0 - a) / 0.5
        cx = xc if left else -xc
        ax = -half_w if left else half_w
        ang_spring = math.atan2(0.0, ax - cx)
        ang_apex = math.atan2(head_rise, -cx)
        ang = ang_spring + (ang_apex - ang_spring) * s
        return (cx + radius * math.cos(ang), spring_h + radius * math.sin(ang))
    raise ValueError('arch_shape must be round, flat, triangular, or pointed')


def _arch_ring(shape, span, rise, thickness, segments):
    """Inner (intrados) and outer (extrados) head-curve point lists in the
    (x, z) cross-section plane. The outer is the inner offset by *thickness*
    along a MITRED outward normal — at a sharp head (triangular / pointed apex)
    the offset extends by 1/cos(half-angle) so the two outer slopes meet
    cleanly at the corner instead of kinking; on smooth curves it is a plain
    constant-thickness offset."""
    half = span * 0.5
    below_z = -0.5 * span - thickness
    inner = [_head_point(shape, i / segments, half, 0.0, rise)
             for i in range(segments + 1)]
    # Per-segment outward unit normals.
    seg = []
    for i in range(len(inner) - 1):
        tx, tz = inner[i + 1][0] - inner[i][0], inner[i + 1][1] - inner[i][1]
        length = math.hypot(tx, tz) or 1.0
        nx, nz = tz / length, -tx / length
        mx, mz = 0.5 * (inner[i][0] + inner[i + 1][0]), \
            0.5 * (inner[i][1] + inner[i + 1][1])
        if nx * mx + nz * (mz - below_z) < 0.0:
            nx, nz = -nx, -nz
        seg.append((nx, nz))
    outer = []
    for i in range(len(inner)):
        if i == 0:
            vn, sn = seg[0], seg[0]
        elif i == len(inner) - 1:
            vn, sn = seg[-1], seg[-1]
        else:
            bx, bz = seg[i - 1][0] + seg[i][0], seg[i - 1][1] + seg[i][1]
            bl = math.hypot(bx, bz) or 1.0
            vn, sn = (bx / bl, bz / bl), seg[i]
        cos_i = max(0.2, vn[0] * sn[0] + vn[1] * sn[1])   # mitre factor, clamped
        d = thickness / cos_i
        outer.append((inner[i][0] + vn[0] * d, inner[i][1] + vn[1] * d))
    return inner, outer


# ---------------------------------------------------------------------------
# Barrel vault
# ---------------------------------------------------------------------------

def build_barrel_vault(
    name="barrel_vault",
    span=4.0,
    length=6.0,
    arch_shape="round",
    rise=None,
    thickness=0.3,
    arch_segments=16,
    length_segments=1,
    smooth_angle=35.0,
    collection=None,
):
    """Build a barrel (tunnel) vault: a head-curve cross-section swept straight
    along the tunnel as one watertight quad shell.

    Parameters:
      span      -- clear width across the tunnel (X).
      length    -- length along the tunnel (Y).
      arch_shape-- head profile, the doorway vocabulary: "round" (semicircle /
                   segmental), "flat" (a flat slab), "triangular" (gabled),
                   "pointed" (gothic).
      rise      -- crown height above the springing (Z). None -> span/2.
      thickness -- shell thickness (offset along the head normal).
      arch_segments / length_segments -- facet counts.

    Origin at the footprint centre on the springing plane (Z=0), tunnel along
    +/-Y, span along +/-X, rising +Z. Returns the object.
    """
    if span <= 0.0 or length <= 0.0 or thickness <= 0.0:
        raise ValueError("span, length and thickness must be > 0")
    if arch_segments < 2 or length_segments < 1:
        raise ValueError("arch_segments >= 2 and length_segments >= 1")
    if rise is None:
        rise = span * 0.5
    if arch_shape != "flat" and rise <= 0.0:
        raise ValueError("rise must be > 0")

    inner, outer = _arch_ring(arch_shape, span, rise, thickness, arch_segments)
    ls = length_segments
    ys = [-length * 0.5 + length * (j / ls) for j in range(ls + 1)]
    n = arch_segments

    bm = bmesh.new()
    in_v = [[bm.verts.new((x, y, z)) for (x, z) in inner] for y in ys]
    out_v = [[bm.verts.new((x, y, z)) for (x, z) in outer] for y in ys]

    for j in range(ls):
        for i in range(n):
            bm.faces.new((in_v[j][i], in_v[j][i + 1],
                          in_v[j + 1][i + 1], in_v[j + 1][i]))       # intrados
            bm.faces.new((out_v[j][i], out_v[j + 1][i],
                          out_v[j + 1][i + 1], out_v[j][i + 1]))     # extrados
        bm.faces.new((in_v[j][0], in_v[j + 1][0],
                      out_v[j + 1][0], out_v[j][0]))                 # left edge
        bm.faces.new((in_v[j][n], out_v[j][n],
                      out_v[j + 1][n], in_v[j + 1][n]))              # right edge
    for i in range(n):                                              # end caps
        bm.faces.new((in_v[0][i], out_v[0][i],
                      out_v[0][i + 1], in_v[0][i + 1]))
        bm.faces.new((in_v[ls][i], in_v[ls][i + 1],
                      out_v[ls][i + 1], out_v[ls][i]))

    # UV seams: one longitudinal cut along the left springing unrolls the swept
    # shell; both end-cap rings are seamed off as their own islands.
    for j in range(ls):
        _mark_seam(in_v[j][0], in_v[j + 1][0])
    for ri, ro in ((in_v[0], out_v[0]), (in_v[ls], out_v[ls])):
        for i in range(n):
            _mark_seam(ri[i], ri[i + 1])
            _mark_seam(ro[i], ro[i + 1])
        _mark_seam(ri[0], ro[0])
        _mark_seam(ri[n], ro[n])
    return _finish(bm, name, collection, smooth_angle)


# ---------------------------------------------------------------------------
# Groin vault
# ---------------------------------------------------------------------------

def _half_head_samples(shape, rise, segments):
    """Normalized arch half-profile: (q, h) samples for the crown->spring half
    at unit half-width — q in [0, 1] (0 crown, 1 spring), h the height (rise ->
    0). Drives both crossing barrels of a groin vault."""
    q, h = [], []
    for k in range(segments + 1):
        p = 0.5 + 0.5 * (k / segments)
        x, z = _head_point(shape, p, 1.0, 0.0, rise)
        q.append(x)
        h.append(z)
    return q, h


def _barrel_solid(shape, span, rise, length, axis, z_bot, segs, name, coll):
    """A closed barrel SOLID for the boolean groin build: the arch head profile
    over the cross axis, boxed down to *z_bot* (below the springing), extruded
    along *axis* ('y' -> arch over X, 'x' -> arch over Y). Capped, watertight."""
    half = span * 0.5
    arch = [_head_point(shape, i / segs, half, 0.0, rise)
            for i in range(segs + 1)]
    sec = list(arch) + [(half, z_bot), (-half, z_bot)]   # closed (cross, z) loop
    bm = bmesh.new()

    def pos(u, s, z):
        return (u, s, z) if axis == "y" else (s, u, z)

    r0 = [bm.verts.new(pos(u, -length * 0.5, z)) for (u, z) in sec]
    r1 = [bm.verts.new(pos(u, length * 0.5, z)) for (u, z) in sec]
    m = len(sec)
    for i in range(m):
        j = (i + 1) % m
        bm.faces.new((r0[i], r0[j], r1[j], r1[i]))
    bm.faces.new(list(reversed(r0)))
    bm.faces.new(list(r1))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    o = bpy.data.objects.new(name, me)
    coll.objects.link(o)
    return o


def build_groin_vault(
    name="groin_vault",
    bay_width=4.0,
    bay_depth=4.0,
    arch_shape="round",
    rise=None,
    thickness=0.3,
    jamb_height=0.0,
    arch_segments=24,
    smooth_angle=35.0,
    collection=None,
):
    """Build a single groin (cross) vault bay: two barrel vaults meeting at
    right angles, with sharp diagonal groins rising from the four corners to
    the central crown, and a full lunette arch on each of the four walls.

    Built the standard way: two crossing barrels are boolean-UNIONed (their
    upper envelope forms the vault and the intersection makes the groins), the
    coincident verts along the groins are merged, the lower halves and end caps
    are stripped, the springing is optionally extruded down into jamb walls, and
    the surface is solidified to the shell *thickness*.

    Parameters: bay_width (X) / bay_depth (Y) footprint; arch_shape/rise the
    head profile (any doorway shape: round/flat/triangular/pointed); thickness
    the shell; jamb_height extrudes the wall pieces down below the springing
    (0 = none); arch_segments the facet count per barrel. Origin at the
    footprint centre on the springing plane (Z=0). Returns the object.
    """
    if bay_width <= 0.0 or bay_depth <= 0.0 or thickness <= 0.0:
        raise ValueError("bay dimensions and thickness must be > 0")
    if arch_segments < 3:
        raise ValueError("arch_segments must be >= 3")
    ax, ay = bay_width * 0.5, bay_depth * 0.5
    if rise is None:
        rise = min(ax, ay)
    if arch_shape != "flat" and rise <= 0.0:
        raise ValueError("rise must be > 0")
    coll = collection or bpy.context.scene.collection
    z_bot = -max(0.3, rise * 0.4)

    # Two crossing barrel solids: A arches over X (tunnel along Y), B over Y.
    a = _barrel_solid(arch_shape, bay_width, rise, bay_depth, "y", z_bot,
                      arch_segments, name + "_boolA", coll)
    b = _barrel_solid(arch_shape, bay_depth, rise, bay_width, "x", z_bot,
                      arch_segments, name + "_boolB", coll)
    for oo in list(bpy.context.selected_objects):
        oo.select_set(False)
    bpy.context.view_layer.objects.active = a
    a.select_set(True)
    mod = a.modifiers.new("bool", 'BOOLEAN')
    mod.operation = 'UNION'
    mod.object = b
    mod.solver = 'EXACT'
    bpy.ops.object.modifier_apply(modifier="bool")
    bpy.data.objects.remove(b, do_unlink=True)

    bm = bmesh.new()
    bm.from_mesh(a.data)
    bpy.data.objects.remove(a, do_unlink=True)
    # Merge the coincident verts the EXACT boolean leaves along the groins.
    bmesh.ops.remove_doubles(bm, verts=list(bm.verts), dist=1e-4)
    # Clip everything below the springing with a CLEAN cut at z=0 (a centroid
    # test leaves a jagged base — the near-vertical springing faces get removed
    # at inconsistent heights), so all four lunettes spring at exactly z=0.
    bmesh.ops.bisect_plane(
        bm, geom=list(bm.verts) + list(bm.edges) + list(bm.faces),
        plane_co=(0.0, 0.0, 0.0), plane_no=(0.0, 0.0, 1.0),
        clear_inner=True, dist=1e-5)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    # Remove the barrel end caps at the four walls (the lunette openings).
    eps = 1e-3
    todel = []
    for f in bm.faces:
        vs = f.verts
        if (all(abs(v.co.y - ay) < eps for v in vs)
                or all(abs(v.co.y + ay) < eps for v in vs)
                or all(abs(v.co.x - ax) < eps for v in vs)
                or all(abs(v.co.x + ax) < eps for v in vs)):
            todel.append(f)
    bmesh.ops.delete(bm, geom=todel, context='FACES')
    bmesh.ops.delete(bm, geom=[v for v in bm.verts if not v.link_faces],
                     context='VERTS')
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)

    # Jambs: extrude the open lunette boundary down to a FLAT base plane at
    # z = -jamb_height (dropping the springing straight to a level base, filling
    # the wall/tympanum below each lunette). Shifting the boundary down instead
    # would keep it arched, so the final base clip only catches the corner
    # springers and leaves the jamb bottoms jagged.
    if jamb_height > 0.0:
        boundary = [e for e in bm.edges if len(e.link_faces) == 1]
        res = bmesh.ops.extrude_edge_only(bm, edges=boundary)
        for g in res["geom"]:
            if isinstance(g, bmesh.types.BMVert):
                g.co.z = -jamb_height

    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bmesh.ops.solidify(bm, geom=list(bm.faces), thickness=thickness)
    # Clip the base dead-flat at the springing (or jamb bottom): solidify pushes
    # the shell rim below the base plane, and an angled springing makes it worse,
    # so cut everything below with one plane for a flat, level base on any shape.
    base_z = -jamb_height
    bmesh.ops.bisect_plane(
        bm, geom=list(bm.verts) + list(bm.edges) + list(bm.faces),
        plane_co=(0.0, 0.0, base_z), plane_no=(0.0, 0.0, 1.0),
        clear_inner=True, dist=1e-5)
    bmesh.ops.delete(bm, geom=[v for v in bm.verts if not v.link_faces],
                     context='VERTS')
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    # UV seams: cut the sharp creases (groins, lunette rims, shell edges).
    thr = math.radians(30.0)
    for e in bm.edges:
        if len(e.link_faces) == 2 and e.calc_face_angle() > thr:
            e.seam = True
    return _finish(bm, name, collection, smooth_angle)
