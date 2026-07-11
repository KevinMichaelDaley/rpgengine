"""Parametric arched-doorway generator (ticket rpg-imlo, epic rpg-pm1c).

An arched_doorway is a wall panel with a doorway opening cut through it. It is
built the way a box-modeler would, from KMD's recipe:

  1. Two open polylines with the SAME structure are defined in the wall plane,
     both running bottom-left -> up -> over the top -> down -> bottom-right:
       - the OUTER curve: the wall panel outline (a grid-snappable rectangle
         with its bottom edge omitted, since the opening reaches the floor);
       - the INNER curve: the doorway opening itself (an arch profile —
         round, flat lintel, triangular, or pointed).
  2. The two curves are joined into a planar frame. The frame is TILED so its
     internal (connecting) edges are axis-aligned: the two piers use
     horizontal rungs (constant Z), the spandrel above the arch uses vertical
     rungs (constant X), and the panel's top corners resolve as clean
     rectangular blocks — no slanted interior edges. This keeps the reveal
     surfaces coursed for later extrusions.
  3. The frame is given wall thickness by extruding it along the wall normal
     into one watertight, quad-only solid.

Conventions (see assets/arch/proc/README.md):
  - Built Z-up in Blender; engine Y-up conversion happens on export.
  - Wall panel lies in the XZ plane (x horizontal, z vertical); thickness runs
    along Y. The two faces look toward -Y (front) and +Y (back).
  - Origin at the opening center on the floor: (0, 0, 0) is the floor
    midpoint of the doorway, so instances snap onto a floor and centre on
    their opening.
  - One consistent world-unit scale (1 unit = 1 meter).
"""

import math
import random
from collections import defaultdict

import bmesh
import bpy
import numpy as np


# ---------------------------------------------------------------------------
# Arch head profiles
# ---------------------------------------------------------------------------

def _head_point(shape, a, half_w, spring_h, head_rise):
    """One point on the arch head, for parameter *a* in [0, 1] running from
    the left spring (a=0) over the top to the right spring (a=1).

    Shapes:
      "round"      -- elliptical head; a full semicircle when
                      head_rise == half_w, segmental when smaller, stilted
                      when larger.
      "flat"       -- flat lintel: the opening is a plain rectangle, so the
                      head sits at the spring line (head_rise is ignored).
      "triangular" -- gabled head rising linearly to a central peak.
      "pointed"    -- two-centred (gothic) head: two circular arcs springing
                      vertically and meeting at a point at the apex.

    Returns (x, z) in the wall plane.
    """
    if shape == "round":
        return (-half_w * math.cos(math.pi * a),
                spring_h + head_rise * math.sin(math.pi * a))

    if shape == "flat":
        return (-half_w + 2.0 * half_w * a, spring_h)

    if shape == "triangular":
        return (-half_w + 2.0 * half_w * a,
                spring_h + head_rise * (1.0 - abs(2.0 * a - 1.0)))

    if shape == "pointed":
        # Two circular arcs, each centred on the spring line. The left arc
        # passes through the left spring L = (-half_w, spring_h) and the apex
        # A = (0, spring_h + head_rise) with centre (xc, spring_h); the right
        # half mirrors it. The radius is horizontal at the springer, so the
        # arch rises vertically there, and the arcs meet at A with opposite
        # slopes -> a point.
        xc = (head_rise * head_rise - half_w * half_w) / (2.0 * half_w)
        radius = xc + half_w
        left = a <= 0.5
        s = a / 0.5 if left else (1.0 - a) / 0.5   # 0 at spring, 1 at apex
        cx = xc if left else -xc
        ax = -half_w if left else half_w
        ang_spring = math.atan2(0.0, ax - cx)
        ang_apex = math.atan2(head_rise, -cx)
        ang = ang_spring + (ang_apex - ang_spring) * s
        return (cx + radius * math.cos(ang),
                spring_h + radius * math.sin(ang))

    raise ValueError('arch_shape must be round, flat, triangular, or pointed')


# ---------------------------------------------------------------------------
# Mesh assembly
# ---------------------------------------------------------------------------

def _snap(value, grid):
    """Round *value* to the nearest positive multiple of *grid* (grid <= 0
    disables), at least one cell."""
    if grid <= 0.0:
        return value
    return max(1, round(value / grid)) * grid


def _tangent_slope(hp, a):
    """|dz/dx| of the arch head at parameter *a* via central difference.
    Returns +inf where the tangent is vertical."""
    da = 1e-4
    x0, z0 = hp(max(0.0, a - da))
    x1, z1 = hp(min(1.0, a + da))
    dx = x1 - x0
    return math.inf if abs(dx) < 1e-12 else abs((z1 - z0) / dx)


def _run_segments(hp, a0, a1, base):
    """Segment count for the arch run [a0, a1]: a STRAIGHT run (all interior
    samples lie on the chord) collapses to a single segment — flat lintels,
    triangular gables and any straight portion only get edges at their
    corners; a curved run is subdivided in proportion to its span."""
    p0, p1 = hp(a0), hp(a1)
    cx, cz = p1[0] - p0[0], p1[1] - p0[1]
    clen = math.hypot(cx, cz) or 1.0
    dev = 0.0
    for t in (0.25, 0.5, 0.75):
        px, pz = hp(a0 + (a1 - a0) * t)
        dev = max(dev, abs((px - p0[0]) * cz - (pz - p0[1]) * cx) / clen)
    if dev < 1e-4:
        return 1
    return max(1, round(base * (a1 - a0)))


def _transitions(hp):
    """Find the arch parameters (aL, aR) that bound the "crown" — the run
    where the head tangent is SHALLOWER than 45 degrees (|slope| <= 1) and
    the reveal should route vertically to the panel top. Outside it (the
    "haunches", |slope| > 1) the head is steep and routes horizontally into
    the piers, so no thin spandrel quads clump near the springers.

    Returns (aL, aR). aL == aR (== 0.5) means the whole head is steep
    (crown degenerate at the apex); aL == 0 and aR == 1 means the whole head
    is shallow (no haunch)."""
    n = 240
    shallow = [i for i in range(n + 1)
               if _tangent_slope(hp, i / n) <= 1.0 + 1e-9]
    if not shallow:
        return 0.5, 0.5

    def refine(lo, hi):                              # bisect |slope| - 1 = 0
        for _ in range(40):
            mid = 0.5 * (lo + hi)
            if ((_tangent_slope(hp, lo) - 1.0)
                    * (_tangent_slope(hp, mid) - 1.0) <= 0.0):
                hi = mid
            else:
                lo = mid
        return 0.5 * (lo + hi)

    i_lo, i_hi = shallow[0], shallow[-1]
    a_lo = 0.0 if i_lo == 0 else refine((i_lo - 1) / n, i_lo / n)
    a_hi = 1.0 if i_hi == n else refine(i_hi / n, (i_hi + 1) / n)
    return a_lo, a_hi


def _frame_faces(half_w, half_wp, sill_h, opening_h, panel_h, shape,
                 head_rise, jamb_segs, head_segs, wall_cols=1, corner_rows=1,
                 sill_cols=1, band_rows=1):
    """Tile the planar wall frame. Returns (positions, faces, splay_dirs):
    positions is a list of (x, z), faces a list of quad index tuples, and
    splay_dirs a dict {vertex_index: (dx, dz)} of unit inward directions for
    the opening's arch and jamb verts (for the splay; scaled by the splay
    amount at extrude time). Coincident verts are shared (dedup by position).

    The opening bottom sits at *sill_h*; the jambs rise *opening_h* to the
    spring line. When sill_h > 0 the opening is a closed window: a solid
    bottom band (floor -> sill) is added and the opening is capped by a sill.
    When sill_h == 0 it is a door, open at the floor.

    The inner reveal routes HORIZONTALLY into the piers where the arch head
    is steeper than 45 degrees and VERTICALLY over the shallow crown, with
    the transition at the exact 45-degree tangent point; the panel top
    corners resolve as clean rectangular blocks. *wall_cols* / *sill_cols*
    give horizontal columns (for smooth cylinder wrapping); *corner_rows* /
    *band_rows* give vertical rows in the corner blocks / bottom band.
    """
    z_sill = sill_h
    z_spring = sill_h + opening_h
    pos = []
    index = {}
    splay = {}

    def add(x, z):
        key = (round(x, 6), round(z, 6))
        if key not in index:
            index[key] = len(pos)
            pos.append((float(x), float(z)))
        return index[key]

    def add_open(x, z, dx, dz):
        i = add(x, z)
        splay[i] = (dx, dz)
        return i

    def hp(a):
        return _head_point(shape, a, half_w, z_spring, head_rise)

    def arch_in(x, z):                               # inward normal at (x,z)
        vx, vz = -x, z_spring - z
        length = math.hypot(vx, vz) or 1.0
        return (vx / length, vz / length)

    a_lo, a_hi = _transitions(hp)
    if a_hi <= a_lo:                                 # whole head steep
        a_lo = a_hi = 0.5
        n_crown = 0
        n_hl = _run_segments(hp, 0.0, 0.5, head_segs)
        n_hr = _run_segments(hp, 0.5, 1.0, head_segs)
    else:                                            # adaptive per run
        n_hl = _run_segments(hp, 0.0, a_lo, head_segs) if a_lo > 1e-9 else 0
        n_hr = (_run_segments(hp, a_hi, 1.0, head_segs)
                if a_hi < 1.0 - 1e-9 else 0)
        n_crown = _run_segments(hp, a_lo, a_hi, head_segs)

    # Opening boundary verts, each tagged with an inward splay direction.
    # The jambs splay uniformly inward (constant, horizontal), the arch
    # insets radially toward the spring-line centre, and the sill narrows
    # horizontally to match (below) — so the whole back opening is a smaller
    # copy, its bottom corners pulled together, while the sill stays flat.
    inner_l = []
    for k in range(jamb_segs + 1):
        z = z_sill + opening_h * k / jamb_segs
        inner_l.append(add_open(-half_w, z, 1.0, 0.0))
    for i in range(1, n_hl + 1):
        x, z = hp(a_lo * i / n_hl)
        inner_l.append(add_open(x, z, *arch_in(x, z)))
    inner_l_xz = [pos[v] for v in inner_l]

    crown = [inner_l[-1]]
    crown_xz = [pos[inner_l[-1]]]
    for i in range(1, n_crown + 1):
        x, z = hp(a_lo + (a_hi - a_lo) * i / n_crown)
        crown.append(add_open(x, z, *arch_in(x, z)))
        crown_xz.append((x, z))

    inner_r = [crown[-1]]
    inner_r_xz = [crown_xz[-1]]
    for i in range(1, n_hr + 1):
        x, z = hp(a_hi + (1.0 - a_hi) * i / n_hr)
        inner_r.append(add_open(x, z, *arch_in(x, z)))
        inner_r_xz.append((x, z))
    for k in range(1, jamb_segs + 1):
        z = z_spring - opening_h * k / jamb_segs
        inner_r.append(add_open(half_w, z, -1.0, 0.0))
        inner_r_xz.append((half_w, z))

    faces = []

    def pier(inner_idx, inner_xz, outer_x):
        """Pier strip: column 0 is the (splay-tagged) opening edge, out to
        outer_x in wall_cols columns; rows follow the inner boundary."""
        grid = [inner_idx]
        for c in range(1, wall_cols + 1):
            grid.append([add(x + (outer_x - x) * c / wall_cols, z)
                         for x, z in inner_xz])
        for c in range(wall_cols):
            for i in range(len(inner_xz) - 1):
                faces.append((grid[c][i], grid[c][i + 1],
                              grid[c + 1][i + 1], grid[c + 1][i]))

    def corner(px, pz, outer_x):
        grid = [[add(px + (outer_x - px) * c / wall_cols,
                     pz + (panel_h - pz) * j / corner_rows)
                 for j in range(corner_rows + 1)]
                for c in range(wall_cols + 1)]
        for c in range(wall_cols):
            for j in range(corner_rows):
                faces.append((grid[c][j], grid[c][j + 1],
                              grid[c + 1][j + 1], grid[c + 1][j]))

    pier(inner_l, inner_l_xz, -half_wp)
    pier(inner_r, inner_r_xz, half_wp)
    corner(inner_l_xz[-1][0], inner_l_xz[-1][1], -half_wp)
    corner(inner_r_xz[0][0], inner_r_xz[0][1], half_wp)

    # Crown spandrel: columns = crown samples, corner_rows rows each running
    # from its arch height up to the panel top. Using corner_rows (the same
    # count as the corner blocks) means the shared vertical edges at P_L / P_R
    # match, and the vertical connectors get horizontal splits so they follow
    # a barrel/vertical bend instead of staying straight.
    scol = []
    for i, (cx, cz) in enumerate(crown_xz):
        col = [crown[i]]
        for j in range(1, corner_rows + 1):
            col.append(add(cx, cz + (panel_h - cz) * j / corner_rows))
        scol.append(col)
    for i in range(len(crown_xz) - 1):
        for j in range(corner_rows):
            faces.append((scol[i][j], scol[i + 1][j],
                          scol[i + 1][j + 1], scol[i][j + 1]))

    # Bottom band (window): solid wall floor -> sill across the full width.
    # Its top row shares the pier bottoms; the middle span (the sill edge, the
    # opening bottom) is splay-tagged to narrow HORIZONTALLY toward centre so
    # the back-face bottom corners pull in with the jambs, while the sill face
    # stays flat. Column x-positions match the piers at +-half_w / +-half_wp.
    sill_path = []
    if z_sill > 1e-9:
        left = [-half_wp + (half_wp - half_w) * c / wall_cols
                for c in range(wall_cols + 1)]          # -half_wp .. -half_w
        mid = [-half_w + 2.0 * half_w * c / sill_cols
               for c in range(sill_cols + 1)]           # -half_w .. half_w
        right = [half_w + (half_wp - half_w) * c / wall_cols
                 for c in range(wall_cols + 1)]         # half_w .. half_wp
        cols_x = left[:-1] + mid + right[1:]
        grid = []
        for x in cols_x:
            col = []
            for j in range(band_rows + 1):
                z = z_sill * j / band_rows
                if j == band_rows and abs(x) <= half_w + 1e-9 and half_w > 0:
                    col.append(add_open(x, z, -x / half_w, 0.0))
                else:
                    col.append(add(x, z))
            grid.append(col)
        for c in range(len(cols_x) - 1):
            for j in range(band_rows):
                faces.append((grid[c][j], grid[c + 1][j],
                              grid[c + 1][j + 1], grid[c][j + 1]))
        # Sill edge (opening bottom): the band top-row verts across the
        # opening span, left corner -> right corner, for optional beveling.
        sill_path = [grid[c][band_rows] for c, x in enumerate(cols_x)
                     if abs(x) <= half_w + 1e-9]

    # The opening boundary, ordered bottom-left -> up jamb -> arch -> down
    # jamb -> bottom-right (vert indices), for beveling the reveal corner.
    opening_path = list(inner_l) + list(crown[1:]) + list(inner_r[1:])
    return pos, faces, splay, opening_path, sill_path


