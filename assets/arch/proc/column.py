"""Parametric column generator (ticket rpg-umku, epic rpg-pm1c).

A single freestanding column: optional base block + optional base flare +
shaft (with taper profile) + optional capital flare + optional capital block.

Conventions (see assets/arch/proc/README.md):
  - Built Z-up in Blender; engine Y-up conversion happens on export.
  - Origin at the base center, at floor level (Z = 0); the origin is assumed
    to sit on a grid coordinate for the snapping options.
  - One consistent world-unit scale (1 unit = 1 meter).

Topology: the column is ONE connected, watertight, quad-only surface, built
the way a box-modeler would:

  1. The shaft cross-section is a closed polycurve: a regular polygon of
     `shaft_sides` verts, optionally with each corner chamfered/beveled.
     The polycurve is padded to a multiple-of-4 vert count by splitting its
     longest edges with collinear verts (same geometry, a few extra verts),
     so any side count bridges cleanly to the rectangular caps.
  2. The polycurve is instanced as a stack of scaled rings (base flare,
     shaft taper profile, capital flare) stitched with quad bands.
  3. Each end either bridges to a rectangular block (loop of the same vert
     count, extruded block sides, all-quad grid-filled ends) or, when the
     block is disabled, is closed flat by bridging to an inscribed square
     loop that is grid-filled.
  4. Normals on the curved section are set explicitly per loop as the mesh
     is built (`smooth_radial` / `smooth_vertical`), enabling stylized
     normal schemes (e.g. cel shading) without shade-smooth/auto-smooth.
"""

import math
from collections import defaultdict

import bmesh
import bpy


# ---------------------------------------------------------------------------
# UV seams and uniform texel density (shared convention with arch.py:
# UV_SCALE UV units per world metre, unpacked so a tiling material tiles).
# ---------------------------------------------------------------------------

UV_SCALE = 1.0


def _mark_column_seams(bm, start_azim):
    """Seam a column for unwrapping: one vertical meridian down the *start_azim*
    side unrolls the surface of revolution into a strip, and the flat cap grids
    at the extreme Z are cut off along their rims."""
    zs = [v.co.z for v in bm.verts]
    zmin, zmax = min(zs), max(zs)

    def azdiff(a, b):
        d = (a - b) % (2.0 * math.pi)
        return min(d, 2.0 * math.pi - d)

    for e in bm.edges:
        v0, v1 = e.verts
        z0, z1 = v0.co.z, v1.co.z
        if (abs(z0 - zmin) < 1e-4 and abs(z1 - zmin) < 1e-4) or \
           (abs(z0 - zmax) < 1e-4 and abs(z1 - zmax) < 1e-4):
            e.seam = True                       # top / bottom cap rim
            continue
        a0 = math.atan2(v0.co.y, v0.co.x)
        a1 = math.atan2(v1.co.y, v1.co.x)
        if azdiff(a0, start_azim) < 0.08 and azdiff(a1, start_azim) < 0.08:
            e.seam = True                       # meridian cut


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
    """Unwrap along the marked seams, equalise island density, then pack the
    islands into [0,1] so a sampled material box covers the piece once."""
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


# ---------------------------------------------------------------------------
# Cross-section polycurve
# ---------------------------------------------------------------------------

def _shaft_profile(sides, corner_chamfer, corner_chamfer_segments,
                   rotation=0.0, corner_bow=1.0):
    """Build the shaft cross-section polycurve at unit circumradius.

    Returns a CCW list of (x, y) starting near the +45-degree corner. The
    polygon is rotated about its center by *rotation* (radians) before
    anything else — e.g. a 4-sided shaft reads block-aligned at 0 and
    diamond at pi/4. Each polygon corner is optionally chamfered:
    *corner_chamfer* in [0, 0.9] is the fraction of the half-edge cut back
    on each side of the corner, and *corner_chamfer_segments* >= 1 is the
    number of edges across the cut (1 = flat chamfer, more = a shaped bevel).
    *corner_bow* moulds the vertical arris by placing the arc's control point
    between the chord midpoint and the corner: 1.0 bulges convex to the corner
    (a rounded bead arris), 0.0 is a straight chamfer, negative bows concave
    (a cove/flute cut into each corner). The result is padded to a multiple-of-
    4 vert count by splitting its longest edges with collinear midpoints
    (geometry unchanged)."""
    corners = []
    for k in range(sides):
        t = math.pi * 0.25 + rotation + 2.0 * math.pi * k / sides
        corners.append((math.cos(t), math.sin(t)))

    if corner_chamfer <= 0.0:
        pts = list(corners)
    else:
        edge_len = math.dist(corners[0], corners[1])
        cut = corner_chamfer * 0.5 * edge_len
        pts = []
        for k in range(sides):
            px, py = corners[k]
            qx, qy = corners[k - 1]
            rx, ry = corners[(k + 1) % sides]
            ux, uy = (px - qx) / edge_len, (py - qy) / edge_len
            vx, vy = (rx - px) / edge_len, (ry - py) / edge_len
            ax, ay = px - ux * cut, py - uy * cut   # chamfer start
            bx, by = px + vx * cut, py + vy * cut   # chamfer end
            # Control point on the line from the chord midpoint toward the
            # corner: corner_bow 1 -> corner (convex), 0 -> midpoint (straight),
            # < 0 -> past the midpoint toward centre (concave cove).
            mx, my = (ax + bx) * 0.5, (ay + by) * 0.5
            cxp = mx + (px - mx) * corner_bow
            cyp = my + (py - my) * corner_bow
            seg = corner_chamfer_segments
            # Quadratic bezier a -> control -> b, sampled seg+1 times.
            for i in range(seg + 1):
                f = i / seg
                w0, w1, w2 = (1 - f) ** 2, 2 * f * (1 - f), f ** 2
                pts.append((w0 * ax + w1 * cxp + w2 * bx,
                            w0 * ay + w1 * cyp + w2 * by))

    # Pad to a multiple of 4 by splitting the longest edges at their midpoint.
    needed = (-len(pts)) % 4
    while needed:
        n = len(pts)
        longest = max(range(n),
                      key=lambda i: math.dist(pts[i], pts[(i + 1) % n]))
        ax, ay = pts[longest]
        bx, by = pts[(longest + 1) % n]
        pts.insert(longest + 1, ((ax + bx) * 0.5, (ay + by) * 0.5))
        needed -= 1

    # Rotate the list so vertex 0 sits nearest the +45-degree direction,
    # keeping the bridge to the axis-aligned block rings twist-free no
    # matter how the cross-section was rotated.
    def _angular_dist(p):
        d = (math.atan2(p[1], p[0]) - math.pi * 0.25) % (2.0 * math.pi)
        return min(d, 2.0 * math.pi - d)
    start = min(range(len(pts)), key=lambda i: _angular_dist(pts[i]))
    return pts[start:] + pts[:start]


