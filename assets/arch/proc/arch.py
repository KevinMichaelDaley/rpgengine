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

    return pos, faces, splay


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
    for f in faces:
        for a, b in zip(f, f[1:] + f[:1]):
            if und[frozenset((a, b))] == 1:
                bm.faces.new((front[a], front[b], back[b], back[a]))
    return front, back


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
    smooth_angle=35.0,
    grid_size=(0.25, 0.25, 0.25),
    snap_panel=False,
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

    pos, faces, splay_dirs = _frame_faces(
        half_w, half_wp, sill_height, opening_height, panel_height,
        arch_shape, head_rise, jamb_segments, head_segments, wall_columns,
        corner_rows, sill_columns, band_rows)
    off = {i: (splay * dx, splay * dz)
           for i, (dx, dz) in splay_dirs.items()} if splay > 0 else None

    bm = bmesh.new()
    _extrude_panel(bm, pos, faces, -wall_thickness * 0.5,
                   wall_thickness * 0.5, off, off_front=(wide_side == "outer"))
    return _finish(bm, name, collection, smooth_angle)


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
    pos, faces, splay_dirs = _frame_faces(
        half_w, half_wp, sill_height, opening_height, panel_height,
        arch_shape, head_rise, jamb_segments, max(4, head_segs), wall_cols,
        corner_rows, sill_cols, band_rows)
    off = {i: (splay * dx, splay * dz)
           for i, (dx, dz) in splay_dirs.items()} if splay > 0 else None

    bm = bmesh.new()
    _extrude_panel(bm, pos, faces, -wall_thickness * 0.5,
                   wall_thickness * 0.5, off, off_front=(wide_side == "outer"))
    _warp_cylinder(bm, tower_radius)
    return _finish(bm, name, collection, smooth_angle)


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

    pos, faces, splay_dirs = _frame_faces(
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
    if clip_bottom:
        z = floor_z if floor_z is not None else max(v.co.z for v in bottom_v)
        _clip_plane(bm, z, keep_above=True)
    if clip_top:
        z = ceiling_z if ceiling_z is not None else min(v.co.z for v in top_v)
        _clip_plane(bm, z, keep_above=False)
    return _finish(bm, name, collection, smooth_angle)


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