def _extrude_panel(bm, pos, faces, y0, y1, off=None, off_front=False):
    """Realize the planar frame at depth y0, mirror it at y1, and wall every
    boundary edge — one watertight quad solid. *off* optionally shifts the
    opening verts in the wall plane (the inward splay of the reveal); it is
    applied to the FRONT face (y0) when *off_front*, otherwise the BACK face
    (y1) — so the narrow end of the splay can sit on either wall face.
    Returns the front verts."""
    off = off or {}
    foff = off if off_front else {}
    boff = {} if off_front else off
    front = []
    for i, (x, z) in enumerate(pos):
        ox, oz = foff.get(i, (0.0, 0.0))
        front.append(bm.verts.new((x + ox, y0, z + oz)))
    back = []
    for i, (x, z) in enumerate(pos):
        ox, oz = boff.get(i, (0.0, 0.0))
        back.append(bm.verts.new((x + ox, y1, z + oz)))
    for f in faces:
        bm.faces.new([front[i] for i in f])
        bm.faces.new([back[i] for i in reversed(f)])
    und = defaultdict(int)
    for f in faces:
        for a, b in zip(f, f[1:] + f[:1]):
            und[frozenset((a, b))] += 1
    boundary = []
    for f in faces:
        for a, b in zip(f, f[1:] + f[:1]):
            if und[frozenset((a, b))] == 1:
                bm.faces.new((front[a], front[b], back[b], back[a]))
                boundary.append((a, b))
    # UV seams: seam every boundary edge on BOTH the flat front and back faces
    # (so each unwraps as its own island), then cut one cross edge per boundary
    # loop — the outer perimeter and the opening reveal — so each wall band
    # unrolls into a flat strip instead of a closed tube.
    for a, b in boundary:
        _mark_seam(front[a], front[b])
        _mark_seam(back[a], back[b])
    adj = defaultdict(list)
    for a, b in boundary:
        adj[a].append(b)
        adj[b].append(a)
    seen = set()
    for start in list(adj):
        if start in seen:
            continue
        _mark_seam(front[start], back[start])       # open this wall loop
        stack = [start]
        while stack:
            v = stack.pop()
            if v in seen:
                continue
            seen.add(v)
            stack.extend(nb for nb in adj[v] if nb not in seen)
    return front, back


def _bevel_shape(style, segments, seed=None):
    """Map a bevel style to the operator's (profile, segments): profile is the
    superellipse exponent — 0.5 is a straight chamfer / circular round, lower
    pinches concave (cavetto), higher bulges convex (ovolo). "random" picks a
    random curved profile and segment count deterministically from *seed*."""
    if style == "random":
        rng = random.Random(seed if seed is not None else 0)
        return round(rng.uniform(0.15, 0.85), 3), rng.randint(2, 6)
    seg = max(1, segments)
    if style == "chamfer":
        return 0.5, 1
    if style in ("ovolo", "round"):
        return 0.5, seg
    if style == "cavetto":
        return 0.2, seg
    if style == "ogee":
        return 0.85, max(2, seg)
    return 0.5, 1


def _pos_key(co):
    return (round(co.x, 4), round(co.y, 4), round(co.z, 4))


def _mark_seam(v0, v1):
    """Mark the edge between two verts as a UV seam (no-op if not adjacent)."""
    for e in v0.link_edges:
        if e.other_vert(v0) is v1:
            e.seam = True
            return


# Target texel density in UV units per world metre. Surfaces are unwrapped along
# the marked seams, their islands equalised, then the whole layout scaled to
# THIS density (not packed to [0, 1]) so a shared tiling material reads
# consistently across every generated surface (walls, bands, columns).
UV_SCALE = 1.0


def _normalize_island_density(obj, density):
    """Scale each UV island so its texel density equals *density* UV/metre —
    equivalent to Average Islands Scale + a global rescale, but done directly on
    the mesh (no UV-editor context needed). Islands are the face groups the
    seams cut the mesh into; each is scaled about its own UV centroid."""
    me = obj.data
    if not me.uv_layers.active:
        return
    uvl = me.uv_layers.active.data
    seam = {}
    for e in me.edges:
        seam[frozenset(e.vertices)] = e.use_seam
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
        auv = 0.0
        cx = cy = cnt = 0.0
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
        factor = density / (auv / a3) ** 0.5           # linear density -> target
        cx /= cnt
        cy /= cnt
        for f in faces:
            for li in me.polygons[f].loop_indices:
                u = uvl[li].uv
                uvl[li].uv = ((u.x - cx) * factor + cx, (u.y - cy) * factor + cy)


def _pack_islands(obj):
    """Pack the object's UV islands into the [0,1] bounds (scale-to-fill) with
    Blender's built-in ``uv.pack_islands`` (UV-sync selection so it sees every
    island), so a sampled material box covers the object once without tiling.
    No-op headless."""
    win = bpy.context.window
    area = next((a for a in (win.screen.areas if win else [])
                 if a.type == 'VIEW_3D'), None)
    if area is None:
        return
    region = next(r for r in area.regions if r.type == 'WINDOW')
    prev_sel = list(bpy.context.selected_objects)
    prev_act = bpy.context.view_layer.objects.active
    ts = bpy.context.scene.tool_settings
    prev_sync = ts.use_uv_select_sync
    for o in prev_sel:
        o.select_set(False)
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    ts.use_uv_select_sync = True
    ov = dict(window=win, area=area, region=region,
              active_object=obj, object=obj, edit_object=obj)
    bpy.ops.object.mode_set(mode='EDIT')
    with bpy.context.temp_override(**ov):
        bpy.ops.mesh.select_all(action='SELECT')
        bpy.ops.uv.pack_islands(rotate=True, margin=0.003)
    bpy.ops.object.mode_set(mode='OBJECT')
    ts.use_uv_select_sync = prev_sync
    obj.select_set(False)
    for o in prev_sel:
        o.select_set(True)
    if prev_act:
        bpy.context.view_layer.objects.active = prev_act


def _finalize_uvs(obj, density=UV_SCALE):
    """Unwrap *obj* along its marked seams, equalise per-island texel density,
    then pack the islands into the [0,1] bounds so a sampled material box covers
    the piece once (no tiling). Safe if there is no VIEW_3D context (skips)."""
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
    _pack_islands(obj)


def _masonry_course_uvs(obj):
    """Overwrite the UVs (keeping the generator's seams) so a tiling masonry
    material courses continuously over the whole piece -- through the window
    reveal from the outer face to the inner -- with the bed joints joining
    EXACTLY at every corner and the brick size identical everywhere.

    Each UV island is treated by its shape (found from its face normals):

      * Flat wall faces (normals along +/-Y): an exact per-vertex world
        projection, U = world X, V = world Z. Planar, so this stays continuous
        (adds no seam) and is exact.
      * Flat horizontal faces (normals along +/-Z, e.g. the sill top): U = world
        X, V = world Y.
      * The window reveal -- a strip extruded through the wall depth (Y), so its
        faces' normals are perpendicular to Y -- gets ONE affine transform (it is
        a single curved island, so it must stay one continuous piece): U = the
        depth run (world Y), V = arc length along the opening perimeter. Along the
        straight jambs the perimeter arc length IS the world height, so V = world
        Z there and the courses join the front wall exactly; around the arch V
        continues as arc length (the courses wrap the soffit). It is anchored on
        a jamb vertex so V = world Z at the corner. The soffit crown stretches and
        is meant to be trimmed separately.

    1 UV unit = 1 m throughout, so the brick size matches everywhere. Overwrites
    the packed [0,1] UVs -- use only for a tiling masonry material."""
    me = obj.data
    bm = bmesh.new()
    bm.from_mesh(me)
    bm.normal_update()
    uvl = bm.loops.layers.uv.active
    if uvl is None:
        bm.free()
        return
    bm.faces.ensure_lookup_table()

    # Group faces into UV islands (adjacent faces whose shared edge has matching
    # UVs) via union-find -- this respects the generator's seams.
    parent = list(range(len(bm.faces)))

    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i

    for e in bm.edges:
        if len(e.link_faces) != 2:
            continue
        f0, f1 = e.link_faces
        same = True
        for v in e.verts:
            l0 = next(l for l in f0.loops if l.vert == v)
            l1 = next(l for l in f1.loops if l.vert == v)
            d = np.array(l0[uvl].uv) - np.array(l1[uvl].uv)
            if d.dot(d) > 1e-10:
                same = False
                break
        if same:
            parent[find(f0.index)] = find(f1.index)
    islands = defaultdict(list)
    for f in bm.faces:
        islands[find(f.index)].append(f)

    Yh = np.array([0.0, 1.0, 0.0])
    Z = np.array([0.0, 0.0, 1.0])

    def uv_image(face, world_dir):
        """UV-space vector the world direction maps to (face's affine UV->world)."""
        ls = face.loops
        p0 = np.array(ls[0].vert.co)
        w = np.column_stack([np.array(ls[1].vert.co) - p0,
                             np.array(ls[2].vert.co) - p0])
        ab, _, _, _ = np.linalg.lstsq(w, world_dir, rcond=None)
        u0 = np.array(ls[0][uvl].uv)
        return ab[0] * (np.array(ls[1][uvl].uv) - u0) + \
            ab[1] * (np.array(ls[2][uvl].uv) - u0)

    for faces in islands.values():
        area = 0.0
        navg = np.zeros(3)
        for f in faces:
            a = f.calc_area()
            navg += a * np.array(f.normal)
            area += a
        if area <= 0.0:
            continue
        nlen = np.linalg.norm(navg)
        if nlen < 1e-9:
            continue
        navg /= nlen

        if abs(navg[1]) > 0.7:                    # flat wall (+/-Y): exact planar
            run = np.cross(Z, navg)
            run /= (np.linalg.norm(run) + 1e-12)
            for f in faces:
                for l in f.loops:
                    p = np.array(l.vert.co)
                    l[uvl].uv = (float(p @ run), float(p[2]))
        elif abs(navg[2]) > 0.7:                  # flat horizontal (+/-Z): sill top
            for f in faces:
                for l in f.loops:
                    p = np.array(l.vert.co)
                    l[uvl].uv = (float(p[0]), float(p[1]))
        else:                                     # reveal strip (extruded in Y)
            uvY = np.zeros(2)
            uvP = np.zeros(2)
            a2 = 0.0
            for f in faces:
                n = np.array(f.normal)
                nl = np.linalg.norm(n)
                if nl < 1e-8:
                    continue
                n /= nl
                pdir = np.cross(n, Yh)            # perimeter tangent (in-plane)
                if np.linalg.norm(pdir) < 1e-5:
                    continue
                pdir /= np.linalg.norm(pdir)
                if pdir[2] < 0.0:
                    pdir = -pdir                  # orient up the perimeter
                a = f.calc_area()
                try:
                    uvY += a * uv_image(f, Yh)
                    uvP += a * uv_image(f, pdir)
                except np.linalg.LinAlgError:
                    continue
                a2 += a
            if a2 <= 0.0:
                continue
            uvY /= a2
            uvP /= a2
            basis = np.column_stack([uvY, uvP])
            if abs(np.linalg.det(basis)) < 1e-9:
                continue
            amat = np.linalg.inv(basis)
            jamb = min(faces, key=lambda f: abs(f.normal.z))   # most vertical face
            ref = jamb.loops[0]
            p_ref = np.array(ref.vert.co)
            uv_ref = np.array(ref[uvl].uv)
            target = np.array([p_ref @ Yh, p_ref[2]])
            for f in faces:
                for l in f.loops:
                    l[uvl].uv = tuple(amat @ (np.array(l[uvl].uv) - uv_ref) + target)

    bm.to_mesh(me)
    bm.free()