def _pad_and_align(pts):
    """Pad a cross-section to a multiple-of-4 vert count (splitting the longest
    edges with collinear midpoints) and rotate the list so vertex 0 sits
    nearest the +45-degree direction — matching _shaft_profile so the bridge to
    the axis-aligned block rings stays twist-free."""
    needed = (-len(pts)) % 4
    while needed:
        n = len(pts)
        longest = max(range(n),
                      key=lambda i: math.dist(pts[i], pts[(i + 1) % n]))
        ax, ay = pts[longest]
        bx, by = pts[(longest + 1) % n]
        pts.insert(longest + 1, ((ax + bx) * 0.5, (ay + by) * 0.5))
        needed -= 1

    def _angular_dist(p):
        d = (math.atan2(p[1], p[0]) - math.pi * 0.25) % (2.0 * math.pi)
        return min(d, 2.0 * math.pi - d)
    start = min(range(len(pts)), key=lambda i: _angular_dist(pts[i]))
    return pts[start:] + pts[:start]


def _flute_section(count, style, depth, fillet_ratio, segments, rotation):
    """Fluted / reeded shaft cross-section at unit reference radius (the arris
    circle). *count* flutes each span a sector sampled *segments* times; *depth*
    is the radial cut as a fraction of the radius. Styles:
      "flute"        -- concave grooves meeting at sharp arrises (Doric),
      "flute_fillet" -- grooves separated by flat fillets (Ionic); fillet_ratio
                        is the fraction of each sector kept flat,
      "reed"         -- convex ridges cresting at the reference circle with
                        grooves between them (reeding / cabling).
    Returns (flute_pts, smooth_pts): the fluted profile and the plain reference
    circle at the SAME angles, so callers can fade flutes in/out per ring."""
    flute_pts, smooth_pts = [], []
    for k in range(count):
        for i in range(segments):
            s = i / segments                       # position across the sector
            theta = rotation + 2.0 * math.pi * (k + s) / count
            if style == "reed":                    # convex ridge, groove at edges
                r = 1.0 - depth * (1.0 - math.sin(math.pi * s))
            elif style == "flute_fillet":
                fr = fillet_ratio * 0.5
                if s < fr or s > 1.0 - fr:
                    r = 1.0                        # flat fillet band
                else:
                    ss = (s - fr) / max(1e-6, 1.0 - 2.0 * fr)
                    r = 1.0 - depth * math.sin(math.pi * ss)
            else:                                  # concave flute, sharp arris
                r = 1.0 - depth * math.sin(math.pi * s)
            ct, st = math.cos(theta), math.sin(theta)
            flute_pts.append((r * ct, r * st))
            smooth_pts.append((ct, st))
    # Pad both together with identical structure so they stay index-aligned.
    n0 = len(flute_pts)
    needed = (-n0) % 4
    while needed:
        n = len(flute_pts)
        longest = max(range(n), key=lambda i: math.dist(
            flute_pts[i], flute_pts[(i + 1) % n]))
        for arr in (flute_pts, smooth_pts):
            ax, ay = arr[longest]
            bx, by = arr[(longest + 1) % n]
            arr.insert(longest + 1, ((ax + bx) * 0.5, (ay + by) * 0.5))
        needed -= 1

    def _ad(p):
        d = (math.atan2(p[1], p[0]) - math.pi * 0.25) % (2.0 * math.pi)
        return min(d, 2.0 * math.pi - d)
    start = min(range(len(flute_pts)), key=lambda i: _ad(flute_pts[i]))
    return (flute_pts[start:] + flute_pts[:start],
            smooth_pts[start:] + smooth_pts[:start])


def _profile_normals(pts):
    """2D outward unit normals for a CCW polycurve: per-segment and
    per-vertex (bisector of the two adjacent segments)."""
    n = len(pts)
    seg = []
    for k in range(n):
        ax, ay = pts[k]
        bx, by = pts[(k + 1) % n]
        dx, dy = bx - ax, by - ay
        l = math.hypot(dx, dy) or 1.0
        seg.append((dy / l, -dx / l))  # CCW winding -> (dy, -dx) points out
    vert = []
    for k in range(n):
        sx = seg[k - 1][0] + seg[k][0]
        sy = seg[k - 1][1] + seg[k][1]
        l = math.hypot(sx, sy) or 1.0
        vert.append((sx / l, sy / l))
    return seg, vert


def _profile_inradius(pts):
    """Distance from the origin to the nearest polycurve segment (the safe
    radius for inscribing cap geometry inside the cross-section)."""
    n = len(pts)
    best = math.inf
    for k in range(n):
        ax, ay = pts[k]
        bx, by = pts[(k + 1) % n]
        dx, dy = bx - ax, by - ay
        l2 = dx * dx + dy * dy
        t = 0.0 if l2 == 0.0 else max(0.0, min(1.0, -(ax * dx + ay * dy) / l2))
        best = min(best, math.hypot(ax + dx * t, ay + dy * t))
    return best


# ---------------------------------------------------------------------------
# Ring construction and stitching
# ---------------------------------------------------------------------------

def _rect_positions(count, half_x, half_y):
    """(x, y) positions of a closed rectangle loop of exactly *count* verts
    (count % 4 == 0), spanning [-half_x, +half_x] on X and [-half_y,
    +half_y] on Y. Vertex 0 is the (+,+) corner, CCW, count/4 segments per
    side — aligned with the profile polycurve's start near +45 degrees."""
    seg = count // 4
    corners = [(half_x, half_y), (-half_x, half_y),
               (-half_x, -half_y), (half_x, -half_y)]
    pts = []
    for c in range(4):
        x0, y0 = corners[c]
        x1, y1 = corners[(c + 1) % 4]
        for s in range(seg):
            f = s / seg
            pts.append((x0 + (x1 - x0) * f, y0 + (y1 - y0) * f))
    return pts


def _rect_ring(bm, count, half_x, half_y, z):
    """Create the verts of a _rect_positions loop at height *z*."""
    return [bm.verts.new((x, y, z))
            for x, y in _rect_positions(count, half_x, half_y)]


def _stitch_band(bm, ring_a, ring_b, smooth=False):
    """Connect two aligned equal-count rings with a band of quads.
    Returns the faces in ring order (face k spans verts k..k+1)."""
    n = len(ring_a)
    faces = []
    for k in range(n):
        j = (k + 1) % n
        f = bm.faces.new((ring_a[k], ring_a[j], ring_b[j], ring_b[k]))
        f.smooth = smooth
        faces.append(f)
    return faces


def _grid_fill_cap(bm, ring, half_x, half_y, z):
    """Close the open rectangle loop *ring* with an (m x m) all-quad grid,
    m = len(ring) / 4. The grid boundary reuses the ring verts exactly, so
    the cap is topologically welded to the sides (equivalent to Blender's
    Grid Fill, but deterministic)."""
    m = len(ring) // 4

    grid = [[None] * (m + 1) for _ in range(m + 1)]
    for i in range(m + 1):
        grid[i][m] = ring[(m - i) % (4 * m)]      # top edge (y = +half_y)
        grid[i][0] = ring[2 * m + i]              # bottom edge (y = -half_y)
    for j in range(m + 1):
        grid[0][j] = ring[m + (m - j)]            # left edge (x = -half_x)
        grid[m][j] = ring[(3 * m + j) % (4 * m)]  # right edge (x = +half_x)
    for i in range(1, m):
        for j in range(1, m):
            grid[i][j] = bm.verts.new((-half_x + 2.0 * half_x * i / m,
                                       -half_y + 2.0 * half_y * j / m, z))
    for i in range(m):
        for j in range(m):
            bm.faces.new((grid[i][j], grid[i + 1][j],
                          grid[i + 1][j + 1], grid[i][j + 1]))


def _build_cushion_inset(bm, shaft_top, profile, echinus_r, square,
                         z_rim, z_blk, rounding):
    """Cushion capital with the reference quad-modeling topology, matched
    to the corrected exemplar's edge connectivity.

    Per quarter (m4 = M/4 bell verts): the shaft top extrudes outward
    through a collar to the bell rim (two flat bands), one bowl band rises
    to ring R1, and R1 feeds the shield: its middle meridians rise into
    the shield outline's bottom mids; its end meridians land on diamond
    bottoms B whose diamond quads redirect the loop at the shield's bottom
    corners — the corner verts C are the 5-poles, the diamond tops D the
    3-poles. Each block corner carries a diagonal chain c-G1-G2-G3-D1-R0
    laddered to both adjacent faces (kites at c). The whole flat side is
    then inset once: outline (top edge = the block ring itself) -> border
    quad ring -> flat shield n-gon.

    Returns (faces, ngon_faces, ring_top).
    """
    count = len(shaft_top)
    m4 = count // 4
    h_c = z_blk - z_rim
    faces = []

    def quad(v0, v1, v2, v3):
        faces.append(bm.faces.new((v0, v1, v2, v3)))

    def lerp3(a, b, t):
        return (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t,
                a[2] + (b[2] - a[2]) * t)

    ring_top = [bm.verts.new((square[k][0], square[k][1], z_blk))
                for k in range(count)]

    # --- Collar: two flat outward bands from the shaft top to the bell
    # rim (the "extrude outward" stage of the reference sequence). The
    # collar/bell directions come from the BLOCK-RING positions — evenly
    # spread per side with the corners exactly on the diagonals — not from
    # the shaft profile, whose verts may cluster along flat edges (padded
    # or rotated low-poly shafts would skew the whole bowl otherwise).
    # The collar band absorbs the shaft-shape transition.
    r_shaft = max(math.hypot(v.co.x, v.co.y) for v in shaft_top)
    r_mid = 0.5 * (r_shaft + echinus_r)
    dirs = []
    for k in range(count):
        x, y = square[k]
        l = math.hypot(x, y) or 1.0
        dirs.append((x / l, y / l))
    collar = [bm.verts.new((dx * r_mid, dy * r_mid, z_rim))
              for dx, dy in dirs]
    bell = [bm.verts.new((dx * echinus_r, dy * echinus_r, z_rim))
            for dx, dy in dirs]
    for lower, upper in ((shaft_top, collar), (collar, bell)):
        for k in range(count):
            j = (k + 1) % count
            faces.append(bm.faces.new((lower[k], lower[j],
                                       upper[j], upper[k])))

    # --- Per-face frame helpers. Face q spans block ring indices
    # base..base+m4 (corners at both ends); its plane holds one coordinate
    # fixed. u = the free coordinate, from the block-ring positions.
    def face_frame(q):
        base = q * m4
        p0 = square[base]
        p1 = square[(base + m4) % count]
        if abs(p0[1] - p1[1]) < 1e-9:
            axis, fixed = 0, p0[1]      # u = x, y fixed
        else:
            axis, fixed = 1, p0[0]
        return base, axis, fixed, p0, p1

    def mk(q_axis, fixed, u, z, pull=0.0):
        """Vert in face plane (optionally pulled inward off-plane)."""
        f = fixed * (1.0 - pull)
        if q_axis == 0:
            return bm.verts.new((u, f, z))
        return bm.verts.new((f, u, z))

    # z levels (fractions of h_c below the block ring, from the exemplar)
    z_O = z_blk - 0.65 * h_c    # outline bottom mids
    z_D = z_blk - 0.62 * h_c    # diamond tops
    z_C = z_blk - 0.55 * h_c    # outline bottom corners / G2
    z_S = z_blk - 0.28 * h_c    # outline side verts / G1
    z_R1 = z_blk - 0.75 * h_c   # bowl ring R1
    z_B = z_blk - 0.68 * h_c    # diamond bottoms / G3

    g = max(0.0, min(1.0, rounding))
    n_mid = m4 - 3              # outline bottom mid verts (O)

    per_face = []
    for q in range(4):
        base, axis, fixed, p0, p1 = face_frame(q)
        u0 = square[base + 1][axis]                # e_1 column (left end)
        u1 = square[(base + m4 - 1) % count][axis]  # e_{m4-1} column
        uc = 0.5 * (u0 + u1)

        def su(f):  # signed span position: -1 at e_1, +1 at e_{m4-1}
            return uc + f * 0.5 * (u1 - u0)

        # Outline: S (sides), C (bottom corners), D (diamond tops), O mids
        S_L = mk(axis, fixed, su(-0.90), z_S)
        S_R = mk(axis, fixed, su(0.90), z_S)
        C_L = mk(axis, fixed, su(-0.81), z_C)
        C_R = mk(axis, fixed, su(0.81), z_C)
        D_L = mk(axis, fixed, su(-0.59), z_D)
        D_R = mk(axis, fixed, su(0.59), z_D)
        O = [mk(axis, fixed, su(-0.40 + 0.80 * i / (n_mid - 1)), z_O)
             for i in range(n_mid)]

        # Bowl ring R1 side verts: under [B_L, O.., B_R] columns, blended
        # slightly toward the bell at the ends.
        M = []
        for i in range(1, m4):
            if i == 1:
                u, pull = su(-0.70), 0.14
            elif i == m4 - 1:
                u, pull = su(0.70), 0.14
            else:
                u, pull = su(-0.40 + 0.80 * (i - 2) / (n_mid - 1)), 0.02
            M.append(mk(axis, fixed, u, z_R1, pull))

        # Diamond bottoms.
        B_L = mk(axis, fixed, su(-0.74), z_B, 0.09)
        B_R = mk(axis, fixed, su(0.74), z_B, 0.09)

        per_face.append({"S_L": S_L, "S_R": S_R, "C_L": C_L, "C_R": C_R,
                         "D_L": D_L, "D_R": D_R, "O": O, "M": M,
                         "B_L": B_L, "B_R": B_R})

    # --- Corner chains: c -> G1 -> G2 -> G3 -> D1 -> bell diagonal.
    corners = []
    for q in range(4):
        base = q * m4
        cx, cy = square[base]
        bx, by = bell[base].co.x, bell[base].co.y
        # straight diagonal for harsh chamfers, hugging the corner when
        # rounded (the asymmetric zero-offset bevel).
        def dpos(t_z, t_lin):
            t = t_lin * (1.0 - g) + (1.0 - math.cos(
                t_lin * math.pi * 0.5)) * g
            return (cx + (bx - cx) * t, cy + (by - cy) * t,
                    z_blk + (t_z) * (z_rim - z_blk))
        G1 = bm.verts.new(dpos(0.28, 0.30))
        G2 = bm.verts.new(dpos(0.55, 0.55))
        G3 = bm.verts.new(dpos(0.68, 0.80))
        D1 = bm.verts.new(dpos(0.75, 0.92))
        corners.append({"G1": G1, "G2": G2, "G3": G3, "D1": D1})

    # --- Bowl band R0 (bell) -> R1 ring (D1 at corners, M along faces).
    R1 = []
    for q in range(4):
        R1.append(corners[q]["D1"])
        R1.extend(per_face[q]["M"])
    for k in range(count):
        j = (k + 1) % count
        faces.append(bm.faces.new((bell[k], bell[j], R1[j], R1[k])))

    # --- Per-face upper structure.
    ngon_faces = []
    for q in range(4):
        base = q * m4
        f = per_face[q]
        M, O = f["M"], f["O"]
        e = [ring_top[base + i] for i in range(1, m4)]

        # bowl top band: middle meridians into the outline mids
        for jj in range(n_mid - 1):
            quad(O[jj], O[jj + 1], M[jj + 2], M[jj + 1])
        # side quads into the diamond bottoms
        quad(f["B_L"], O[0], M[1], M[0])
        quad(f["B_R"], M[m4 - 2], M[m4 - 3], O[-1])
        # the redirect diamonds at the shield's bottom corners
        quad(f["C_L"], f["D_L"], O[0], f["B_L"])
        quad(O[-1], f["D_R"], f["C_R"], f["B_R"])

        # inset: outline loop -> shield n-gon (mitered in-plane offset)
        loop = ([f["C_L"], f["D_L"]] + O + [f["D_R"], f["C_R"], f["S_R"]]
                + e[::-1] + [f["S_L"]])
        base_ax, axis, fixed, _p0, _p1 = face_frame(q)
        pts = [(v.co.x if axis == 0 else v.co.y, v.co.z) for v in loop]
        n = len(pts)
        area = sum(pts[k][0] * pts[(k + 1) % n][1]
                   - pts[(k + 1) % n][0] * pts[k][1] for k in range(n))
        sgn = 1.0 if area > 0 else -1.0
        t_in = 0.09 * h_c
        ngon_vs = []
        for k in range(n):
            (ax0, az0), (ax1, az1) = pts[k - 1], pts[k]
            (ax2, az2) = pts[(k + 1) % n]
            d0x, d0z = ax1 - ax0, az1 - az0
            d1x, d1z = ax2 - ax1, az2 - az1
            l0 = math.hypot(d0x, d0z) or 1.0
            l1 = math.hypot(d1x, d1z) or 1.0
            n0x, n0z = -d0z / l0 * sgn, d0x / l0 * sgn
            n1x, n1z = -d1z / l1 * sgn, d1x / l1 * sgn
            bx_, bz_ = n0x + n1x, n0z + n1z
            bl = math.hypot(bx_, bz_) or 1.0
            bx_, bz_ = bx_ / bl, bz_ / bl
            dot = max(0.5, (1.0 + n0x * n1x + n0z * n1z) * 0.5)
            sc = min(2.0, 1.0 / math.sqrt(dot))
            u = ax1 + bx_ * t_in * sc
            z = az1 + bz_ * t_in * sc
            v = loop[k]
            if axis == 0:
                ngon_vs.append(bm.verts.new((u, v.co.y, z)))
            else:
                ngon_vs.append(bm.verts.new((v.co.x, u, z)))
        ngon_faces.append(bm.faces.new(ngon_vs))
        faces.append(ngon_faces[-1])
        for k in range(n):
            j = (k + 1) % n
            quad(loop[k], loop[j], ngon_vs[j], ngon_vs[k])

    # --- Corner ladders and kites.
    for q in range(4):
        base = q * m4
        cor = corners[q]
        fq = per_face[q]           # face on the +side of this corner
        fp = per_face[q - 1]       # previous face
        c_v = ring_top[base]
        e_q = ring_top[base + 1]
        e_p = ring_top[(base - 1) % count]
        # shoulder quads joining R1's diagonal to both diamond bottoms
        quad(cor["D1"], cor["G3"], fq["B_L"], fq["M"][0])
        quad(fp["M"][m4 - 2], fp["B_R"], cor["G3"], cor["D1"])
        # ladder rows
        quad(fq["C_L"], fq["B_L"], cor["G3"], cor["G2"])
        quad(cor["G2"], cor["G3"], fp["B_R"], fp["C_R"])
        quad(fq["S_L"], fq["C_L"], cor["G2"], cor["G1"])
        quad(cor["G1"], cor["G2"], fp["C_R"], fp["S_R"])
        # kites at the block corner
        quad(e_q, fq["S_L"], cor["G1"], c_v)
        quad(c_v, cor["G1"], fp["S_R"], e_p)

    return faces, ngon_faces, ring_top