def _reveal_edge_pairs(vert_lists, opening_path):
    """The reveal-to-face corner edges along the whole opening (jambs + arch)
    for each face in *vert_lists* (front and/or back copies), as position
    pairs (used to re-select the edges on the built object)."""
    pairs = []
    for verts in vert_lists:
        for i in range(len(opening_path) - 1):
            pairs.append((verts[opening_path[i]].co.copy(),
                          verts[opening_path[i + 1]].co.copy()))
    return pairs


def _collect_reveal_pairs(front, back, opening_path, sill_path, faces_opt,
                          sill_flag):
    """Reveal edge position-pairs for the chosen face(s), returned as ONE list
    per face (so each face's reveal is beveled as its own clean chamfer strip).
    Each list covers the opening (jambs + arch) plus optionally the window
    sill. Call AFTER any wrap so the recorded positions match the built
    object."""
    vlists = []
    if faces_opt in ("inside", "both"):
        vlists.append(front)
    if faces_opt in ("outside", "both"):
        vlists.append(back)
    paths = [opening_path]
    if sill_flag and sill_path:
        paths.append(sill_path)
    per_face = []
    for verts in vlists:
        pairs = []
        for p in paths:
            pairs += _reveal_edge_pairs([verts], p)
        per_face.append(pairs)
    return per_face


def _reveal_style(style, segments, seed):
    """Resolve the reveal bevel style to a classical molding name and a control
    segment count. "random" picks a curved profile and resolution
    deterministically from *seed*; curved styles are floored to enough segments
    to read as a curve; "chamfer" stays a straight single segment."""
    if style == "random":
        rng = random.Random(seed if seed is not None else 0)
        return (rng.choice(["ovolo", "cavetto", "ogee", "cyma_reversa"]),
                rng.randint(3, 6))
    if style == "chamfer":
        return "chamfer", 1
    return style, max(3, segments)


def _set_curve_profile(cp, pts):
    """Load a molding polyline (list of (x, y) from (1, 0) to (0, 1)) into a
    bevel modifier's CurveProfile. The control points MUST be given explicit
    handle types — the default 'FREE' handles have degenerate positions and
    collapse the sampled curve to a wiggle/line. 'VECTOR' makes straight spans
    between points, reproducing the analytic curve our polyline already
    samples. Points are added then sorted by :meth:`CurveProfile.update`."""
    while len(cp.points) > 2:
        cp.points.remove(cp.points[-1])
    cp.points[0].location = pts[0]
    cp.points[1].location = pts[-1]
    for (u, v) in pts[1:-1]:
        cp.points.add(u, v)
    for p in cp.points:
        p.handle_type_1 = 'VECTOR'
        p.handle_type_2 = 'VECTOR'
    cp.update()


def _reveal_chamfer_loopcut(me, ov, face_pairs, chamfer_w, push_amt, vg_index):
    """Prepare one reveal strip for the shaped bevel and weight its middle loop
    into vertex group *vg_index*:
      1. single-segment 45 chamfer of the reveal edges -> clean quad strip,
      2. a single LOOP CUT down the middle of that chamfer (the chamfer has no
         poles, so the loop follows the whole ring).
    Processing one face's strip at a time keeps every 'both faces are chamfer'
    edge an unambiguous rung, so the loop cut runs down the reveal (not around
    a jamb cross-section). The loop verts are tagged with weight 1.0 so a
    VGROUP-limited bevel modifier can later mold just that loop."""
    want = set()
    for a, b in face_pairs:
        want.add((_pos_key(a), _pos_key(b)))
        want.add((_pos_key(b), _pos_key(a)))
    bm = bmesh.from_edit_mesh(me)
    for e in bm.edges:
        e.select = False
    for f in bm.faces:
        f.select = False
    for v in bm.verts:
        v.select = False
    n = 0
    for e in bm.edges:
        if (_pos_key(e.verts[0].co), _pos_key(e.verts[1].co)) in want:
            e.select = True
            n += 1
    bmesh.update_edit_mesh(me)
    if not n:
        return
    # 1. single-segment chamfer -> flat 45 support strip, pole-free.
    with bpy.context.temp_override(**ov):
        bpy.ops.mesh.bevel(offset=chamfer_w, segments=1, affect='EDGES')
    # 2. loop cut down the middle of the chamfer band. A loop cut across the
    #    band is topologically a 1-cut subdivision of every rung (an edge shared
    #    by two chamfer quads; support edges border a non-chamfer face and are
    #    excluded). Doing it in bmesh avoids the modal loopcut operator (which
    #    can leave a pending edge-slide, and has crashed under temp-override).
    bm = bmesh.from_edit_mesh(me)
    # Verify the deform layer up front — adding a custom-data layer reallocates
    # and would invalidate BMVert references taken afterwards.
    dl = bm.verts.layers.deform.verify()
    chamfer_faces = set(f for f in bm.faces if f.select and len(f.verts) == 4)
    # Rungs are the chamfer band's cross edges. Interior rungs are shared by two
    # chamfer quads, but that misses the band's OPEN ENDS (e.g. a jamb bottoms
    # out on the floor band, a non-chamfer face) — excluding them stops the loop
    # at the springers. So for each quad, take a cross edge (one shared with a
    # chamfer neighbour) AND its opposite: the opposite of an interior rung is
    # the next interior rung, and for an end quad it is the band's end edge.
    rungs = set()
    for q in chamfer_faces:
        edges = list(q.edges)
        cross_i = None
        for i, e in enumerate(edges):
            if sum(1 for f in e.link_faces if f in chamfer_faces) == 2:
                cross_i = i
                break
        if cross_i is None:
            continue
        rungs.add(edges[cross_i])
        rungs.add(edges[(cross_i + 2) % 4])
    if not rungs:
        return
    rungs = list(rungs)
    # Identify the new loop verts from the op's OWN return value — subdivide
    # uses the .tag/flags internally, so tagging pre-existing verts to spot the
    # new ones by difference is unreliable.
    res = bmesh.ops.subdivide_edges(bm, edges=rungs, cuts=1, use_grid_fill=False)
    loop_verts = [g for g in (res.get('geom_inner', []) + res.get('geom', []))
                  if isinstance(g, bmesh.types.BMVert)]
    # 3. Push the new loop OUT along its normal. The chamfer left the band flat
    #    (coplanar), and beveling a flat edge cannot curve it — there is no
    #    dihedral to shape. Displacing the loop toward where the original sharp
    #    reveal corner was recreates that corner, now pole-free (the chamfer
    #    resolved the pole), so the following bevel rounds it into a real
    #    molding instead of a flat ripple.
    bm.normal_update()
    for v in loop_verts:
        v.co += v.normal * push_amt
        # Weight the loop into the vertex group directly — deterministic, no
        # dependence on tool weight state.
        v[dl][vg_index] = 1.0
    bmesh.update_edit_mesh(me)


def _bevel_object(obj, face_pair_lists, size, style, segments, seed=None,
                  clamp_z=None):
    """Apply the reveal-corner molding to *obj*. Each face's reveal strip is
    chamfered and loop-cut (see :func:`_reveal_chamfer_loopcut`); the resulting
    pole-free loops are then molded in one pass by a VGROUP-limited bevel
    modifier carrying a CUSTOM classical profile. The chamfer's support loops
    bound the shaped bevel, so it fills the reveal as a real molding rather
    than a ripple on a flat 45. When *clamp_z* is set (an un-beveled window
    sill), new geometry that dips below the sill plane is raised onto it."""
    if size <= 0.0 or not any(face_pair_lists):
        return
    mstyle, mseg = _reveal_style(style, segments, seed)
    pts = _molding_profile(mstyle, mseg)
    # The chamfer opens the reveal and lays clean support loops; the loop cut is
    # then pushed out ~to the old corner so the shaped bevel has a dihedral to
    # round. The shaped bevel is kept inside the supports so it stays a molding.
    chamfer_w = size
    push_amt = size * 0.4
    bevel_w = size * 0.6
    for ob in list(bpy.context.selected_objects):
        ob.select_set(False)
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    vg = obj.vertex_groups.new(name="reveal_bevel")
    vg_index = vg.index
    bpy.ops.object.mode_set(mode='EDIT')
    me = obj.data
    win = bpy.context.window
    area = next(a for a in win.screen.areas if a.type == 'VIEW_3D')
    region = next(r for r in area.regions if r.type == 'WINDOW')
    ov = dict(window=win, area=area, region=region,
              active_object=obj, object=obj, edit_object=obj)
    for face_pairs in face_pair_lists:
        if face_pairs:
            _reveal_chamfer_loopcut(me, ov, face_pairs, chamfer_w, push_amt,
                                    vg_index)
    bpy.ops.object.mode_set(mode='OBJECT')
    # Record pre-molding vertex positions so the sill clamp can target only the
    # geometry the bevel newly creates/moves.
    before = (set(_pos_key(v.co) for v in obj.data.vertices)
              if clamp_z is not None else None)
    mod = obj.modifiers.new("reveal_bevel", 'BEVEL')
    mod.limit_method = 'VGROUP'
    mod.vertex_group = "reveal_bevel"
    mod.affect = 'EDGES'
    mod.offset_type = 'OFFSET'
    mod.width = bevel_w
    mod.profile_type = 'CUSTOM'
    mod.segments = max(2, len(pts) - 1)
    mod.mark_seam = True            # seam the moulding's edges (topology change)
    _set_curve_profile(mod.custom_profile, pts)
    bpy.ops.object.modifier_apply(modifier=mod.name)
    stale = obj.vertex_groups.get("reveal_bevel")
    if stale is not None:
        obj.vertex_groups.remove(stale)
    if clamp_z is not None:
        for v in obj.data.vertices:
            if (_pos_key(v.co) not in before
                    and v.co.z < clamp_z - 1e-5):
                v.co.z = clamp_z


def _smooth_mesh(obj, angle):
    """Re-apply angle-based smoothing to a mesh object (after an operator that
    added faces, e.g. the reveal bevel)."""
    if angle <= 0.0:
        return
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    thr = math.radians(angle)
    for f in bm.faces:
        f.smooth = True
    for e in bm.edges:
        if len(e.link_faces) == 2:
            e.smooth = e.calc_face_angle() <= thr
    bm.to_mesh(obj.data)
    bm.free()


def _warp_cylinder(bm, tower_radius):
    """Bend the flat panel (built in XZ, thickness along Y) around a vertical
    cylinder of *tower_radius*. Horizontal position x becomes arc angle and
    depth y becomes cylinder radius, so the panel curves convex toward +Y
    with the tower axis behind it; the opening centre on the reference
    surface stays at the origin."""
    for v in bm.verts:
        x, y, z = v.co
        theta = x / tower_radius
        r = tower_radius + y
        v.co = (r * math.sin(theta), r * math.cos(theta) - tower_radius, z)


def _clip_plane(bm, z_plane, keep_above):
    """Cut the mesh with the horizontal plane z = *z_plane*, discard the side
    not kept (below when keep_above, above otherwise), and cap the cut so the
    result stays watertight — used to level a barrel-curved wall's tilted top
    or bottom against a floor / ceiling plane."""
    geom = list(bm.verts) + list(bm.edges) + list(bm.faces)
    res = bmesh.ops.bisect_plane(bm, geom=geom, dist=1e-7,
                                 plane_co=(0.0, 0.0, z_plane),
                                 plane_no=(0.0, 0.0, 1.0),
                                 clear_inner=keep_above,
                                 clear_outer=not keep_above)
    cut = [e for e in res["geom_cut"] if isinstance(e, bmesh.types.BMEdge)]
    if cut:
        bmesh.ops.holes_fill(bm, edges=cut, sides=0)