def _ease(s, curvature):
    """Flare radius interpolant on s in [0, 1]. curvature 0 = linear cone;
    > 0 bulges outward (fuller near the wide end, ovolo-like); < 0 coves
    inward (cavetto-like). Magnitude 1 is the strongest curve."""
    if curvature >= 0.0:
        return s ** (1.0 + 3.0 * curvature)
    return 1.0 - (1.0 - s) ** (1.0 - 3.0 * curvature)


def _snap(value, grid):
    """Round *value* to the nearest multiple of *grid* (grid <= 0 disables)."""
    if grid <= 0.0:
        return value
    return round(value / grid) * grid


def _snap_size(value, grid):
    """Snap a strictly positive dimension to the grid, at least one cell."""
    if grid <= 0.0:
        return value
    return max(1, round(value / grid)) * grid


# ---------------------------------------------------------------------------
# Classical moulding vocabulary (swept as surfaces of revolution)
# ---------------------------------------------------------------------------

def _moulding_shape(kind, t):
    """Radial deviation of a classical moulding element at normalized height
    t in [0, 1], in units of the element's bulge amplitude (added on top of
    the element's linear radius baseline). Positive bows OUT (convex), negative
    bows IN (concave / hollow)."""
    q = math.pi * 0.5
    if kind == "fillet":                        # straight band / listel
        return 0.0
    if kind == "ovolo":                         # convex quarter round (out)
        return math.sin(t * q)
    if kind == "cavetto":                       # concave quarter hollow (in)
        return -(1.0 - math.cos(t * q))
    if kind in ("torus", "astragal", "bead"):   # half-round bead (out)
        return math.sin(t * math.pi)
    if kind == "scotia":                        # deep hollow (in)
        return -math.sin(t * math.pi)
    if kind == "ogee":                          # cyma recta S: hollow -> round
        return (-(1.0 - math.cos(t * math.pi)) * 0.5 if t < 0.5
                else math.sin((t - 0.5) * math.pi) * 0.5)
    if kind == "cyma":                          # cyma reversa S: round -> hollow
        return (math.sin(t * math.pi) * 0.5 if t < 0.5
                else -(1.0 - math.cos((t - 0.5) * math.pi)) * 0.5)
    return 0.0


def _moulding_rings(elements, r_start, r_end, z_start, z_end, unit,
                    bulge_unit=None):
    """Sweep a stack of classical moulding *elements* into (radius, z) rings
    from (r_start, z_start) up to (r_end, z_end), bottom to top. Each element
    is a tuple (kind, height_weight, radius_step, bulge): *height_weight* sets
    its share of the vertical span, *radius_step* its net radius change (units
    of *unit*, rescaled so the stack closes exactly on r_end), *bulge* its
    convex/concave amplitude (units of *bulge_unit*, defaulting to *unit* —
    scale it to widen/narrow every bead without changing the baseline taper).
    Returns the rings ABOVE the (r_start, z_start) anchor, which the caller
    already holds; the last lands exactly on (r_end, z_end)."""
    if bulge_unit is None:
        bulge_unit = unit
    total_h = sum(max(1e-6, e[1]) for e in elements) or 1.0
    steps = [e[2] * unit for e in elements]
    net = sum(steps)
    want = r_end - r_start
    if abs(net) > 1e-9:
        steps = [s * (want / net) for s in steps]   # keep proportions, close
    else:
        steps = [want / len(steps)] * len(steps)
    rings = []
    r, z = r_start, z_start
    for (kind, _hw, _rs, bulge), dr in zip(elements, steps):
        h = (z_end - z_start) * (_hw / total_h)
        segs = 1 if kind == "fillet" else 8
        for i in range(1, segs + 1):
            t = i / segs
            base = r + dr * t
            rings.append((base + _moulding_shape(kind, t) * bulge * bulge_unit,
                          z + h * t))
        r += dr
        z += h
    if rings:
        rings[-1] = (r_end, z_end)                   # snap the closing ring
    return rings