def _warp_barrel(bm, radius, axis_height, concave=False, flat_below=False):
    """Bend the flat panel (built in XZ, thickness along Y) around a
    HORIZONTAL cylinder whose axis runs along X — a barrel vault / tunnel.
    VERTICAL position z becomes arc angle and depth y becomes cylinder
    radius, so the panel curves up-and-over in the YZ plane. The wall is
    tangent (flat) at *axis_height*; *concave* puts the axis in front (+Y)
    for a tunnel/vault interior, otherwise behind (-Y) for the exterior.
    Points at z = axis_height keep their x, y, z. With *flat_below* the wall
    stays flat (vertical) below axis_height and only the part above it bends
    — a vault springing from a straight wall (the arc is vertical-tangent at
    axis_height, so the join is smooth)."""
    for v in bm.verts:
        x, y, z = v.co
        if flat_below and z < axis_height:
            continue
        theta = (z - axis_height) / radius
        if concave:
            r = radius - y
            v.co = (x, radius - r * math.cos(theta),
                    axis_height + r * math.sin(theta))
        else:
            r = radius + y
            v.co = (x, r * math.cos(theta) - radius,
                    axis_height + r * math.sin(theta))


def build_arched_doorway(
    name="arched_doorway",
    opening_width=1.2,
    opening_height=1.8,
    arch_shape="round",
    head_rise=0.6,
    panel_width=2.4,
    panel_height=3.0,
    wall_thickness=0.4,
    sill_height=0.0,
    splay=0.0,
    wide_side="outer",
    jamb_segments=1,
    head_segments=12,
    wall_columns=1,
    corner_rows=1,
    sill_columns=1,
    band_rows=1,
    reveal_bevel=0.0,
    reveal_bevel_style="chamfer",
    reveal_bevel_segments=1,
    reveal_bevel_faces="both",
    reveal_bevel_seed=None,
    reveal_bevel_sill=True,
    smooth_angle=35.0,
    grid_size=(0.25, 0.25, 0.25),
    snap_panel=False,
    masonry_uv=False,
    collection=None,
):
    """Build an arched-doorway (or window) mesh object and link it in.

    Opening (the doorway / window hole):
      opening_width  -- clearance width of the opening (> 0).
      opening_height -- height of the jambs, from the opening bottom up to
                        the spring line (> 0).
      arch_shape     -- head profile: "round", "flat", "triangular",
                        "pointed" (see _head_point).
      head_rise      -- height of the arch head above the spring line
                        (ignored for "flat"). With "round",
                        head_rise == opening_width/2 gives a full semicircle.
      sill_height    -- height of the opening bottom above the floor. 0 is a
                        door (open at the floor); > 0 closes the bottom with
                        a sill and lifts the opening up the wall (a window).
      splay          -- Romanesque inward splay: the opening is inset by
                        this much on one face along the arch, jambs and sill
                        corners, so the reveal funnels. 0 = straight through.
      wide_side      -- which face carries the WIDE end of the splay:
                        "outer" (the +Y / exterior face — a regular window,
                        wide outside, narrow inside) or "inner" (the -Y /
                        interior face — an arrow slit, narrow outside, wide
                        inside).

    Wall panel (the surrounding wall):
      panel_width    -- full width of the wall panel (> opening_width).
      panel_height   -- full height of the panel; must clear the arch apex
                        (> opening_height + head_rise).
      wall_thickness -- depth of the wall along Y (> 0).

    Resolution:
      jamb_segments  -- vertical divisions per jamb / pier column.
      head_segments  -- divisions across the arch head / spandrel.

    Grid:
      grid_size      -- (gx, gy, gz) cell size.
      snap_panel     -- snap panel_width, panel_height and wall_thickness to
                        the grid (the opening stays as given; the origin is
                        assumed to sit on a grid coordinate).

    collection -- optional bpy collection (defaults to the scene root).

    Returns the new mesh object, origin at the opening centre on the floor.
    Raises ValueError on out-of-range or inconsistent parameters.
    """
    if opening_width <= 0.0 or opening_height <= 0.0:
        raise ValueError("opening_width and opening_height must be > 0")
    if wall_thickness <= 0.0:
        raise ValueError("wall_thickness must be > 0")
    if jamb_segments < 1 or head_segments < 1:
        raise ValueError("segment counts must be >= 1")
    if head_rise < 0.0:
        raise ValueError("head_rise must be >= 0")
    if arch_shape in ("round", "triangular", "pointed") and head_rise <= 0.0:
        raise ValueError("head_rise must be > 0 for curved/peaked heads")

    gx, gy, gz = grid_size
    if snap_panel:
        panel_width = _snap(panel_width, gx)
        panel_height = _snap(panel_height, gz)
        wall_thickness = _snap(wall_thickness, gy)

    if sill_height < 0.0:
        raise ValueError("sill_height must be >= 0")
    if splay < 0.0:
        raise ValueError("splay must be >= 0")
    if wide_side not in ("outer", "inner"):
        raise ValueError('wide_side must be "outer" or "inner"')

    half_w = opening_width * 0.5
    half_wp = panel_width * 0.5
    apex = (sill_height + opening_height
            + (0.0 if arch_shape == "flat" else head_rise))
    if half_wp <= half_w:
        raise ValueError("panel_width must exceed opening_width")
    if panel_height <= apex:
        raise ValueError("panel_height must clear the arch apex "
                         "(> sill_height + opening_height + head_rise)")
    if splay >= half_w:
        raise ValueError("splay must be smaller than the opening half-width")

    pos, faces, splay_dirs, opening_path, sill_path = _frame_faces(
        half_w, half_wp, sill_height, opening_height, panel_height,
        arch_shape, head_rise, jamb_segments, head_segments, wall_columns,
        corner_rows, sill_columns, band_rows)
    off = {i: (splay * dx, splay * dz)
           for i, (dx, dz) in splay_dirs.items()} if splay > 0 else None

    bm = bmesh.new()
    front, back = _extrude_panel(
        bm, pos, faces, -wall_thickness * 0.5, wall_thickness * 0.5, off,
        off_front=(wide_side == "outer"))
    rev_pairs = _collect_reveal_pairs(
        front, back, opening_path, sill_path, reveal_bevel_faces,
        reveal_bevel_sill) if reveal_bevel > 0.0 else []
    obj = _finish(bm, name, collection, smooth_angle)
    if rev_pairs:
        _bevel_object(obj, rev_pairs, reveal_bevel, reveal_bevel_style,
                      reveal_bevel_segments, reveal_bevel_seed,
                      clamp_z=(None if sill_height > 0.0
                               and reveal_bevel_sill else sill_height))
        _smooth_mesh(obj, smooth_angle)
        _finalize_uvs(obj)          # re-unwrap: the bevel changed topology
    if masonry_uv:
        _masonry_course_uvs(obj)   # world-coursed UVs for a tiling masonry material
    return obj


# ---------------------------------------------------------------------------
# Concentric frame moldings (doorjamb, portal, archivolts)
#
# These are SEPARATE objects that follow the opening outline. They share the
# doorway's origin (opening centre on the floor) so they register with it.
# Each is a rectangular-section tube swept along the outline, offset radially
# from the opening edge and positioned in depth.
# ---------------------------------------------------------------------------

def _opening_outline(shape, half_w, sill_h, opening_h, head_rise, head_segs,
                     jamb_segs=1):
    """Ordered opening boundary as a list of (x, z) points with matching
    OUTWARD unit normals (pointing away from the opening interior) and a
    `closed` flag. The arch is sampled head_segs times; each jamb is split
    into jamb_segs segments (raise it so swept pieces bend smoothly around a
    barrel/horizontal cylinder). For a door (sill_h == 0) the outline is open
    at the floor; for a window (sill_h > 0) it is a closed loop closed by the
    sill edge."""
    z_spring = sill_h + opening_h
    closed = sill_h > 1e-9
    z_bottom = sill_h if closed else 0.0

    def hp(a):
        return _head_point(shape, a, half_w, z_spring, head_rise)

    pts = [(-half_w, z_bottom + (z_spring - z_bottom) * k / jamb_segs)
           for k in range(jamb_segs + 1)]
    for i in range(1, head_segs):
        pts.append(hp(i / head_segs))
    pts.extend([(half_w, z_spring - (z_spring - z_bottom) * k / jamb_segs)
                for k in range(jamb_segs + 1)])

    n = len(pts)
    seg = n if closed else n - 1
    cx = 0.0
    cz = 0.5 * (z_bottom + max(p[1] for p in pts))
    edge_n = []
    for i in range(seg):
        j = (i + 1) % n
        dx = pts[j][0] - pts[i][0]
        dz = pts[j][1] - pts[i][1]
        length = math.hypot(dx, dz) or 1.0
        nx, nz = -dz / length, dx / length
        mx = 0.5 * (pts[i][0] + pts[j][0]) - cx      # orient away from centre
        mz = 0.5 * (pts[i][1] + pts[j][1]) - cz
        if nx * mx + nz * mz < 0.0:
            nx, nz = -nx, -nz
        edge_n.append((nx, nz))

    normals = []
    for i in range(n):
        if closed:
            e0, e1 = edge_n[(i - 1) % n], edge_n[i % seg]
        elif i == 0:
            e0 = e1 = edge_n[0]
        elif i == n - 1:
            e0 = e1 = edge_n[-1]
        else:
            e0, e1 = edge_n[i - 1], edge_n[i]
        nx, nz = e0[0] + e1[0], e0[1] + e1[1]
        length = math.hypot(nx, nz) or 1.0
        normals.append((nx / length, nz / length))
    return pts, normals, closed


def _arch_head_path(shape, half_w, z_spring, head_rise, head_segs, jamb_ext):
    """The arch head as an ordered (x, z) path from the left springer over the
    crown to the right springer, with matching OUTWARD unit normals (radially
    away from the opening). *jamb_ext* > 0 extends the band straight down each
    jamb below the springer (the label continuing past the impost). Returns an
    open path (swept ends are capped)."""
    pts = []
    if jamb_ext > 0.0:
        pts.append((-half_w, z_spring - jamb_ext))
    for i in range(head_segs + 1):
        pts.append(_head_point(shape, i / head_segs, half_w, z_spring,
                               head_rise))
    if jamb_ext > 0.0:
        pts.append((half_w, z_spring - jamb_ext))

    n = len(pts)
    cx = 0.0
    zs = [p[1] for p in pts]
    cz = 0.5 * (min(zs) + max(zs))
    edge_n = []
    for i in range(n - 1):
        dx = pts[i + 1][0] - pts[i][0]
        dz = pts[i + 1][1] - pts[i][1]
        length = math.hypot(dx, dz) or 1.0
        nx, nz = -dz / length, dx / length
        mx = 0.5 * (pts[i][0] + pts[i + 1][0]) - cx      # orient away from centre
        mz = 0.5 * (pts[i][1] + pts[i + 1][1]) - cz
        if nx * mx + nz * mz < 0.0:
            nx, nz = -nx, -nz
        edge_n.append((nx, nz))

    normals = []
    for i in range(n):
        if i == 0:
            e0 = e1 = edge_n[0]
        elif i == n - 1:
            e0 = e1 = edge_n[-1]
        else:
            e0, e1 = edge_n[i - 1], edge_n[i]
        nx, nz = e0[0] + e1[0], e0[1] + e1[1]
        length = math.hypot(nx, nz) or 1.0
        normals.append((nx / length, nz / length))
    return pts, normals


def _resample_path(pts, normals, n_out):
    """Resample an open (x, z) path and its unit normals to *n_out* points
    spaced evenly by arc length — so ornament motifs tile at a constant pitch
    regardless of the arch's varying curvature."""
    seg_len = [math.dist(pts[i], pts[i + 1]) for i in range(len(pts) - 1)]
    total = sum(seg_len) or 1.0
    cum = [0.0]
    for s in seg_len:
        cum.append(cum[-1] + s)
    out_pts, out_norm = [], []
    for m in range(n_out):
        target = total * m / (n_out - 1)
        i = 0
        while i < len(seg_len) - 1 and cum[i + 1] < target:
            i += 1
        t = 0.0 if seg_len[i] <= 1e-12 else (target - cum[i]) / seg_len[i]
        out_pts.append((pts[i][0] + (pts[i + 1][0] - pts[i][0]) * t,
                        pts[i][1] + (pts[i + 1][1] - pts[i][1]) * t))
        nx = normals[i][0] + (normals[i + 1][0] - normals[i][0]) * t
        nz = normals[i][1] + (normals[i + 1][1] - normals[i][1]) * t
        length = math.hypot(nx, nz) or 1.0
        out_norm.append((nx / length, nz / length))
    return out_pts, out_norm


_ORNAMENT_STATIONS = {          # path stations for *count* motifs, per pattern
    "chevron": lambda c: c * 6,
    "billet": lambda c: 2 * c,
    "nailhead": lambda c: 2 * c,
    "dogtooth": lambda c: c,
}


def _ornament_band(bm, pts, normals, inner, outer, y_base, sign, base_d,
                   pattern, relief):
    """Build one label band as an explicit watertight shell whose front carries
    a sharp running ornament. *pts*/*normals* are the arc frames already
    resampled to the pattern's station count (see _ORNAMENT_STATIONS); *inner*
    /*outer* are the radial edges, *base_d* the flat band depth, *relief* the
    ornament height. The back, the two radial sides and the end caps are common
    to every pattern; the FRONT is per-motif geometry:
      "dogtooth" -- every cell an X-ridged pyramid (apex linked to its 4 cell
                    corners, so the hip edges read as an X); a continuous row,
      "nailhead" -- the same X pyramid on alternate cells, flat between (bosses),
      "billet"   -- alternate cells raised into flat-topped blocks with vertical
                    walls, flush between,
      "chevron"  -- a proud mid ridge whose radial position zig-zags cell to
                    cell (creased V's)."""
    n = len(pts)
    mid = 0.5 * (inner + outer)
    # Crown station: with an odd station count on a symmetric arch, the middle
    # station lands exactly on the apex. Motifs are assigned by distance from it
    # so the ornament is mirror-symmetric about the crown (and a flat cell sits
    # on the apex, so blocks/bosses never straddle the point asymmetrically).
    ci = (n - 1) * 0.5

    def vert(r, d, i):
        return bm.verts.new((pts[i][0] + normals[i][0] * r,
                             y_base + sign * d,
                             pts[i][1] + normals[i][1] * r))

    bi = [vert(inner, 0.0, i) for i in range(n)]
    bo = [vert(outer, 0.0, i) for i in range(n)]
    fi = [vert(inner, base_d, i) for i in range(n)]
    fo = [vert(outer, base_d, i) for i in range(n)]
    add = bm.faces.new
    for i in range(n - 1):
        add((bi[i], bo[i], bo[i + 1], bi[i + 1]))       # back
        add((bi[i], bi[i + 1], fi[i + 1], fi[i]))       # inner side
        add((bo[i], fo[i], fo[i + 1], bo[i + 1]))       # outer side
    # UV seams: cut the four longitudinal rails so the back, the two radial
    # sides and the ornamented front each unroll as their own strip. Also cut
    # every front cross edge so each raised motif (a billet block's box, a
    # pyramid) unwraps as its own flat island at uniform texel density instead
    # of being dragged out of a shared strip.
    for rail in (bi, bo, fi, fo):
        for i in range(n - 1):
            _mark_seam(rail[i], rail[i + 1])
    if pattern != "chevron":
        for c in range(n):
            _mark_seam(fi[c], fo[c])

    if pattern == "chevron":
        peak = []
        for i in range(n):
            phase = abs(i - ci) / 6.0                   # symmetric about crown
            tri = 1.0 - abs(2.0 * (phase % 1.0) - 1.0)
            peak.append(vert(inner + (outer - inner) * (0.1 + 0.8 * tri),
                             base_d + relief, i))
        for i in range(n - 1):
            add((fi[i], peak[i], peak[i + 1], fi[i + 1]))
            add((peak[i], fo[i], fo[i + 1], peak[i + 1]))
        add((bi[0], fi[0], peak[0], fo[0], bo[0]))      # pentagon end caps
        add((bi[-1], bo[-1], fo[-1], peak[-1], fi[-1]))
        return

    add((bi[0], fi[0], fo[0], bo[0]))                   # quad end caps
    add((bi[-1], bo[-1], fo[-1], fi[-1]))

    for c in range(n - 1):
        # Cell centre at index c + 0.5; floor(distance-from-crown) odd -> raised,
        # so raised cells are isolated and symmetric with a flat cell on the apex.
        raised = (pattern == "dogtooth") or (int(abs(c + 0.5 - ci)) % 2 == 1)
        if not raised:
            add((fi[c], fo[c], fo[c + 1], fi[c + 1]))   # flush cell
            continue
        if pattern == "billet":                         # flat-topped block
            ti0, to0 = vert(inner, base_d + relief, c), vert(outer, base_d + relief, c)
            ti1 = vert(inner, base_d + relief, c + 1)
            to1 = vert(outer, base_d + relief, c + 1)
            add((ti0, to0, to1, ti1))                   # top
            add((fi[c], fo[c], to0, ti0))               # start wall
            add((fi[c + 1], ti1, to1, fo[c + 1]))       # end wall
            add((fi[c], ti0, ti1, fi[c + 1]))           # inner wall
            add((fo[c], fo[c + 1], to1, to0))           # outer wall
            # Seam the four vertical corners so the box unfolds into a flat
            # cross (walls stay joined to the top) — a developable, undistorted
            # unwrap at uniform texel density.
            _mark_seam(fi[c], ti0)
            _mark_seam(fo[c], to0)
            _mark_seam(fi[c + 1], ti1)
            _mark_seam(fo[c + 1], to1)
        else:                                           # X-ridged pyramid
            ax = 0.5 * (pts[c][0] + pts[c + 1][0])
            az = 0.5 * (pts[c][1] + pts[c + 1][1])
            anx = normals[c][0] + normals[c + 1][0]
            anz = normals[c][1] + normals[c + 1][1]
            al = math.hypot(anx, anz) or 1.0
            apex = bm.verts.new((ax + anx / al * mid,
                                 y_base + sign * (base_d + relief),
                                 az + anz / al * mid))
            a, b, cc, d = fi[c], fo[c], fo[c + 1], fi[c + 1]
            add((apex, a, b))
            add((apex, b, cc))
            add((apex, cc, d))
            add((apex, d, a))
            _mark_seam(apex, a)                          # slit the cone to unfold


def _sweep_profile(bm, pts, normals, section, closed=False):
    """Sweep a cross-section *section* — a closed polygon of (r, y) points,
    r = radial offset from the opening edge along the outward normal, y =
    depth — along the outline, welded into one watertight tube. A *closed*
    outline wraps into a ring (no end caps); an open one is capped at both
    ends with the section polygon. Returns the created faces."""
    rails = [[bm.verts.new((pts[i][0] + normals[i][0] * r, y,
                            pts[i][1] + normals[i][1] * r))
              for i in range(len(pts))] for r, y in section]
    n = len(pts)
    span = n if closed else n - 1
    k_count = len(section)
    faces = []
    for k in range(k_count):
        k2 = (k + 1) % k_count
        for i in range(span):
            j = (i + 1) % n
            faces.append(bm.faces.new((rails[k][i], rails[k][j],
                                       rails[k2][j], rails[k2][i])))
    if not closed:
        faces.append(bm.faces.new([rails[k][0] for k in range(k_count)]))
        faces.append(bm.faces.new([rails[k][-1]
                                   for k in range(k_count - 1, -1, -1)]))
    # UV seams: one longitudinal cut along rail 0 unrolls the tube into a flat
    # strip. An open sweep also seams both end-cap rings (so the caps are their
    # own islands); a closed loop seams one section ring to open the loop.
    for i in range(span):
        _mark_seam(rails[0][i], rails[0][(i + 1) % n])
    if closed:
        for k in range(k_count):
            _mark_seam(rails[k][0], rails[(k + 1) % k_count][0])
    else:
        for k in range(k_count):
            k2 = (k + 1) % k_count
            _mark_seam(rails[k][0], rails[k2][0])
            _mark_seam(rails[k][-1], rails[k2][-1])
    return faces


def _sweep_tube(bm, pts, normals, r0, r1, y0, y1, closed=False):
    """Sweep a rectangular cross-section (radial [r0, r1] x depth [y0, y1])
    along the outline — a thin wrapper over _sweep_profile."""
    return _sweep_profile(bm, pts, normals,
                          [(r0, y0), (r1, y0), (r1, y1), (r0, y1)], closed)


# ---------------------------------------------------------------------------
# Classical molding profiles and trimmed band cross-sections
# ---------------------------------------------------------------------------

_MOLDING_STYLES = ("chamfer", "ovolo", "cavetto", "ogee", "cyma_reversa")


def _molding_profile(style, segments):
    """A classical edge-moulding profile as a polyline from (1, 0) to (0, 1)
    in normalized (across, depth) space — how a corner is cut, to be scaled
    and placed per rim. 'chamfer' is a flat bevel; 'ovolo'/'cavetto' are
    convex/concave quarter rounds; 'ogee' (cyma recta) and 'cyma_reversa' are
    S-curves."""
    n = max(1, segments)
    q = math.pi / 2
    if style == "chamfer":
        return [(1.0, 0.0), (0.0, 1.0)]
    if style == "ovolo":
        return [(math.cos(i / n * q), math.sin(i / n * q)) for i in range(n + 1)]
    if style == "cavetto":
        return [(1.0 - math.sin(i / n * q), 1.0 - math.cos(i / n * q))
                for i in range(n + 1)]
    if style == "ogee":                       # cavetto then ovolo
        pts = [(1.0 - 0.5 * math.sin(i / n * q), 0.5 - 0.5 * math.cos(i / n * q))
               for i in range(n + 1)]
        pts += [(0.5 * math.cos(i / n * q), 0.5 + 0.5 * math.sin(i / n * q))
                for i in range(1, n + 1)]
        return pts
    if style == "cyma_reversa":               # ovolo then cavetto
        pts = [(0.5 + 0.5 * math.cos(i / n * q), 0.5 * math.sin(i / n * q))
               for i in range(n + 1)]
        pts += [(0.5 * (1.0 - math.sin(i / n * q)),
                 0.5 + 0.5 * (1.0 - math.cos(i / n * q))) for i in range(1, n + 1)]
        return pts
    return [(1.0, 0.0), (0.0, 1.0)]


def _dedup_ring(pts):
    """Drop consecutive and wrap-around duplicate points from a closed
    polygon."""
    out = []
    for p in pts:
        if not out or math.dist(out[-1], p) > 1e-7:
            out.append(p)
    if len(out) > 1 and math.dist(out[0], out[-1]) < 1e-7:
        out.pop()
    return out


def _band_section(width, depth, rim_style, rim_size, front_inset,
                  fb_style, fb_size, segments):
    """A moulded band cross-section in local (r, d): r runs 0 (opening edge)
    -> width across the wall face, d = depth proud of the face. The inner
    (r=0) and outer (r=width) front corners carry a *rim_style* moulding of
    *rim_size*; the front face between the rims is recessed by *front_inset*
    with a *fb_style* bevel of *fb_size* on each step. Returns a closed
    polygon of (r, d) points."""
    rs = 0.0 if rim_style == "none" else max(0.0, min(rim_size, 0.49 * width,
                                                      0.49 * depth))
    fb = max(0.0, min(fb_size, 0.24 * width))
    fi = max(0.0, min(front_inset, 0.9 * depth))
    pts = [(0.0, 0.0), (width, 0.0)]                    # base, at the face
    if rs > 0.0:                                        # outer rim
        pts.append((width, depth - rs))
        for u, v in _molding_profile(rim_style, segments):
            pts.append((width - rs * (1.0 - u), depth - rs * (1.0 - v)))
    else:
        pts.append((width, depth))
    r_o, r_i = width - rs, rs                           # front face limits
    if fi > 0.0 and (r_o - r_i) > 2.0 * fb + 1e-6:      # recessed front
        for u, v in _molding_profile(fb_style, segments):
            pts.append((r_o - fb * (1.0 - u), depth - fi * (1.0 - v)))
        for u, v in _molding_profile(fb_style, segments):
            pts.append((r_i + fb * u, depth - fi + fi * v))
    if rs > 0.0:                                        # inner rim
        for u, v in _molding_profile(rim_style, segments):
            pts.append((rs * u, depth - rs * v))
    else:
        pts.append((0.0, depth))
    return _dedup_ring(pts)


def _random_band_trim(seed):
    """Deterministically pick a moulding trim spec (rim + front inset) from
    the classical preset styles for the given integer *seed*."""
    rng = random.Random(seed)
    inset = 0.0 if rng.random() < 0.4 else round(rng.uniform(0.03, 0.09), 4)
    return dict(rim_style=rng.choice(_MOLDING_STYLES),
                rim_size=round(rng.uniform(0.02, 0.06), 4),
                front_inset=inset,
                fb_style=rng.choice(_MOLDING_STYLES),
                fb_size=round(rng.uniform(0.02, 0.05), 4))