# Named moulding stacks. Steps are rescaled to the actual radius span, so they
# read as proportions; bulges are in units of the projection (shaft radius x
# the *_moulding_projection factor). Ordered bottom -> top.
_BASE_MOULDINGS = {
    # Tuscan/Doric: a single torus on a listel.
    "tuscan": [("fillet", 0.3, 0.0, 0.0), ("torus", 1.7, -0.6, 0.5),
               ("fillet", 0.35, -0.4, 0.0)],
    # Attic: lower torus, scotia hollow, upper torus, fillet. The scotia is a
    # shallow hollow — the neck stays close to the shaft radius, not pinched.
    "attic": [("fillet", 0.22, 0.0, 0.0), ("torus", 1.0, -0.2, 0.36),
              ("scotia", 0.95, -0.18, 0.22), ("torus", 0.85, -0.2, 0.28),
              ("fillet", 0.22, -0.1, 0.0)],
    # Dwarven: heavy angular steps, an ovolo instead of a round torus.
    "dwarven": [("fillet", 0.45, -0.3, 0.0), ("ovolo", 0.8, -0.4, 0.3),
                ("fillet", 0.5, -0.4, 0.0)],
}

# Capital moulding stacks: swept over the capital transition, shaft top
# (narrow, r_start) up to the abacus underside (wide, echinus_r). Radius steps
# are positive-leaning (the neck flares OUT to the block).
_CAPITAL_MOULDINGS = {
    # Doric: a big ovolo echinus under a fillet.
    "echinus": [("fillet", 0.25, 0.1, 0.0), ("ovolo", 1.6, 0.7, 0.45),
                ("fillet", 0.3, 0.2, 0.0)],
    # Astragal bead, cavetto neck, then the ovolo echinus.
    "astragal": [("astragal", 0.5, 0.1, 0.3), ("fillet", 0.2, 0.05, 0.0),
                 ("ovolo", 1.3, 0.7, 0.4), ("fillet", 0.25, 0.15, 0.0)],
    # Egyptian-ish concave bell (cavetto) flaring to the abacus.
    "cavetto": [("fillet", 0.2, 0.05, 0.0), ("cavetto", 1.7, 0.85, 0.35),
                ("fillet", 0.3, 0.1, 0.0)],
}

# Shaft inset (engraved horizontal band) profiles: swept over one groove; the
# radius dips inward and returns, so both ends sit flush with the shaft.
_INSET_MOULDINGS = {
    "scotia": [("scotia", 1.0, 0.0, 1.0)],            # round hollow groove
    "cavetto": [("cavetto", 0.5, 0.0, 1.0),           # V-ish concave notch
                ("ovolo", 0.5, 0.0, -1.0)],
    "astragal": [("astragal", 1.0, 0.0, -1.0)],       # raised bead band (out)
    "ogee": [("ogee", 1.0, 0.0, 1.0)],                # S-profile fillet groove
}


def _resolve_moulding(spec, table):
    """A moulding spec is either a preset name (str) or an explicit element
    list. Returns the element list, or None if *spec* is falsy."""
    if not spec:
        return None
    if isinstance(spec, str):
        if spec not in table:
            raise ValueError("unknown moulding preset %r (have %s)"
                             % (spec, ", ".join(sorted(table))))
        return table[spec]
    return list(spec)


# ---------------------------------------------------------------------------
# Generator
# ---------------------------------------------------------------------------