def _molding_section(width, depth, y_base, sign, chamfer, chamfer_segments):
    """Build a portal / archivolt cross-section polygon of (r, y) points.

    Local profile coordinates: r runs 0 (opening edge) -> width (outward
    across the wall face); the band projects *depth* proud of the face at
    y_base, in the +Y (sign=+1) or -Y (sign=-1) direction. *chamfer* bevels
    the outer-front corner, rounded into a roll when *chamfer_segments* > 1.
    """
    def y(d):
        return y_base + sign * d

    c = max(0.0, min(chamfer, 0.999 * min(width, depth)))
    pts = [(0.0, y(0.0)), (width, y(0.0))]          # inner/outer at the face
    if c > 0.0:                                      # bevel / roll outer-front
        cr, cd = width - c, depth - c
        if chamfer_segments <= 1:
            pts += [(width, y(depth - c)), (width - c, y(depth))]
        else:
            for s in range(chamfer_segments + 1):
                ang = 0.5 * math.pi * s / chamfer_segments
                pts.append((cr + c * math.cos(ang), y(cd + c * math.sin(ang))))
    else:
        pts.append((width, y(depth)))
    pts.append((0.0, y(depth)))                     # inner-front
    return pts


def _finish(bm, name, collection, smooth_angle=0.0):
    """Recalculate normals, apply angle-based smoothing, build the object,
    link it, and return it.

    *smooth_angle* (degrees) drives configurable shading: 0 leaves the mesh
    fully faceted (flat); a positive angle shades every face smooth and marks
    only edges whose dihedral exceeds the angle as sharp — so curved arches
    and cylinder-wrapped walls read smooth while structural corners (jambs,
    panel faces, sills, block edges) stay crisp."""
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    if smooth_angle > 0.0:
        thr = math.radians(smooth_angle)
        for f in bm.faces:
            f.smooth = True
        for e in bm.edges:
            if len(e.link_faces) == 2:
                e.smooth = e.calc_face_angle() <= thr   # False -> sharp edge
    mesh = bpy.data.meshes.new(name)
    bm.to_mesh(mesh)
    bm.free()
    obj = bpy.data.objects.new(name, mesh)
    (collection or bpy.context.scene.collection).objects.link(obj)
    _finalize_uvs(obj)
    return obj


def build_doorjamb(
    name="doorjamb",
    opening_width=1.2,
    opening_height=1.8,
    arch_shape="round",
    head_rise=0.6,
    wall_thickness=0.4,
    sill_height=0.0,
    jamb_thickness=0.12,
    jamb_spacing=0.06,
    smooth_angle=35.0,
    head_segments=24,
    collection=None,
):
    """Build the inner doorjamb lining as a SEPARATE object.

    A rectangular-section frame that lines the reveal of the opening: it sits
    flush with the opening edge on its outer side and projects INTO the
    opening by *jamb_thickness*, running through the wall depth but set back
    (*jamb_spacing*) from each face so the raw reveal shows before it. Follows
    a window's closed outline (sill included) when sill_height > 0.

      opening_* / arch_shape / head_rise / sill_height -- must match the
                        doorway so the lining registers with its opening.
      wall_thickness -- the doorway's wall depth (shared).
      jamb_thickness -- how far the lining projects into the opening.
      jamb_spacing   -- recess of the lining from each wall face.
      head_segments  -- arch sampling for the lining.

    Origin at the opening centre on the floor. Returns the object.
    """
    if jamb_thickness <= 0.0:
        raise ValueError("jamb_thickness must be > 0")
    if 2.0 * jamb_spacing >= wall_thickness:
        raise ValueError("jamb_spacing too large for wall_thickness")

    half_w = opening_width * 0.5
    pts, normals, closed = _opening_outline(arch_shape, half_w, sill_height,
                                            opening_height, head_rise,
                                            head_segments)
    bm = bmesh.new()
    _sweep_tube(bm, pts, normals, -jamb_thickness, 0.0,
                -wall_thickness * 0.5 + jamb_spacing,
                wall_thickness * 0.5 - jamb_spacing, closed)
    return _finish(bm, name, collection, smooth_angle)


def _resolve_trim(chamfer, chamfer_segments, rim_style, rim_size, front_inset,
                  front_bevel_style, front_bevel_size, trim_segments,
                  trim_seed):
    """Assemble a band-trim spec from explicit params, a random seed (picks
    a classical preset), or the legacy chamfer fallback."""
    if trim_seed is not None:
        spec = _random_band_trim(trim_seed)
        spec["segments"] = trim_segments
        return spec
    if rim_style == "none" and chamfer > 0.0:
        return dict(rim_style="chamfer", rim_size=chamfer, front_inset=0.0,
                    fb_style="chamfer", fb_size=0.0, segments=chamfer_segments)
    return dict(rim_style=rim_style, rim_size=rim_size, front_inset=front_inset,
                fb_style=front_bevel_style, fb_size=front_bevel_size,
                segments=trim_segments)


def _portal_section(face, wall_thickness, width, depth, trim, section):
    """Resolve a portal cross-section to a (r, y) polygon: an explicit
    *section* of (r, d) local points if given, else the moulded band from the
    *trim* spec placed on the chosen face."""
    sign = 1.0 if face == "outer" else -1.0
    y_base = sign * wall_thickness * 0.5
    if section is not None:
        return [(r, y_base + sign * d) for r, d in section]
    rd = _band_section(width, depth, trim["rim_style"], trim["rim_size"],
                       trim["front_inset"], trim["fb_style"], trim["fb_size"],
                       trim["segments"])
    return [(r, y_base + sign * d) for r, d in rd]


def build_portal(
    name="portal",
    opening_width=1.2,
    opening_height=1.8,
    arch_shape="round",
    head_rise=0.6,
    wall_thickness=0.4,
    sill_height=0.0,
    width=0.18,
    depth=0.14,
    rim_style="none",
    rim_size=0.03,
    front_inset=0.0,
    front_bevel_style="chamfer",
    front_bevel_size=0.03,
    trim_segments=3,
    trim_seed=None,
    chamfer=0.0,
    chamfer_segments=1,
    section=None,
    face="outer",
    smooth_angle=35.0,
    head_segments=24,
    collection=None,
):
    """Build a projecting portal surround as a SEPARATE object.

    A moulded frame band that follows the opening outline on one wall face and
    projects out from it — the raised architrave around a Romanesque doorway.

    Cross-section (in the radial x depth plane of the band):
      width / depth  -- radial width across the face and projection proud.
      rim_style      -- classical moulding on BOTH front rims (inner + outer):
                        "none", "chamfer", "ovolo", "cavetto", "ogee",
                        "cyma_reversa"; rim_size sets its size.
      front_inset    -- recess the front face between the rims by this depth
                        (0 = flat front), with a front_bevel_style bevel of
                        front_bevel_size on each step — a panelled architrave.
      trim_segments  -- roundness of the moulding curves.
      trim_seed      -- if set, randomly pick a classical preset trim (rim +
                        inset + bevel) deterministically from this seed.
      section        -- optional explicit (r, d) profile overriding all trim.
      chamfer / chamfer_segments -- legacy: a plain outer-rim chamfer.
      face           -- "outer" (+Y / exterior) or "inner" (-Y / interior).

    opening_* / arch_shape / head_rise / sill_height must match the doorway.
    Origin at the opening centre on the floor.
    """
    if (width <= 0.0 or depth <= 0.0) and section is None:
        raise ValueError("width and depth must be > 0")
    if face not in ("outer", "inner"):
        raise ValueError('face must be "outer" or "inner"')

    half_w = opening_width * 0.5
    pts, normals, closed = _opening_outline(arch_shape, half_w, sill_height,
                                            opening_height, head_rise,
                                            head_segments)
    trim = _resolve_trim(chamfer, chamfer_segments, rim_style, rim_size,
                         front_inset, front_bevel_style, front_bevel_size,
                         trim_segments, trim_seed)
    sec = _portal_section(face, wall_thickness, width, depth, trim, section)
    bm = bmesh.new()
    _sweep_profile(bm, pts, normals, sec, closed)
    return _finish(bm, name, collection, smooth_angle)


def build_arch_label(
    name="arch_label",
    opening_width=1.2,
    opening_height=1.8,
    arch_shape="round",
    head_rise=0.6,
    sill_height=0.0,
    wall_thickness=0.4,
    inner_radius=0.05,
    width=0.2,
    depth=0.12,
    rim_style="ovolo",
    rim_size=0.04,
    front_inset=0.0,
    front_bevel_style="chamfer",
    front_bevel_size=0.03,
    trim_segments=3,
    trim_seed=None,
    jamb_extension=0.0,
    pattern=None,
    pattern_width=None,
    pattern_count=22,
    pattern_relief=0.03,
    pattern_stations=8,
    face="outer",
    smooth_angle=35.0,
    head_segments=24,
    collection=None,
):
    """Build a label mould / hood mould as a SEPARATE object.

    A moulded band that follows the ARCH HEAD (springer to springer) on one
    wall face, sitting concentrically OUTSIDE the opening — the dripstone /
    archivolt order framing a Romanesque arch. It reuses the opening's arch
    geometry so it registers exactly with a matching doorway.

    Placement:
      inner_radius   -- gap from the opening edge to the label's inner edge
                        (stack several bands by increasing this to build
                        concentric archivolt orders).
      jamb_extension -- continue the band straight down each jamb below the
                        springer by this much (0 = stop at the springer).
      face           -- "outer" (+Y exterior) or "inner" (-Y).

    Running ornament (when *pattern* is set the band carries a repeated carved
    motif instead of the plain moulding):
      pattern         -- "chevron" (zigzag), "billet" (alternating blocks),
                         "nailhead" (pyramid bosses), "dogtooth" (pointed
                         pyramids receding to the face between).
      pattern_count   -- number of motifs along the arch head.
      pattern_relief  -- how far the ornament stands proud of the band front.
      pattern_stations-- path samples per motif (motif resolution).

    Cross-section (width across the face x depth proud), rim/inset trim, and
    trim_seed all match build_portal. opening_* / arch_shape / head_rise /
    sill_height must match the doorway. Origin at the opening centre on the
    floor.
    """
    if width <= 0.0 or depth <= 0.0:
        raise ValueError("width and depth must be > 0")
    if face not in ("outer", "inner"):
        raise ValueError('face must be "outer" or "inner"')
    half_w = opening_width * 0.5
    z_spring = sill_height + opening_height
    pts, normals = _arch_head_path(arch_shape, half_w, z_spring, head_rise,
                                   head_segments, jamb_extension)
    sign = 1.0 if face == "outer" else -1.0
    y_base = sign * wall_thickness * 0.5
    bm = bmesh.new()
    if pattern:
        if pattern not in _ORNAMENT_STATIONS:
            raise ValueError('pattern must be chevron/billet/nailhead/'
                             'dogtooth or None')
        # Resample the arch evenly by arc length so motifs tile at a constant
        # pitch, then build the sharp per-motif ornament band.
        n_st = max(3, _ORNAMENT_STATIONS[pattern](int(pattern_count)) + 1)
        if n_st % 2 == 0:            # odd -> a station lands on the crown
            n_st += 1
        rpts, rnorm = _resample_path(pts, normals, n_st)
        # Ornament is TRIM: default it to a third of the band width unless an
        # explicit pattern_width is given.
        pw = pattern_width if pattern_width is not None else width * 0.33
        _ornament_band(bm, rpts, rnorm, inner_radius, inner_radius + pw,
                       y_base, sign, depth, pattern, pattern_relief)
    else:
        trim = _resolve_trim(0.0, 1, rim_style, rim_size, front_inset,
                             front_bevel_style, front_bevel_size, trim_segments,
                             trim_seed)
        rd = _band_section(width, depth, trim["rim_style"], trim["rim_size"],
                           trim["front_inset"], trim["fb_style"],
                           trim["fb_size"], trim["segments"])
        sec = [(inner_radius + r, y_base + sign * d) for r, d in rd]
        _sweep_profile(bm, pts, normals, sec, closed=False)
    return _finish(bm, name, collection, smooth_angle)


# A wide PLAIN flat order carries the archivolt's radial mass, framed by thin
# roll mouldings; the carved patterns are narrow trim (roughly a third to a
# fifth of the plain order's width).
_DEFAULT_ARCHIVOLT = [
    {"roll": "roll", "width": 0.05, "depth": 0.09},
    {"pattern": "billet", "width": 0.055, "depth": 0.04, "relief": 0.03,
     "count": 22},
    {"roll": "roll", "width": 0.045, "depth": 0.14},
    {"plain": True, "width": 0.2, "depth": 0.15, "rim": "ovolo",
     "rim_size": 0.035},                                     # dominant flat band
    {"roll": "roll", "width": 0.045, "depth": 0.14},
    {"pattern": "chevron", "width": 0.06, "depth": 0.045, "relief": 0.035,
     "count": 18},
    {"roll": "roll", "width": 0.06, "depth": 0.09},
]


def _roll_section(width, depth, y_base, sign):
    """A plain roll (bull-nose) moulding cross-section: the front is rounded
    over, a half-round roll separating / framing the carved orders."""
    return _molding_section(width, depth, y_base, sign,
                            min(width * 0.5, depth), 6)


def build_archivolt(
    name="archivolt",
    opening_width=1.3,
    opening_height=1.8,
    arch_shape="round",
    head_rise=0.65,
    sill_height=0.0,
    wall_thickness=0.5,
    inner_radius=0.03,
    orders=None,
    jamb_extension=0.0,
    face="outer",
    smooth_angle=35.0,
    head_segments=28,
    collection=None,
):
    """Build a full archivolt assembly as ONE object: a stack of concentric
    orders following the arch head, contiguous so they read as one framed
    surround rather than floating rings.

    *orders* is a list (innermost first) of dicts, auto-placed at increasing
    radii from *inner_radius* (each order's width sets its radial band, so radii
    AND widths vary down the stack):
      ornament order -- {"pattern": chevron/billet/nailhead/dogtooth, "width":,
                         "depth":, "relief":, "count":}
      roll order     -- {"roll": "roll", "width":, "depth":} : a plain
                         bull-nose roll moulding — the framework that separates
                         and ties the carved bands together (put one first and
                         last to frame the assembly, and between carved bands).
    Defaults to a chevron+billet archivolt framed and separated by rolls.
    opening_* / arch_shape / head_rise / sill_height must match the doorway.
    """
    if face not in ("outer", "inner"):
        raise ValueError('face must be "outer" or "inner"')
    if orders is None:
        orders = _DEFAULT_ARCHIVOLT
    half_w = opening_width * 0.5
    z_spring = sill_height + opening_height
    pts, normals = _arch_head_path(arch_shape, half_w, z_spring, head_rise,
                                   head_segments, jamb_extension)
    sign = 1.0 if face == "outer" else -1.0
    y_base = sign * wall_thickness * 0.5
    bm = bmesh.new()
    r = inner_radius
    for od in orders:
        w = od["width"]
        if "pattern" in od:
            pat = od["pattern"]
            if pat not in _ORNAMENT_STATIONS:
                raise ValueError("bad archivolt pattern %r" % pat)
            n_st = max(3, _ORNAMENT_STATIONS[pat](int(od.get("count", 16))) + 1)
            if n_st % 2 == 0:
                n_st += 1
            rp, rn = _resample_path(pts, normals, n_st)
            _ornament_band(bm, rp, rn, r, r + w, y_base, sign,
                           od.get("depth", 0.045), pat, od.get("relief", 0.06))
        elif od.get("plain"):
            # Flat fascia framed by rim mouldings, flat in between.
            rim = od.get("rim", "ovolo")
            rs = od.get("rim_size", min(w * 0.2, 0.04))
            rd = _band_section(w, od.get("depth", 0.14), rim, rs, 0.0,
                               "chamfer", 0.0, od.get("segments", 4))
            _sweep_profile(bm, pts, normals,
                           [(r + rr, y_base + sign * dd) for rr, dd in rd],
                           closed=False)
        else:
            sec = _roll_section(w, od.get("depth", 0.12), y_base, sign)
            _sweep_profile(bm, pts, normals,
                           [(r + rr, yy) for rr, yy in sec], closed=False)
        r += w
    return _finish(bm, name, collection, smooth_angle)


def embed_jamb_columns(
    col_obj,
    opening_width=1.3,
    opening_height=1.8,
    sill_height=0.0,
    wall_thickness=0.5,
    inset=0.12,
    embed=None,
    project=0.35,
    flatten=1.0,
    height=None,
    face="outer",
    name="jamb_column",
    smooth_angle=30.0,
    collection=None,
):
    """Embed an existing column as an engaged nook shaft on each jamb of an
    opening. *col_obj* is any column mesh (built vertically, base at z ~ 0);
    it is duplicated to the left and right jamb, scaled so its height spans
    floor to springer (or *height*), optionally flattened in depth, sunk into
    the wall face, and then BISECTED at the back wall plane so the part that
    would poke through the far side is cut away and capped — the shaft embeds
    cleanly, flush at the back.

      inset   -- radial offset of the shaft axis outside the opening edge.
      flatten -- scale the shaft depth (into-wall Y) — < 1 makes a flatter,
                 more forward-facing relief shaft instead of a full round.
      embed   -- how far the axis sits behind the wall face. None (default)
                 auto-embeds so only *project* of the shaft's (flattened) depth
                 stands proud — i.e. it always sits at LEAST halfway into the
                 wall rather than reading as a free-standing column.
      project -- proud fraction of the shaft depth when embed is auto.
      face    -- which wall face (+Y "outer" / -Y "inner") the shafts sit on.

    Returns the two created objects (left, right)."""
    if face not in ("outer", "inner"):
        raise ValueError('face must be "outer" or "inner"')
    half_w = opening_width * 0.5
    z_spring = sill_height + opening_height
    src = col_obj.data
    zs = [v.co.z for v in src.vertices]
    z0 = min(zs)
    native_h = (max(zs) - z0) or 1.0
    scale = (height if height else z_spring) / native_h
    sign = 1.0 if face == "outer" else -1.0
    # Half the shaft's depth once scaled+flattened; used to auto-embed it so at
    # most `project` of it projects past the wall face (never free-standing).
    half_depth = 0.5 * (max(v.co.y for v in src.vertices)
                        - min(v.co.y for v in src.vertices)) * scale * flatten
    if embed is None:
        # proud amount = project * full depth (2*half_depth); embed the rest.
        embed = max(0.0, half_depth * (1.0 - 2.0 * project))
    y_axis = sign * wall_thickness * 0.5 - sign * embed
    back = -sign * wall_thickness * 0.5
    coll = collection or bpy.context.scene.collection
    objs = []
    for tag, sx in (("l", -1.0), ("r", 1.0)):
        bm = bmesh.new()
        bm.from_mesh(src)
        for v in bm.verts:
            ox, oy, oz = v.co.x, v.co.y, v.co.z
            v.co.x = ox * scale + sx * (half_w + inset)
            v.co.y = oy * scale * flatten + y_axis
            v.co.z = (oz - z0) * scale + sill_height
        geom = list(bm.verts) + list(bm.edges) + list(bm.faces)
        res = bmesh.ops.bisect_plane(bm, geom=geom, dist=1e-6,
                                     plane_co=(0.0, back, 0.0),
                                     plane_no=(0.0, sign, 0.0),
                                     clear_inner=True)
        cut = [e for e in res.get("geom_cut", [])
               if isinstance(e, bmesh.types.BMEdge)]
        if cut:
            bmesh.ops.holes_fill(bm, edges=cut)          # cap the clipped back
        objs.append(_finish(bm, "%s_%s" % (name, tag), coll, smooth_angle))
    return objs


def build_tower_portal(
    name="tower_portal",
    tower_radius=3.0,
    tessellation_density=6.0,
    opening_width=1.2,
    opening_height=1.8,
    arch_shape="round",
    head_rise=0.6,
    wall_thickness=0.4,
    sill_height=0.0,
    width=0.18,
    depth=0.14,
    rim_style="none",
    rim_size=0.03,
    front_inset=0.0,
    front_bevel_style="chamfer",
    front_bevel_size=0.03,
    trim_segments=3,
    trim_seed=None,
    chamfer=0.0,
    chamfer_segments=1,
    section=None,
    face="outer",
    smooth_angle=35.0,
    collection=None,
):
    """Projecting portal for a tower doorway — the surround wrapped around the
    same cylinder so it registers with build_tower_doorway. See build_portal
    for the cross-section / trim parameters and build_tower_doorway for the
    wrapping."""
    if (width <= 0.0 or depth <= 0.0) and section is None:
        raise ValueError("width and depth must be > 0")
    if face not in ("outer", "inner"):
        raise ValueError('face must be "outer" or "inner"')
    if tower_radius <= 0.0 or tessellation_density <= 0.0:
        raise ValueError("tower_radius and tessellation_density must be > 0")

    half_w = opening_width * 0.5
    head_segs = max(6, math.ceil(opening_width * tessellation_density))
    pts, normals, closed = _opening_outline(arch_shape, half_w, sill_height,
                                            opening_height, head_rise,
                                            head_segs)
    trim = _resolve_trim(chamfer, chamfer_segments, rim_style, rim_size,
                         front_inset, front_bevel_style, front_bevel_size,
                         trim_segments, trim_seed)
    sec = _portal_section(face, wall_thickness, width, depth, trim, section)
    bm = bmesh.new()
    _sweep_profile(bm, pts, normals, sec, closed)
    _warp_cylinder(bm, tower_radius)
    return _finish(bm, name, collection, smooth_angle)


# ---------------------------------------------------------------------------
# Cylinder-wrapped variants (guard-tower doors)
# ---------------------------------------------------------------------------

def _wrap_counts(tessellation_density, *widths):
    """Column counts for spans of the given widths at the requested density
    (columns per metre), each at least 1."""
    return [max(1, math.ceil(w * tessellation_density)) for w in widths]


def build_tower_doorway(
    name="tower_doorway",
    tower_radius=3.0,
    tessellation_density=6.0,
    opening_width=1.2,
    opening_height=1.8,
    arch_shape="round",
    head_rise=0.6,
    panel_width=2.4,
    panel_height=3.0,
    wall_thickness=0.4,
    sill_height=0.0,
    splay=0.0,
    wide_side="outer",
    jamb_segments=1,
    corner_rows=1,
    band_rows=1,
    reveal_bevel=0.0,
    reveal_bevel_style="chamfer",
    reveal_bevel_segments=1,
    reveal_bevel_faces="both",
    reveal_bevel_seed=None,
    reveal_bevel_sill=True,
    smooth_angle=35.0,
    collection=None,
):
    """Build an arched doorway (or window) wrapped around an upright cylinder
    — a doorway or arrow-slit window for a round guard tower. *wide_side*
    ("outer" / "inner") picks which face carries the wide end of the splay:
    "outer" = wide on the tower exterior (regular window), "inner" = wide on
    the interior (arrow slit).

    Built flat, then bent around a vertical cylinder of *tower_radius*
    (measured to the panel mid-depth). *tessellation_density* is the minimum
    number of vertical lines per metre of width, so the piers, corner blocks
    and arch carry enough columns for the wall to bend smoothly instead of
    faceting. All the opening/panel parameters match build_arched_doorway.

    Origin at the opening centre on the reference surface; the door faces +Y
    with the tower axis behind it (toward -Y). Returns the object.
    """
    if tower_radius <= 0.0:
        raise ValueError("tower_radius must be > 0")
    if tessellation_density <= 0.0:
        raise ValueError("tessellation_density must be > 0")

    if sill_height < 0.0 or splay < 0.0:
        raise ValueError("sill_height and splay must be >= 0")
    if wide_side not in ("outer", "inner"):
        raise ValueError('wide_side must be "outer" or "inner"')

    half_w = opening_width * 0.5
    half_wp = panel_width * 0.5
    apex = (sill_height + opening_height
            + (0.0 if arch_shape == "flat" else head_rise))
    if half_wp <= half_w:
        raise ValueError("panel_width must exceed opening_width")
    if panel_height <= apex:
        raise ValueError("panel_height must clear the arch apex")
    if panel_width > 2.0 * math.pi * tower_radius:
        raise ValueError("panel_width exceeds the tower circumference")
    if splay >= half_w:
        raise ValueError("splay must be smaller than the opening half-width")

    wall_cols, head_segs, sill_cols = _wrap_counts(
        tessellation_density, half_wp - half_w, opening_width, opening_width)
    pos, faces, splay_dirs, opening_path, sill_path = _frame_faces(
        half_w, half_wp, sill_height, opening_height, panel_height,
        arch_shape, head_rise, jamb_segments, max(4, head_segs), wall_cols,
        corner_rows, sill_cols, band_rows)
    off = {i: (splay * dx, splay * dz)
           for i, (dx, dz) in splay_dirs.items()} if splay > 0 else None

    bm = bmesh.new()
    front, back = _extrude_panel(
        bm, pos, faces, -wall_thickness * 0.5, wall_thickness * 0.5, off,
        off_front=(wide_side == "outer"))
    _warp_cylinder(bm, tower_radius)
    rev_pairs = _collect_reveal_pairs(
        front, back, opening_path, sill_path, reveal_bevel_faces,
        reveal_bevel_sill) if reveal_bevel > 0.0 else []
    obj = _finish(bm, name, collection, smooth_angle)
    if rev_pairs:
        _bevel_object(obj, rev_pairs, reveal_bevel, reveal_bevel_style,
                      reveal_bevel_segments, reveal_bevel_seed,
                      clamp_z=(None if sill_height > 0.0
                               and reveal_bevel_sill else sill_height))
        _smooth_mesh(obj, smooth_angle)
        _finalize_uvs(obj)          # re-unwrap: the bevel changed topology
    return obj