def build_column(
    name="column",
    # Overall / shaft.
    total_height=3.0,
    shaft_radius=0.22,
    shaft_sides=20,
    shaft_taper=0.85,
    shaft_rotation=0.0,
    corner_chamfer=0.0,
    corner_chamfer_segments=1,
    corner_bow=1.0,
    # Vertical fluting / reeding relief running along the shaft.
    fluting=None,
    flute_count=20,
    flute_depth=0.07,
    flute_segments=5,
    flute_fillet_ratio=0.35,
    flute_smooth_angle=26.0,
    # Horizontal engraved bands cut into the shaft.
    shaft_insets=0,
    shaft_inset_style="scotia",
    shaft_inset_depth=0.03,
    shaft_inset_height=0.05,
    # Base block (the extruded rectangular plinth).
    base_block=True,
    base_width=0.72,
    base_depth=0.72,
    base_block_height=0.20,
    base_z=0.0,
    # Capital block (the extruded rectangular abacus).
    capital_block=True,
    capital_style="block",
    capital_width=0.68,
    capital_depth=0.68,
    capital_block_height=0.14,
    cushion_corner_rounding=1.0,
    cushion_boundary_softness=1.0,
    # Base flare (block top -> shaft bottom transition).
    base_flare=0.5,
    base_flare_height=0.15,
    base_flare_segments=1,
    base_flare_curvature=0.0,
    base_moulding=None,
    base_moulding_projection=0.4,
    # Capital flare (shaft top -> block underside transition).
    capital_flare=0.6,
    capital_flare_height=0.16,
    capital_flare_segments=1,
    capital_flare_curvature=0.0,
    capital_moulding=None,
    capital_moulding_projection=0.4,
    # Grid snapping (origin is assumed to sit on a grid coordinate).
    grid_size=(0.25, 0.25, 0.25),
    snap_base=False,
    snap_capital=False,
    snap_shaft=False,
    # Shading.
    smooth_radial=None,
    smooth_vertical=False,
    collection=None,
):
    """Build a column mesh object and link it into the scene.

    Overall / shaft:
      total_height -- Z of the column top (capital block top), from Z = 0.
      shaft_radius -- circumradius of the shaft cross-section (scale 1.0
                      of the taper profile).
      shaft_sides  -- polygon corner count, any value >= 3 (16+ reads
                      round; 5/6/8 reads polygonal).
      shaft_taper  -- EITHER a float: top/bottom radius ratio (linear
                      taper, 1.0 = straight); OR a list of >= 2 width
                      factors bottom->top, evenly spaced along the shaft,
                      each a multiple of shaft_radius (e.g. a bulged
                      entasis profile [1.0, 1.06, 0.95, 0.8]).
      shaft_rotation -- rotation of the cross-section about its center, in
                      RADIANS, applied at creation. E.g. shaft_sides=4 is
                      a block-aligned square at 0 and a diamond at
                      math.radians(45). Blocks stay axis-aligned.
      corner_chamfer / corner_chamfer_segments -- optional cut applied to
                      each cross-section corner (see _shaft_profile).

    Blocks (each an extruded rectangular slab, individually optional):
      base_block / capital_block   -- False removes the block; that end of
                      the flare/shaft is closed with a flat all-quad cap.
      capital_style -- "block" (default): the capital flare rises into the
                      rectangular abacus. "cushion": a Romanesque cushion /
                      block capital instead — the shaft top first extrudes
                      OUTWARD to the cushion's bell radius (a flat
                      projecting rim, radius set by capital_flare), then
                      the block's four bottom corners are cut by a
                      zero-offset asymmetric chamfer: count-matched rings
                      morph from the round rim to the rectangular block
                      footprint over capital_flare_height, so with
                      capital_flare_segments=1 each corner is a single
                      planar chamfer facet and higher segment counts (plus
                      capital_flare_curvature) carve the rounded cushion.
                      The capital block itself becomes the abacus on top
                      (capital_block=False caps flat at the cushion top).
      cushion_corner_rounding -- 0..1 chamfer smoothness: 0 keeps every
                      chamfer vert on the straight bell-to-corner diagonal
                      (a harsh, flat cut for dwarven work); 1 bows the
                      approach so the surface arrives tangent to the block
                      sides (the round Romanesque cushion). Resolution
                      comes from capital_flare_segments; shape from this.
      cushion_boundary_softness -- 0..1 normal crease between the flat
                      shield panels and the rounded corner chamfers:
                      0 = hard crease (shield loops keep the flat side-
                      plane normal, chamfer loops average only over curved
                      faces — a crisp carved-panel read); 1 = fully
                      averaged smooth normals; between values lerp.
                      (capital_flare_curvature is unused in cushion mode.)
      base_width, base_depth, base_block_height       -- plinth dimensions.
      capital_width, capital_depth, capital_block_height -- abacus
                      dimensions.
      base_z       -- Z of the base block underside (default 0; negative
                      sinks the plinth below floor level). The mesh origin
                      stays at (0, 0, 0).

    Flares (independently parametrized transitions; heights set the
    vertical alignment of the shaft between the blocks):
      base_flare / capital_flare       -- overhang as a fraction of the
                      local shaft radius (0 = no flare, straight step).
                      Clamped to stay inside the block footprint.
      base_flare_height / capital_flare_height -- vertical extent of each
                      transition (0 = hard edge at the block).
      base_flare_segments / capital_flare_segments -- quad bands along the
                      flare (1 = straight cone, more = smooth curve).
      base_flare_curvature / capital_flare_curvature -- -1..+1: negative
                      coves inward (cavetto), positive bulges outward
                      (ovolo), 0 is a straight cone.

    Grid snapping:
      grid_size    -- (gx, gy, gz) cell size tuple.
      snap_base    -- snap base block width/depth/height and base_z.
      snap_capital -- snap capital block width/depth/height and
                      total_height (so the column top lands on the grid).
      snap_shaft   -- snap the shaft's bottom and top Z (the flare heights
                      absorb the adjustment).

    Shading (explicit per-loop normals on the curved section):
      smooth_radial   -- smooth normals around the circumference. None
                      (default) auto-enables when shaft_sides >= 16.
      smooth_vertical -- smooth normals across the bands vertically.

    collection -- optional bpy collection to link into (defaults to the
                  scene root collection).

    Returns the new mesh object, origin at base center (Z = 0).
    Raises ValueError on out-of-range or inconsistent parameters.
    """
    if shaft_sides < 3:
        raise ValueError("shaft_sides must be >= 3")
    if not 0.0 <= corner_chamfer <= 0.9:
        raise ValueError("corner_chamfer must be in [0, 0.9]")
    if (corner_chamfer_segments < 1 or base_flare_segments < 1
            or capital_flare_segments < 1):
        raise ValueError("segment counts must be >= 1")
    if base_flare < 0.0 or capital_flare < 0.0:
        raise ValueError("flare overhangs must be >= 0")
    if base_flare_height < 0.0 or capital_flare_height < 0.0:
        raise ValueError("flare heights must be >= 0")
    if capital_style not in ("block", "cushion"):
        raise ValueError('capital_style must be "block" or "cushion"')
    if capital_style == "cushion" and capital_flare_height <= 0.0:
        raise ValueError("cushion capital needs capital_flare_height > 0")
    if not (0.0 <= cushion_corner_rounding <= 1.0
            and 0.0 <= cushion_boundary_softness <= 1.0):
        raise ValueError("cushion_corner_rounding and "
                         "cushion_boundary_softness must be in [0, 1]")
    if smooth_radial is None:
        smooth_radial = shaft_sides >= 16

    # Taper profile: float ratio -> linear 2-ring profile; list -> one ring
    # per width factor, evenly spaced bottom->top.
    if isinstance(shaft_taper, (int, float)):
        taper = [1.0, float(shaft_taper)]
    else:
        taper = [float(w) for w in shaft_taper]
    if len(taper) < 2 or any(w <= 0.0 for w in taper):
        raise ValueError("shaft_taper list needs >= 2 positive widths")

    gx, gy, gz = grid_size
    if snap_base:
        base_width = _snap_size(base_width, gx)
        base_depth = _snap_size(base_depth, gy)
        base_block_height = _snap_size(base_block_height, gz)
        base_z = _snap(base_z, gz)
    if snap_capital:
        capital_width = _snap_size(capital_width, gx)
        capital_depth = _snap_size(capital_depth, gy)
        capital_block_height = _snap_size(capital_block_height, gz)
        total_height = _snap_size(total_height, gz)

    # Vertical alignment of the stack (block heights + flare heights place
    # the shaft; snapping the shaft adjusts the flares implicitly).
    z_base_top = base_z + (base_block_height if base_block else 0.0)
    z_shaft_bot = z_base_top + base_flare_height
    z_cap_block_bot = (total_height
                       - (capital_block_height if capital_block else 0.0))
    z_shaft_top = z_cap_block_bot - capital_flare_height
    if snap_shaft:
        z_shaft_bot = _snap(z_shaft_bot, gz)
        z_shaft_top = _snap(z_shaft_top, gz)
    eps = 1e-9
    if not (base_z <= z_base_top <= z_shaft_bot + eps
            and z_shaft_bot < z_shaft_top
            and z_shaft_top - eps <= z_cap_block_bot <= total_height):
        raise ValueError("inconsistent vertical stack: check heights, "
                         "flare heights, and snapping")

    if fluting:
        if fluting not in ("flute", "flute_fillet", "reed"):
            raise ValueError('fluting must be "flute", "flute_fillet", '
                             '"reed", or None')
        if capital_style == "cushion":
            raise ValueError("fluting is not supported with a cushion capital")
        # The fluted cross-section replaces the polygon; flute_pts is the fully
        # cut profile and smooth_pts the plain reference circle (fades the
        # flutes out to smooth at the flares / blocks per ring).
        flute_pts, smooth_pts = _flute_section(
            flute_count, fluting, flute_depth, flute_fillet_ratio,
            flute_segments, shaft_rotation)
        profile = smooth_pts
    else:
        flute_pts = smooth_pts = None
        profile = _shaft_profile(shaft_sides, corner_chamfer,
                                 corner_chamfer_segments, shaft_rotation,
                                 corner_bow)
    if capital_style == "cushion":
        # The inset-shield cushion needs at least 6 verts per quarter
        # (room for the shield plus a redirect diamond at each corner).
        # Pad low-poly profiles with collinear edge splits (same geometry,
        # extra verts) — the same trick used for non-multiple-of-4 counts.
        while len(profile) < 24 or len(profile) % 4:
            n = len(profile)
            longest = max(range(n), key=lambda i: math.dist(
                profile[i], profile[(i + 1) % n]))
            ax, ay = profile[longest]
            bx, by = profile[(longest + 1) % n]
            profile.insert(longest + 1, ((ax + bx) * 0.5, (ay + by) * 0.5))
    seg_n, vert_n = _profile_normals(profile)
    count = len(profile)
    inrad = _profile_inradius(profile)

    r_shaft_bot = shaft_radius * taper[0]
    r_shaft_top = shaft_radius * taper[-1]
    # Flare end radii, kept inside the block footprints.
    flare_r = r_shaft_bot * (1.0 + base_flare)
    if base_block:
        flare_r = min(flare_r, min(base_width, base_depth) * 0.47)
    echinus_r = r_shaft_top * (1.0 + capital_flare)
    if capital_style == "cushion":
        # Cushion bell: at most tangent to the block's side faces.
        echinus_r = min(echinus_r, min(capital_width, capital_depth) * 0.5)
    elif capital_block:
        echinus_r = min(echinus_r, min(capital_width, capital_depth) * 0.47)
    if flare_r < r_shaft_bot or echinus_r < r_shaft_top:
        raise ValueError("block footprint too small for the shaft/flare "
                         "diameter")

    # --- Profile ring parameters (radius scale, z), bottom to top: base
    # flare, shaft taper profile, capital flare. Consecutive duplicates
    # (e.g. zero-height, zero-overhang flares) collapse away.
    params = []

    def _push(r, z):
        if not params or (abs(params[-1][0] - r) > eps
                          or abs(params[-1][1] - z) > eps):
            params.append((r, z))

    base_mould = _resolve_moulding(base_moulding, _BASE_MOULDINGS)
    if base_mould:
        # Classical moulding stack occupies the whole base transition, plinth
        # top (flare_r) up to the shaft (r_shaft_bot). Bead amplitudes scale
        # with base_moulding_projection (bulge width), independent of taper.
        _push(flare_r, z_base_top)
        for r, z in _moulding_rings(
                base_mould, flare_r, r_shaft_bot, z_base_top, z_shaft_bot,
                shaft_radius, shaft_radius * base_moulding_projection):
            _push(r, z)
    else:
        S = base_flare_segments
        for i in range(S + 1):
            s = i / S
            r = flare_r + (r_shaft_bot - flare_r) * _ease(
                s, base_flare_curvature)
            _push(r, z_base_top + (z_shaft_bot - z_base_top) * s)
    # Shaft section: taper rings, plus optional evenly spaced engraved bands.
    shaft_span = z_shaft_top - z_shaft_bot

    def shaft_r_at(f):
        """Baseline (un-grooved) shaft radius at fraction f in [0, 1]."""
        f = min(max(f, 0.0), 1.0)
        x = f * (len(taper) - 1)
        i = min(int(x), len(taper) - 2)
        return shaft_radius * (taper[i] + (taper[i + 1] - taper[i]) * (x - i))

    shaft_rings = [(z_shaft_bot + shaft_span * (i / (len(taper) - 1)),
                    shaft_radius * w) for i, w in enumerate(taper)]
    if fluting and shaft_span > 0.0:
        # Fluting needs enough vertical rings to resolve the rounded flute
        # stops where the grooves fade back to the smooth shaft at each end.
        nsub = 18
        for j in range(1, nsub):
            f = j / nsub
            shaft_rings.append((z_shaft_bot + shaft_span * f, shaft_r_at(f)))
    if shaft_insets > 0 and shaft_inset_depth > 0.0 and shaft_span > 0.0:
        inset_mould = _resolve_moulding(shaft_inset_style, _INSET_MOULDINGS)
        gh = min(shaft_inset_height, shaft_span / (shaft_insets + 1) * 0.9)
        for n in range(shaft_insets):
            fc = (n + 1) / (shaft_insets + 1)          # groove centre fraction
            zc = z_shaft_bot + shaft_span * fc
            z0, z1 = zc - gh * 0.5, zc + gh * 0.5
            r0 = shaft_r_at((z0 - z_shaft_bot) / shaft_span)
            r1 = shaft_r_at((z1 - z_shaft_bot) / shaft_span)
            shaft_rings.append((z0, r0))               # flush groove shoulders
            for r, z in _moulding_rings(inset_mould, r0, r1, z0, z1,
                                        shaft_radius, shaft_inset_depth):
                shaft_rings.append((z, r))
    for z, r in sorted(shaft_rings):
        _push(r, z)
    if capital_style == "block":
        cap_mould = _resolve_moulding(capital_moulding, _CAPITAL_MOULDINGS)
        if cap_mould:
            # Classical capital moulding: shaft top (r_shaft_top) flaring up to
            # the abacus underside (echinus_r).
            for r, z in _moulding_rings(
                    cap_mould, r_shaft_top, echinus_r, z_shaft_top,
                    z_cap_block_bot, shaft_radius,
                    shaft_radius * capital_moulding_projection):
                _push(r, z)
        else:
            S = capital_flare_segments
            for i in range(1, S + 1):
                t = i / S
                r = echinus_r + (r_shaft_top - echinus_r) * _ease(
                    1.0 - t, capital_flare_curvature)
                _push(r, z_shaft_top + (z_cap_block_bot - z_shaft_top) * t)
    if len(params) < 2:
        raise ValueError("degenerate column: no vertical extent")

    bm = bmesh.new()

    def flute_amount(z):
        """Flute depth fraction at height z: 0 at/below the shaft ends (so the
        flares and blocks stay smooth), easing to 1 across the interior — the
        rounded flute stops. Smoothstep over a short zone at each shaft end."""
        if z <= z_shaft_bot or z >= z_shaft_top or shaft_span <= 0.0:
            return 0.0
        d = min(z - z_shaft_bot, z_shaft_top - z)
        stop = shaft_span * 0.12
        t = 1.0 if stop <= 0.0 else min(1.0, d / stop)
        return t * t * (3.0 - 2.0 * t)

    prof_rings = []
    for r, z in params:
        if fluting:
            a = flute_amount(z)
            ring = []
            for (sx, sy), (fx, fy) in zip(smooth_pts, flute_pts):
                ring.append(bm.verts.new(((sx + (fx - sx) * a) * r,
                                          (sy + (fy - sy) * a) * r, z)))
            prof_rings.append(ring)
        else:
            prof_rings.append([bm.verts.new((x * r, y * r, z))
                               for x, y in profile])

    # --- Bottom end: plinth block, or a flat inscribed cap.
    if base_block:
        ring0 = _rect_ring(bm, count, base_width * 0.5, base_depth * 0.5,
                           base_z)
        ring1 = _rect_ring(bm, count, base_width * 0.5, base_depth * 0.5,
                           z_base_top)
        _stitch_band(bm, ring0, ring1)                 # plinth sides
        _grid_fill_cap(bm, ring0, base_width * 0.5, base_depth * 0.5, base_z)
        _stitch_band(bm, ring1, prof_rings[0])         # plinth top annulus
    else:
        h = params[0][0] * inrad * 0.6                 # inscribed square cap
        ring1 = _rect_ring(bm, count, h, h, params[0][1])
        _grid_fill_cap(bm, ring1, h, h, params[0][1])
        _stitch_band(bm, ring1, prof_rings[0])         # flat annulus

    # --- Curved section bands (custom normals apply here). A fluted shaft is
    # shaded smooth and creased by angle instead of by explicit normals.
    smooth_any = smooth_radial or smooth_vertical
    face_smooth = smooth_any or bool(fluting)
    band_faces = []
    for lower, upper in zip(prof_rings, prof_rings[1:]):
        band_faces.append(_stitch_band(bm, lower, upper, smooth=face_smooth))

    # --- Top end: cushion capital, abacus block, or a flat inscribed cap.
    cushion_bands = []
    if capital_style == "cushion":
        hx, hy = capital_width * 0.5, capital_depth * 0.5
        square = _rect_positions(count, hx, hy)
        # Fixed reference topology (collar, one bowl band, diamonds,
        # corner ladders, whole-face inset); resolution comes from the
        # profile count. capital_flare_segments is not used here.
        cushion_faces, ngon_faces, top_c = _build_cushion_inset(
            bm, prof_rings[-1], profile, echinus_r, square,
            z_shaft_top, z_cap_block_bot, cushion_corner_rounding)
        for f in cushion_faces:
            f.smooth = smooth_any
        cushion_bands.append(cushion_faces)
        if capital_block:
            ring3 = _rect_ring(bm, count, hx, hy, total_height)
            _stitch_band(bm, top_c, ring3)             # abacus sides
            bm.faces.new(ring3)                        # single n-gon cap
        else:
            bm.faces.new(top_c)
    elif capital_block:
        ring2 = _rect_ring(bm, count, capital_width * 0.5,
                           capital_depth * 0.5, z_cap_block_bot)
        ring3 = _rect_ring(bm, count, capital_width * 0.5,
                           capital_depth * 0.5, total_height)
        _stitch_band(bm, prof_rings[-1], ring2)        # abacus underside
        _stitch_band(bm, ring2, ring3)                 # abacus sides
        _grid_fill_cap(bm, ring3, capital_width * 0.5, capital_depth * 0.5,
                       total_height)
    else:
        h = params[-1][0] * inrad * 0.6
        ring2 = _rect_ring(bm, count, h, h, params[-1][1])
        _stitch_band(bm, prof_rings[-1], ring2)
        _grid_fill_cap(bm, ring2, h, h, params[-1][1])

    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)

    # --- Explicit per-loop normals for the curved section. Horizontal
    # direction comes from the 2D profile (per-vertex bisector when radial-
    # smooth, per-segment normal when faceted); the (radial, z) tilt comes
    # from the meridian slope of the band (averaged across neighbor bands
    # when vertically smooth). Composed as (h.x*t_r, h.y*t_r, t_z).
    custom = {}
    if smooth_any and not fluting:
        n_bands = len(band_faces)
        tilts = []  # per band: (t_r, t_z) unit meridian normal
        for b in range(n_bands):
            (r0, z0), (r1, z1) = params[b], params[b + 1]
            dz, dr = z1 - z0, r1 - r0
            l = math.hypot(dz, dr) or 1.0
            tilts.append((dz / l, -dr / l))

        def ring_tilt(rb):
            """Tilt at ring rb: average of adjacent bands when smoothing
            vertically; hard boundary rings keep their single band's tilt."""
            lo, hi = max(rb - 1, 0), min(rb, n_bands - 1)
            tx = tilts[lo][0] + tilts[hi][0]
            tz = tilts[lo][1] + tilts[hi][1]
            l = math.hypot(tx, tz) or 1.0
            return (tx / l, tz / l)

        bm.faces.index_update()
        for b in range(n_bands):
            for k, face in enumerate(band_faces[b]):
                j = (k + 1) % count
                corners = ((k, b), (j, b), (j, b + 1), (k, b + 1))
                normals = []
                for pk, rb in corners:
                    h = vert_n[pk] if smooth_radial else seg_n[k]
                    t = ring_tilt(rb) if smooth_vertical else tilts[b]
                    normals.append((h[0] * t[0], h[1] * t[0], t[1]))
                custom[face.index] = normals

    # The cushion is not a surface of revolution, so its smooth normals are
    # averaged numerically per vertex over the cushion faces only — the rim
    # ledge and the abacus keep hard edges. cushion_boundary_softness
    # controls the crease between the flat shield panels (faces lying in a
    # block side plane) and the curved chamfer faces: at 0 each side of the
    # boundary averages only its own kind, at 1 everything averages.
    if cushion_bands and smooth_any:
        bm.faces.index_update()
        hx_, hy_ = capital_width * 0.5, capital_depth * 0.5
        plane_eps = 1e-6

        def face_planar(f):
            xs = [v.co.x for v in f.verts]
            ys = [v.co.y for v in f.verts]
            return (all(abs(abs(x) - hx_) < plane_eps for x in xs)
                    or all(abs(abs(y) - hy_) < plane_eps for y in ys))

        acc_all, acc_curv = {}, {}
        for band in cushion_bands:
            for f in band:
                planar = face_planar(f)
                for v in f.verts:
                    a = acc_all.setdefault(v, [0.0, 0.0, 0.0])
                    a[0] += f.normal.x
                    a[1] += f.normal.y
                    a[2] += f.normal.z
                    if not planar:
                        a = acc_curv.setdefault(v, [0.0, 0.0, 0.0])
                        a[0] += f.normal.x
                        a[1] += f.normal.y
                        a[2] += f.normal.z

        def unit(a):
            l = math.sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]) or 1.0
            return (a[0] / l, a[1] / l, a[2] / l)

        soft = cushion_boundary_softness
        for band in cushion_bands:
            for f in band:
                # Hard normals: a planar (shield) face keeps its own plane
                # normal — flat panels and sharp block corner edges; a
                # curved face averages over curved faces only. Softness
                # lerps toward the everything-averaged normal.
                planar = face_planar(f)
                normals = []
                for v in f.verts:
                    if planar:
                        hard = (f.normal.x, f.normal.y, f.normal.z)
                    else:
                        hard = unit(acc_curv.get(v, [0.0, 0.0, 1.0]))
                    full = unit(acc_all[v])
                    normals.append(unit([hard[i] + (full[i] - hard[i]) * soft
                                         for i in range(3)]))
                custom[f.index] = normals

    if fluting:
        # Angle-based auto-smooth: every face smooth, edges sharper than the
        # threshold left as creases. This keeps the round shaft and the groove
        # bottoms smooth while the flute arrises stay crisp — the same shading
        # a modelled column has, without hand-authored per-loop normals.
        thr = math.radians(flute_smooth_angle)
        for f in bm.faces:
            f.smooth = True
        for e in bm.edges:
            if len(e.link_faces) == 2:
                e.smooth = e.calc_face_angle() <= thr

    # UV seams: meridian down the profile's vertex-0 side + the cap rims.
    _mark_column_seams(bm, math.atan2(profile[0][1], profile[0][0]))

    mesh = bpy.data.meshes.new(name)
    bm.to_mesh(mesh)
    bm.free()

    if custom:
        loop_normals = [None] * len(mesh.loops)
        for poly in mesh.polygons:
            face_normals = custom.get(poly.index)
            for j, li in enumerate(poly.loop_indices):
                loop_normals[li] = (face_normals[j] if face_normals
                                    else tuple(poly.normal))
        mesh.normals_split_custom_set(loop_normals)

    obj = bpy.data.objects.new(name, mesh)
    (collection or bpy.context.scene.collection).objects.link(obj)
    _finalize_uvs(obj)
    return obj