def build_tower_doorjamb(
    name="tower_doorjamb",
    tower_radius=3.0,
    tessellation_density=6.0,
    opening_width=1.2,
    opening_height=1.8,
    arch_shape="round",
    head_rise=0.6,
    wall_thickness=0.4,
    sill_height=0.0,
    jamb_thickness=0.12,
    jamb_spacing=0.06,
    smooth_angle=35.0,
    collection=None,
):
    """Build the doorjamb lining for a tower doorway — the doorjamb wrapped
    around the same cylinder so it registers with build_tower_doorway. See
    build_doorjamb for the lining parameters and build_tower_doorway for the
    wrapping."""
    if jamb_thickness <= 0.0:
        raise ValueError("jamb_thickness must be > 0")
    if 2.0 * jamb_spacing >= wall_thickness:
        raise ValueError("jamb_spacing too large for wall_thickness")
    if tower_radius <= 0.0 or tessellation_density <= 0.0:
        raise ValueError("tower_radius and tessellation_density must be > 0")

    half_w = opening_width * 0.5
    head_segs = max(6, math.ceil(opening_width * tessellation_density))
    pts, normals, closed = _opening_outline(arch_shape, half_w, sill_height,
                                            opening_height, head_rise,
                                            head_segs)
    bm = bmesh.new()
    _sweep_tube(bm, pts, normals, -jamb_thickness, 0.0,
                -wall_thickness * 0.5 + jamb_spacing,
                wall_thickness * 0.5 - jamb_spacing, closed)
    _warp_cylinder(bm, tower_radius)
    return _finish(bm, name, collection, smooth_angle)


# ---------------------------------------------------------------------------
# Barrel-wrapped variants (opening in a tunnel wall / barrel-vault surface)
#
# The wall bends around a HORIZONTAL axis (along X), so it curves up-and-over
# in the vertical plane. Tessellation density drives VERTICAL subdivision
# (jambs, corner blocks, sill band, arch) since the curvature is now vertical;
# the horizontal direction is straight (one column). Origin at the opening
# centre; the wall is tangent at *axis_height* and curves from there.
# ---------------------------------------------------------------------------

def build_barrel_doorway(
    name="barrel_doorway",
    barrel_radius=3.0,
    axis_height=None,
    concave=False,
    tessellation_density=6.0,
    opening_width=1.2,
    opening_height=1.8,
    arch_shape="round",
    head_rise=0.6,
    panel_width=2.4,
    panel_height=3.0,
    wall_thickness=0.4,
    sill_height=0.0,
    splay=0.0,
    wide_side="outer",
    wall_columns=1,
    flat_below=False,
    bend_interior_only=False,
    clip_bottom=False,
    clip_top=False,
    floor_z=None,
    ceiling_z=None,
    reveal_bevel=0.0,
    reveal_bevel_style="chamfer",
    reveal_bevel_segments=1,
    reveal_bevel_faces="both",
    reveal_bevel_seed=None,
    reveal_bevel_sill=True,
    smooth_angle=35.0,
    collection=None,
):
    """Build an arched doorway (or window) wrapped around a HORIZONTAL
    cylinder — an opening in a tunnel wall or the upper part of a barrel
    vault. Built flat, then bent up-and-over around a horizontal axis of
    *barrel_radius* (running along X). *axis_height* is the z where the wall
    is tangent/flat (default: panel mid-height); *concave* bends it toward a
    tunnel/vault interior. *tessellation_density* is the minimum vertical
    lines per metre — it drives the jamb/corner/sill/arch subdivision so the
    wall bends smoothly. Opening/panel parameters match build_arched_doorway.

    *flat_below* keeps the wall straight below *axis_height* and bends only
    the part above it (a vault springing from a vertical wall). With
    *bend_interior_only* only the interior face (the one toward the cylinder)
    follows the curve; the exterior face is flattened to a vertical plane
    pushed out to the wall's outermost extent so it never intersects the
    curve — a flat-backed wall with a vaulted inner surface.

    The bend tilts the wall's top and bottom faces (their front/back edges end
    at different heights). *clip_bottom* / *clip_top* level them by cutting
    against a horizontal plane: *floor_z* / *ceiling_z* set the plane, or
    default to the HIGHER edge of the bottom and the LOWER edge of the top so
    no material dips below the floor or pokes above the ceiling. Returns the
    object."""
    if barrel_radius <= 0.0 or tessellation_density <= 0.0:
        raise ValueError("barrel_radius and tessellation_density must be > 0")
    if sill_height < 0.0 or splay < 0.0:
        raise ValueError("sill_height and splay must be >= 0")
    if wide_side not in ("outer", "inner"):
        raise ValueError('wide_side must be "outer" or "inner"')

    half_w = opening_width * 0.5
    half_wp = panel_width * 0.5
    apex = (sill_height + opening_height
            + (0.0 if arch_shape == "flat" else head_rise))
    if half_wp <= half_w:
        raise ValueError("panel_width must exceed opening_width")
    if panel_height <= apex:
        raise ValueError("panel_height must clear the arch apex")
    if panel_height > 2.0 * math.pi * barrel_radius:
        raise ValueError("panel_height exceeds the barrel circumference")
    if splay >= half_w:
        raise ValueError("splay must be smaller than the opening half-width")
    if axis_height is None:
        axis_height = panel_height * 0.5

    d = tessellation_density
    jamb_segs = max(1, math.ceil(opening_height * d))
    corner_rows = max(1, math.ceil((panel_height - apex) * d))
    band_rows = max(1, math.ceil(sill_height * d)) if sill_height > 0 else 1
    head_segs = max(4, math.ceil(opening_width * d))
    sill_cols = max(1, math.ceil(opening_width * d))

    pos, faces, splay_dirs, opening_path, sill_path = _frame_faces(
        half_w, half_wp, sill_height, opening_height, panel_height,
        arch_shape, head_rise, jamb_segs, head_segs, wall_columns,
        corner_rows, sill_cols, band_rows)
    off = {i: (splay * dx, splay * dz)
           for i, (dx, dz) in splay_dirs.items()} if splay > 0 else None

    bm = bmesh.new()
    front, back = _extrude_panel(
        bm, pos, faces, -wall_thickness * 0.5, wall_thickness * 0.5, off,
        off_front=(wide_side == "outer"))
    # Track the panel's bottom (z=0) and top (z=panel_height) verts before the
    # bend so the default clip planes can follow their tilted edges.
    bottom_v = [v for v in bm.verts if v.co.z < 1e-6]
    top_v = [v for v in bm.verts if v.co.z > panel_height - 1e-6]
    _warp_barrel(bm, barrel_radius, axis_height, concave, flat_below)
    if bend_interior_only:
        # The exterior face is the one away from the axis: front (-Y) for a
        # concave bend, back (+Y) for convex. Flatten it to a vertical plane
        # at its outermost extent so the curved interior never crosses it.
        exterior = front if concave else back
        ext_y = (min(v.co.y for v in exterior) if concave
                 else max(v.co.y for v in exterior))
        for v in exterior:
            v.co.y = ext_y
    rev_pairs = _collect_reveal_pairs(
        front, back, opening_path, sill_path, reveal_bevel_faces,
        reveal_bevel_sill) if reveal_bevel > 0.0 else []
    if clip_bottom:
        z = floor_z if floor_z is not None else max(v.co.z for v in bottom_v)
        _clip_plane(bm, z, keep_above=True)
    if clip_top:
        z = ceiling_z if ceiling_z is not None else min(v.co.z for v in top_v)
        _clip_plane(bm, z, keep_above=False)
    obj = _finish(bm, name, collection, smooth_angle)
    if rev_pairs:
        _bevel_object(obj, rev_pairs, reveal_bevel, reveal_bevel_style,
                      reveal_bevel_segments, reveal_bevel_seed,
                      clamp_z=(None if sill_height > 0.0
                               and reveal_bevel_sill else sill_height))
        _smooth_mesh(obj, smooth_angle)
        _finalize_uvs(obj)          # re-unwrap: the bevel changed topology
    return obj


def build_barrel_doorjamb(
    name="barrel_doorjamb",
    barrel_radius=3.0,
    axis_height=0.0,
    concave=False,
    tessellation_density=6.0,
    opening_width=1.2,
    opening_height=1.8,
    arch_shape="round",
    head_rise=0.6,
    wall_thickness=0.4,
    sill_height=0.0,
    jamb_thickness=0.12,
    jamb_spacing=0.06,
    flat_below=False,
    clip_bottom=False,
    floor_z=None,
    smooth_angle=35.0,
    collection=None,
):
    """Doorjamb lining for a barrel_doorway — wrapped around the same
    horizontal cylinder so it registers. See build_doorjamb / build_barrel_
    doorway. *axis_height*, *flat_below*, *clip_bottom* / *floor_z* should
    match the doorway's so the pieces line up on the floor."""
    if jamb_thickness <= 0.0:
        raise ValueError("jamb_thickness must be > 0")
    if 2.0 * jamb_spacing >= wall_thickness:
        raise ValueError("jamb_spacing too large for wall_thickness")
    if barrel_radius <= 0.0 or tessellation_density <= 0.0:
        raise ValueError("barrel_radius and tessellation_density must be > 0")

    d = tessellation_density
    half_w = opening_width * 0.5
    head_segs = max(6, math.ceil(opening_width * d))
    jamb_segs = max(1, math.ceil(opening_height * d))
    pts, normals, closed = _opening_outline(arch_shape, half_w, sill_height,
                                            opening_height, head_rise,
                                            head_segs, jamb_segs)
    bm = bmesh.new()
    _sweep_tube(bm, pts, normals, -jamb_thickness, 0.0,
                -wall_thickness * 0.5 + jamb_spacing,
                wall_thickness * 0.5 - jamb_spacing, closed)
    z_floor = min(v.co.z for v in bm.verts)
    bottom_v = [v for v in bm.verts if v.co.z < z_floor + 1e-6]
    _warp_barrel(bm, barrel_radius, axis_height, concave, flat_below)
    if clip_bottom:
        z = floor_z if floor_z is not None else max(v.co.z for v in bottom_v)
        _clip_plane(bm, z, keep_above=True)
    return _finish(bm, name, collection, smooth_angle)


def build_barrel_portal(
    name="barrel_portal",
    barrel_radius=3.0,
    axis_height=0.0,
    concave=False,
    tessellation_density=6.0,
    opening_width=1.2,
    opening_height=1.8,
    arch_shape="round",
    head_rise=0.6,
    wall_thickness=0.4,
    sill_height=0.0,
    width=0.18,
    depth=0.14,
    rim_style="none",
    rim_size=0.03,
    front_inset=0.0,
    front_bevel_style="chamfer",
    front_bevel_size=0.03,
    trim_segments=3,
    trim_seed=None,
    chamfer=0.0,
    chamfer_segments=1,
    section=None,
    face="outer",
    flat_below=False,
    clip_bottom=False,
    floor_z=None,
    smooth_angle=35.0,
    collection=None,
):
    """Projecting portal for a barrel_doorway — wrapped around the same
    horizontal cylinder so it registers. See build_portal / build_barrel_
    doorway. *axis_height*, *flat_below*, *clip_bottom* / *floor_z* should
    match the doorway's."""
    if (width <= 0.0 or depth <= 0.0) and section is None:
        raise ValueError("width and depth must be > 0")
    if face not in ("outer", "inner"):
        raise ValueError('face must be "outer" or "inner"')
    if barrel_radius <= 0.0 or tessellation_density <= 0.0:
        raise ValueError("barrel_radius and tessellation_density must be > 0")

    d = tessellation_density
    half_w = opening_width * 0.5
    head_segs = max(6, math.ceil(opening_width * d))
    jamb_segs = max(1, math.ceil(opening_height * d))
    pts, normals, closed = _opening_outline(arch_shape, half_w, sill_height,
                                            opening_height, head_rise,
                                            head_segs, jamb_segs)
    trim = _resolve_trim(chamfer, chamfer_segments, rim_style, rim_size,
                         front_inset, front_bevel_style, front_bevel_size,
                         trim_segments, trim_seed)
    sec = _portal_section(face, wall_thickness, width, depth, trim, section)
    bm = bmesh.new()
    _sweep_profile(bm, pts, normals, sec, closed)
    z_floor = min(v.co.z for v in bm.verts)
    bottom_v = [v for v in bm.verts if v.co.z < z_floor + 1e-6]
    _warp_barrel(bm, barrel_radius, axis_height, concave, flat_below)
    if clip_bottom:
        z = floor_z if floor_z is not None else max(v.co.z for v in bottom_v)
        _clip_plane(bm, z, keep_above=True)
    return _finish(bm, name, collection, smooth_angle)
