"""D1 Street Section + Intersection generators (rpg-psto) --
ferrum.la_street / ferrum.la_intersection.

STREET: crowned asphalt roadway, gutter pans, curbs, heaved sidewalk
slabs, NORMAL street markings (solid white edge lines, dashed white lane
lines, yellow centerline forms), median forms, crosswalks/stop bars,
asphalt patches, optional sinkhole, alley mouths -- plus longitudinal
GRADE, horizontal CURVATURE, and TERRAIN projection, all applied so that
segment ends remain exact profile cross-sections and pieces always join.

TOPOLOGY PLAN (welded-grid discipline)
======================================
The street is authored STRAIGHT along +X (length L measured along the
centerline), section along Y, z = 0 at the gutter flowline. ONE welded
grid (shared global x/y line families) covers roadway + gutters + curbs
+ median; every feature contributes its x-lines to the SAME family.

* CROWN: piecewise-linear z(y); every roadway row spans one linear
  segment so all surface cells are planar quads. Paint strips clip at
  the kinks.
* SIDEWALK: skirted slab islands (2 mm joints) -- heave pitches each
  slab about one axis, so tops stay planar and joints read as cracks.
* MARKINGS: overlay faces +3 mm (B1 lot-stripe pattern): solid white
  edge lines, dashed white lane lines, centerline enum (double solid /
  single solid / dashed yellow), turn-lane solid+dash pairs, crosswalk
  style enum -- 'transverse' (two solid bars, the default) or
  'continental' (piano-key bars).
* JOIN CONTRACT: grade, curvature, and terrain are applied as a final
  per-vertex transform z += g(x) then arc-bend(x, y). g and the bend
  never alter the cross-section shape, so the x = 0 / x = L end planes
  carry the exact straight-street profile: any two segments built with
  the same lanes/lane_width/sidewalk_width join seamlessly. A terrain
  fit drops the street ONTO the surface (absolute heights); segments
  chained over the same terrain meet within the smoothing window. All
  overlays share the SAME piecewise mapping, so they ride exactly 3 mm
  above the surface everywhere.
* DEFORMABLE GRID: the roadway is a genuinely regular quad grid --
  2.5 m stations along x (denser when bent/terrain-fit) and half-lane
  rows across y -- so downstream deforms (lattice, shrinkwrap, block
  assembler terrain) have vertices to work with.
* TERRAIN: `terrain_object` names a mesh; the (bent) centerline ray-
  casts it, the height series is smoothed and slope-clamped (8 %), and
  applied longitudinally ONLY -- the cross-section never tilts.

INTERSECTION: n approach arms (veer / tee / cross / five + skew) around
a flat asphalt pad at z_e. Each arm carries a street-profile APRON that
blends the crown down to the flat pad; adjacent arms are joined by
filleted CURB RETURNS (arc gutter + curb strips) with sidewalk wedges
behind. The arm interface (outer apron end) is the exact straight-street
cross-section, so la_street segments plug onto the arms.

Story options (off by default): checkpoint_scar (resurfacing scar +
flush steel base plates where bollards were sheared), sand_lane (dune
berm with per-point base so the taper tips clear curb and walk),
protest_stain (crown-following chamfered scorch blooms).
"""
import math

import bpy

from .. import params
from ..geom import (_Shell, _box, _material, _MATS,
                    M_CONCRETE, M_METAL, M_ASPHALT, M_PAINT_W, M_PAINT_Y,
                    M_PATCH, M_SAND, M_SCORCH, M_SOIL)

GUT_W = 0.45          # gutter pan width
CURB_H = 0.15         # curb face height
CURB_W = 0.15         # curb top width
Z_WALK = 0.16         # nominal sidewalk slab top
SLAB_L = 1.7          # sidewalk expansion-joint pitch
GAP = 0.002           # island clearance (abutting discipline)
Z_E = 0.015           # road surface at the gutter line
MAX_GRADE = 0.08      # terrain-fit longitudinal slope clamp


def _cells(lines, lo, hi):
    """Consecutive (a, b) pairs of `lines` clipped to [lo, hi]."""
    seg = [v for v in lines if lo - 1e-9 <= v <= hi + 1e-9]
    return list(zip(seg, seg[1:]))


def _xsection(lanes, lw, med):
    """Shared cross-section math: returns (y0r, m0, m1, y1r, z_pk, z_road)."""
    n_s = (lanes + 1) // 2
    n_n = lanes // 2
    med_w = {'none': 0.0, 'turn': 3.3, 'palms': 1.8}[med]
    road_w = lanes * lw + med_w
    y0r = -road_w / 2.0
    m0 = y0r + n_s * lw
    m1 = m0 + med_w
    y1r = m1 + n_n * lw
    z_pk = Z_E + 0.015 * (n_s * lw)

    def z_road(y):
        if y <= m0 + 1e-9:
            t5 = (y - y0r) / max(m0 - y0r, 1e-9)
            return Z_E + (z_pk - Z_E) * min(max(t5, 0.0), 1.0)
        if y >= m1 - 1e-9:
            t5 = (y1r - y) / max(y1r - m1, 1e-9)
            return Z_E + (z_pk - Z_E) * min(max(t5, 0.0), 1.0)
        return z_pk

    return n_s, n_n, y0r, m0, m1, y1r, z_pk, z_road


def _strip(shell, xs, x0, x1, y0, y1, zf, mat, tag, lift=0.0):
    """A planar overlay strip on the road surface (zf linear over [y0,y1])."""
    za, zb = zf(y0) + lift, zf(y1) + lift
    for (a, b) in _cells(xs, x0, x1):
        shell.quad((a, y0, za), (b, y0, za), (b, y1, zb), (a, y1, zb),
                   mat, tag)


def _terrain_profile(name, L, bend, cursor, step=2.0):
    """Slope-clamped longitudinal height profile from a named terrain mesh.

    Samples the (bent) centerline every `step` m by ray-casting the object,
    smooths, clamps slopes to MAX_GRADE, and ZEROES the start so the x=0
    interface stays at the cursor. Returns (stations, fn) or (None, None).
    """
    ob = bpy.data.objects.get(name) if name else None
    if ob is None or ob.type != 'MESH':
        return None, None
    # ray-cast the EVALUATED object: a modifier-displaced terrain (subsurf,
    # displace, geo-nodes) is invisible to the raw mesh's ray_cast.
    dg = bpy.context.evaluated_depsgraph_get()
    ob_ev = ob.evaluated_get(dg)
    inv = ob_ev.matrix_world.inverted()
    n9 = max(2, int(round(L / step)))
    st9 = [L * i / n9 for i in range(n9 + 1)]
    hs = []
    from mathutils import Vector
    for x in st9:
        wx, wy = bend(x, 0.0)
        origin = inv @ Vector((cursor[0] + wx, cursor[1] + wy, 10000.0))
        direction = inv.to_3x3() @ Vector((0.0, 0.0, -1.0))
        hit, loc, _n, _i = ob_ev.ray_cast(origin, direction)
        hs.append((ob_ev.matrix_world @ loc).z - cursor[2] if hit else None)
    if all(h is None for h in hs):
        return None, None                        # terrain never under us
    last = next(h for h in hs if h is not None)
    for i in range(len(hs)):                     # fill misses
        if hs[i] is None:
            hs[i] = last
        last = hs[i]
    hs = [sum(hs[max(0, i - 2):i + 3]) / len(hs[max(0, i - 2):i + 3])
          for i in range(len(hs))]               # smooth
    for _p in range(4):                          # clamp slopes both ways
        for i in range(1, len(hs)):
            dm = MAX_GRADE * (st9[i] - st9[i - 1])
            hs[i] = min(max(hs[i], hs[i - 1] - dm), hs[i - 1] + dm)
        for i in range(len(hs) - 2, -1, -1):
            dm = MAX_GRADE * (st9[i + 1] - st9[i])
            hs[i] = min(max(hs[i], hs[i + 1] - dm), hs[i + 1] + dm)
    # NO zeroing: the street LANDS ON the terrain (that is the point);
    # chained segments sampling the same terrain meet within the
    # smoothing window.

    def fn(x):
        x = min(max(x, 0.0), L)
        i = min(int(x / L * n9), n9 - 1)
        t9 = (x - st9[i]) / (st9[i + 1] - st9[i])
        return hs[i] + (hs[i + 1] - hs[i]) * t9

    return st9, fn


def build_street(p, rng):
    """Build the street section. Returns unlinked objects (operator links)."""
    L = p["length"]
    lanes = p["lanes"]
    lw = p["lane_width"]
    sw = p["sidewalk_width"]
    med = p["median"]
    interior_on = p["mode"] == 'interior'
    heave = p["heave"]
    wear = p["paint_wear"]

    n_s, n_n, y0r, m0, m1, y1r, z_pk, z_road = _xsection(lanes, lw, med)
    y_cf_s = y0r - GUT_W
    y_cf_n = y1r + GUT_W

    # ---- join-preserving transform: grade + terrain + arc bend -------------
    theta = math.radians(p["curve"])
    y_out = y_cf_n + CURB_W + GAP + sw + 1.0
    if abs(theta) > 1e-6:
        R = L / abs(theta)
        if R < y_out + 2.0:                      # clamp: keep radius sane
            R = y_out + 2.0
            theta = math.copysign(L / R, theta)
    else:
        R = 0.0

    def bend(x, y):
        if R == 0.0:
            return x, y
        a9 = x / R
        if theta > 0.0:                          # curve left (center +y)
            return (R - y) * math.sin(a9), R - (R - y) * math.cos(a9)
        return (R + y) * math.sin(a9), (R + y) * math.cos(a9) - R

    cursor = tuple(bpy.context.scene.cursor.location)
    t_st, t_fn = _terrain_profile(p["terrain_object"], L, bend, cursor)
    rise = p["end_rise"]

    def grade(x):
        g = rise * x / L
        if t_fn:
            g += t_fn(x)
        return g

    def xform(co):
        x, y, z = co
        z += grade(x)
        wx, wy = bend(x, y)
        return (wx, wy, z)

    # ---- feature placement (seeded; clamped clear of each other) -----------
    hole = None
    if p["sinkhole"]:
        hw2 = min(1.5 + rng.random() * 0.9, max(0.9, (L - 9.4) / 2.0))
        hd2 = 1.1 + rng.random() * 0.5
        hcx = L * (0.55 + rng.random() * 0.15)
        hcx = min(max(hcx, 4.6 + hw2), L - 4.6 - hw2)
        hy_c = (y0r + m0) / 2.0
        hole = (hcx - hw2, hcx + hw2,
                max(y0r + 0.30, hy_c - hd2), min(m0 - 0.30, hy_c + hd2))
    scar = None
    if p["checkpoint_scar"]:
        sx0 = L * (0.22 + rng.random() * 0.08)
        if hole and sx0 + 4.0 > hole[0] - 0.5:
            sx0 = max(0.5, hole[0] - 4.5)
        scar = (sx0, sx0 + 4.0)
    mouths = []
    if p["alley_mouths"] in ('south', 'both'):
        mx = min(L * (0.30 + rng.random() * 0.25), L - 4.5)
        mouths.append((-1, mx, mx + 3.6))
    if p["alley_mouths"] in ('north', 'both'):
        mx = min(L * (0.55 + rng.random() * 0.25), L - 4.5)
        mouths.append((1, mx, mx + 3.6))

    med_on = med == 'palms'
    mx0, mx1 = 4.0, L - 4.0

    # ---- global line families ----------------------------------------------
    xs = {0.0, L}
    step = 2.5                                   # regular, deformable grid
    if R:
        step = min(step, max(1.0, R * 0.105))    # ~6 deg chords when bent
    if t_fn:
        step = min(step, 2.0)
    k = 1
    while k * step < L - 0.3:
        xs.add(k * step)
        k += 1
    if t_st:
        xs |= {x for x in t_st if 0.0 < x < L}   # profile kinks split cells
    if hole:
        hx0, hx1, hy0, hy1 = hole
        xs |= {hx0, hx1, hx0 + 0.18, hx1 - 0.18}
        if interior_on:
            xs |= {hx0 + 0.33, hx1 - 0.33}
    if scar:
        xs |= {scar[0], scar[1]}
    for (_sg, a, b) in mouths:
        xs |= {a, a + 0.7, b - 0.7, b}
    if med_on:
        xs |= {mx0, mx1, mx0 + CURB_W, mx1 - CURB_W}
    xs = sorted(xs)

    ys = {y0r, m0, m1, y1r}
    for k in range(1, n_s):
        ys.add(y0r + k * lw)
    for k in range(n_s):                         # half-lane rows: keeps the
        ys.add(y0r + (k + 0.5) * lw)             # surface cells square-ish
    for k in range(1, n_n):
        ys.add(m1 + k * lw)
    for k in range(n_n):
        ys.add(m1 + (k + 0.5) * lw)
    if hole:
        ys |= {hy0, hy1, hy0 + 0.18, hy1 - 0.18}
        if interior_on:
            ys |= {hy0 + 0.33, hy1 - 0.33}
    if med_on:
        ys |= {m0 + CURB_W, m1 - CURB_W}
    ys = sorted(ys)

    shell = _Shell()
    mats = [_material(nm) for nm in _MATS]

    # ---- roadway surface ---------------------------------------------------
    shell.tag = 'road'
    for (ya, yb) in _cells(ys, y0r, y1r):
        in_med_rows = med_on and m0 - 1e-9 <= ya and yb <= m1 + 1e-9
        za, zb = z_road(ya), z_road(yb)
        for (a, b) in _cells(xs, 0.0, L):
            cx, cy = (a + b) / 2.0, (ya + yb) / 2.0
            if hole and hx0 < cx < hx1 and hy0 < cy < hy1:
                continue
            if in_med_rows and mx0 - 1e-9 <= a and b <= mx1 + 1e-9:
                continue
            shell.quad((a, ya, za), (b, ya, za), (b, yb, zb), (a, yb, zb),
                       M_ASPHALT)

    # ---- gutter pans + curbs (both sides) ----------------------------------
    def curb_top_z(sg, x):
        for (msg, a, b) in mouths:
            if msg != sg or not (a - 1e-9 < x < b + 1e-9):
                continue
            if x < a + 0.7:
                return CURB_H - (CURB_H - 0.03) * (x - a) / 0.7
            if x > b - 0.7:
                return CURB_H - (CURB_H - 0.03) * (b - x) / 0.7
            return 0.03
        return CURB_H

    for sg, y_re, y_cf in ((-1, y0r, y_cf_s), (1, y1r, y_cf_n)):
        shell.tag = 'gutter'
        for (a, b) in _cells(xs, 0.0, L):
            pts = [(a, y_re, Z_E), (b, y_re, Z_E),
                   (b, y_cf, 0.0), (a, y_cf, 0.0)]
            if sg < 0:
                pts.reverse()
            shell.quad(*pts, M_CONCRETE)
        shell.tag = 'curb'
        y_bk = y_cf + sg * CURB_W
        for (a, b) in _cells(xs, 0.0, L):
            cta, ctb = curb_top_z(sg, a), curb_top_z(sg, b)
            pts = [(a, y_cf, cta), (b, y_cf, ctb),
                   (b, y_cf, 0.0), (a, y_cf, 0.0)]
            if sg > 0:
                pts.reverse()
            shell.quad(*pts, M_CONCRETE)               # face
            pts = [(a, y_cf, cta), (a, y_bk, cta),
                   (b, y_bk, ctb), (b, y_cf, ctb)]
            if sg > 0:
                pts.reverse()
            shell.quad(*pts, M_CONCRETE)               # top
            pts = [(a, y_bk, cta), (a, y_bk, 0.0),
                   (b, y_bk, 0.0), (b, y_bk, ctb)]
            if sg > 0:
                pts.reverse()
            shell.quad(*pts, M_CONCRETE)               # back

    # ---- median island (palms form): FLAT top, no sunken ring --------------
    if med_on:
        shell.tag = 'median'
        zt = z_pk + 0.16
        i0, i1 = m0 + CURB_W, m1 - CURB_W
        ix0, ix1 = mx0 + CURB_W, mx1 - CURB_W
        for (a, b) in _cells(xs, mx0, mx1):
            shell.quad((a, m0, z_pk), (b, m0, z_pk), (b, m0, zt),
                       (a, m0, zt), M_CONCRETE)        # S face (-y)
            shell.quad((a, m1, zt), (b, m1, zt), (b, m1, z_pk),
                       (a, m1, z_pk), M_CONCRETE)      # N face (+y)
        for xe, sgn2 in ((mx0, -1), (mx1, 1)):
            for (ya, yb) in ((m0, i0), (i0, i1), (i1, m1)):
                pts = [(xe, ya, z_pk), (xe, yb, z_pk), (xe, yb, zt),
                       (xe, ya, zt)]
                if sgn2 < 0:
                    pts.reverse()
                shell.quad(*pts, M_CONCRETE)           # end faces
        for (a, b) in _cells(xs, mx0, mx1):            # flat top: border
            for (ya, yb) in ((m0, i0), (i1, m1)):
                shell.quad((a, ya, zt), (b, ya, zt), (b, yb, zt),
                           (a, yb, zt), M_CONCRETE)
        for (a, b) in ((mx0, ix0), (ix1, mx1)):        # border end caps
            shell.quad((a, i0, zt), (b, i0, zt), (b, i1, zt),
                       (a, i1, zt), M_CONCRETE)
        for (a, b) in _cells(xs, ix0, ix1):            # flush soil bed
            shell.quad((a, i0, zt), (b, i0, zt), (b, i1, zt),
                       (a, i1, zt), M_SOIL, 'median')

    # ---- sinkhole ----------------------------------------------------------
    if hole:
        shell.tag = 'sinkhole'
        z_lip = -0.35
        z_soil = -0.95
        z_fl = -2.1 if interior_on else -1.15
        l1 = (hx0 + 0.18, hx1 - 0.18, hy0 + 0.18, hy1 - 0.18)
        l2 = (hx0 + 0.33, hx1 - 0.33, hy0 + 0.33, hy1 - 0.33) \
            if interior_on else l1

        def ring(outer, inner, z, mat):
            (ox0, ox1, oy0, oy1) = outer
            (nx0, nx1, ny0, ny1) = inner
            for (a, b) in _cells(xs, ox0, ox1):
                for (ya, yb) in ((oy0, ny0), (ny1, oy1)):
                    shell.quad((a, ya, z), (b, ya, z), (b, yb, z),
                               (a, yb, z), mat)
            for (ya, yb) in _cells(ys, ny0, ny1):
                for (a, b) in ((ox0, nx0), (nx1, ox1)):
                    shell.quad((a, ya, z), (b, ya, z), (b, yb, z),
                               (a, yb, z), mat)

        def drops(rect, ztop, zbot, ztop_f=None):
            (rx0, rx1, ry0, ry1) = rect
            zt2 = ztop_f if ztop_f else (lambda _y: ztop)
            for (a, b) in _cells(xs, rx0, rx1):
                shell.quad((a, ry0, zt2(ry0)), (b, ry0, zt2(ry0)),
                           (b, ry0, zbot), (a, ry0, zbot), M_CONCRETE)
                shell.quad((b, ry1, zt2(ry1)), (a, ry1, zt2(ry1)),
                           (a, ry1, zbot), (b, ry1, zbot), M_CONCRETE)
            for (ya, yb) in _cells(ys, ry0, ry1):
                shell.quad((rx0, yb, zt2(yb)), (rx0, ya, zt2(ya)),
                           (rx0, ya, zbot), (rx0, yb, zbot), M_CONCRETE)
                shell.quad((rx1, ya, zt2(ya)), (rx1, yb, zt2(yb)),
                           (rx1, yb, zbot), (rx1, ya, zbot), M_CONCRETE)

        drops((hx0, hx1, hy0, hy1), None, z_lip, ztop_f=z_road)
        ring((hx0, hx1, hy0, hy1), l1, z_lip, M_CONCRETE)
        if interior_on:
            drops(l1, z_lip, z_soil)
            ring(l1, l2, z_soil, M_SOIL)
            drops(l2, z_soil, z_fl)
        else:
            drops(l1, z_lip, z_fl)
        for (a, b) in _cells(xs, l2[0], l2[1]):
            for (ya, yb) in _cells(ys, l2[2], l2[3]):
                shell.quad((a, ya, z_fl), (b, ya, z_fl), (b, yb, z_fl),
                           (a, yb, z_fl), M_SOIL)
        for _r in range(3):
            rx = l2[0] + 0.2 + rng.random() * max(l2[1] - l2[0] - 0.9, 0.1)
            ry = l2[2] + 0.2 + rng.random() * max(l2[3] - l2[2] - 0.8, 0.1)
            rs = 0.25 + rng.random() * 0.3
            _box(shell, (rx, ry, z_fl - 0.02),
                 (rx + rs, ry + rs * 0.8, z_fl + rs * 0.6), M_CONCRETE)
        if interior_on:
            pr, (pcy, pcz) = 0.20, ((hy0 + hy1) / 2.0, -1.45)
            oct8 = [(0.9239, 0.3827), (0.3827, 0.9239),
                    (-0.3827, 0.9239), (-0.9239, 0.3827),
                    (-0.9239, -0.3827), (-0.3827, -0.9239),
                    (0.3827, -0.9239), (0.9239, -0.3827)]
            pa, pb = l1[0] - 0.02, l1[1] + 0.02
            for i8 in range(8):
                (u0, v0) = oct8[i8]
                (u1, v1) = oct8[(i8 + 1) % 8]
                shell.quad((pb, pcy + u0 * pr, pcz + v0 * pr),
                           (pa, pcy + u0 * pr, pcz + v0 * pr),
                           (pa, pcy + u1 * pr, pcz + v1 * pr),
                           (pb, pcy + u1 * pr, pcz + v1 * pr),
                           M_METAL, 'sinkhole')

    # ---- interior mode: sliced-section strata end caps ---------------------
    if interior_on:
        shell.tag = 'road'
        strata = [(0.0, 0.12, M_ASPHALT), (0.12, 0.45, M_CONCRETE),
                  (0.45, 1.05, M_SOIL)]
        for xe, sgn2 in ((0.0, -1), (L, 1)):
            for (ya, yb) in _cells(ys, y0r, y1r):
                za, zb = z_road(ya), z_road(yb)
                for (d0, d1, m9) in strata:
                    pts = [(xe, ya, za - d0), (xe, yb, zb - d0),
                           (xe, yb, zb - d1), (xe, ya, za - d1)]
                    if sgn2 > 0:
                        pts.reverse()
                    shell.quad(*pts, m9)
            for y_re, y_cf in ((y0r, y_cf_s), (y1r, y_cf_n)):
                north = y_cf > 0.0
                for (d0, d1, m9) in strata:
                    pts = [(xe, y_re, Z_E - d0), (xe, y_cf, -d0),
                           (xe, y_cf, -d1), (xe, y_re, Z_E - d1)]
                    if (sgn2 > 0) == north:
                        pts.reverse()
                    shell.quad(*pts, m9 if d0 > 0.2 else M_CONCRETE,
                               'gutter')
                sg9 = 1 if north else -1
                ct = curb_top_z(sg9, xe)
                y_bk = y_cf + sg9 * CURB_W
                for (d0, d1) in ((-ct, 0.0), (0.0, 0.12), (0.12, 0.45),
                                 (0.45, 1.05)):
                    pts = [(xe, y_cf, -d0), (xe, y_bk, -d0),
                           (xe, y_bk, -d1), (xe, y_cf, -d1)]
                    if (sgn2 > 0) == north:
                        pts.reverse()
                    shell.quad(*pts, M_CONCRETE if d1 < 0.5 else M_SOIL,
                               'curb')

    # ---- markings (normal-street vocabulary; overlays +3 mm) ---------------
    shell.tag = 'paint'

    def solid(y_a, y_b, mat, x_a=0.5, x_b=None):
        _strip(shell, xs, x_a, L - 0.5 if x_b is None else x_b,
               y_a, y_b, z_road, mat, 'paint', 0.003)

    def dashes(y_l, mat):
        # standard broken line: 3 m dash / 9 m gap, 0.12 wide
        x = 1.0 + rng.random() * 3.0
        while x + 1.2 < L - 1.0:
            dl = 3.0
            if rng.random() < wear * 0.30:
                dl = 1.2 + rng.random() * 1.2
            if rng.random() >= wear * 0.55:
                d1 = min(x + dl, L - 1.0)
                if not (hole and x < hx1 and d1 > hx0 and
                        hy0 - 0.1 < y_l < hy1 + 0.1):
                    _strip(shell, xs, x, d1, y_l - 0.06, y_l + 0.06,
                           z_road, mat, 'paint', 0.003)
            x += 12.0

    if p["edge_lines"]:                      # solid white edge lines
        solid(y0r + 0.10, y0r + 0.22, M_PAINT_W)
        solid(y1r - 0.22, y1r - 0.10, M_PAINT_W)
    for k in range(1, n_s):                  # dashed white lane lines
        dashes(y0r + k * lw, M_PAINT_W)
    for k in range(1, n_n):
        dashes(m1 + k * lw, M_PAINT_W)
    if med == 'none':
        cl = p["centerline"]
        if cl == 'double_yellow':
            solid(m0 - 0.15, m0 - 0.05, M_PAINT_Y)
            solid(m0 + 0.05, m0 + 0.15, M_PAINT_Y)
        elif cl == 'solid_yellow':
            solid(m0 - 0.06, m0 + 0.06, M_PAINT_Y)
        else:                                # dashed_yellow
            dashes(m0 + 0.06, M_PAINT_Y)
    elif med == 'turn':                      # solid outside, dash inside
        for y_l, sgn2 in ((m0, 1), (m1, -1)):
            solid(y_l + sgn2 * 0.05, y_l + sgn2 * 0.15, M_PAINT_Y)
            dashes(y_l + sgn2 * 0.40, M_PAINT_Y)

    cw_bands = []
    if p["crosswalks"] in ('near', 'both'):
        cw_bands.append((1.4, 4.1, 1))
    if p["crosswalks"] in ('far', 'both') and L > 14.0:
        cw_bands.append((L - 4.1, L - 1.4, -1))

    def cw_strip(xa, xb, y_a, y_b):
        for (ca, cb) in ((max(y_a, y0r + 0.25), min(y_b, m0)),
                         (max(y_a, m0), min(y_b, m1)),
                         (max(y_a, m1), min(y_b, y1r - 0.25))):
            if cb - ca < 0.04:
                continue
            if med_on and ca >= m0 - 1e-9 and cb <= m1 + 1e-9:
                continue
            _strip(shell, xs, xa, xb, ca, cb, z_road, M_PAINT_W,
                   'paint', 0.003)

    for (xa, xb, ap) in cw_bands:
        if p["crosswalk_style"] == 'transverse':
            # two solid transverse lines bounding the crossing
            cw_strip(xa, xa + 0.15, y0r + 0.25, y1r - 0.25)
            cw_strip(xb - 0.15, xb, y0r + 0.25, y1r - 0.25)
        else:                                # continental piano keys
            y = y0r + 0.30
            while y + 0.42 < y1r - 0.25:
                if rng.random() >= wear * 0.35:
                    cw_strip(xa + 0.15, xb - 0.15, y, y + 0.42)
                y += 1.0
        bx = (xa - 1.25, xa - 0.80) if ap > 0 else (xb + 0.80, xb + 1.25)
        if 0.0 < bx[0] and bx[1] < L:        # approach stop bar
            ys_bar = (y0r + 0.25, m0 - 0.10) if ap > 0 \
                else (m1 + 0.10, y1r - 0.25)
            _strip(shell, xs, bx[0], bx[1], ys_bar[0], ys_bar[1],
                   z_road, M_PAINT_W, 'paint', 0.003)

    # ---- asphalt patches ---------------------------------------------------
    shell.tag = 'patches'
    for _k in range(round(p["patches"] * 9)):
        pw2 = 1.4 + rng.random() * 2.4
        pd2 = 0.9 + rng.random() * 1.3
        px2 = 0.4 + rng.random() * max(L - pw2 - 0.8, 0.1)
        side = (y0r + 0.15, m0 - 0.15) if rng.random() < 0.5 \
            else (m1 + 0.15, y1r - 0.15)
        if side[1] - side[0] < 1.0:
            continue
        py2 = side[0] + rng.random() * max(side[1] - side[0] - pd2, 0.05)
        py3 = min(py2 + pd2, side[1])
        if hole and px2 < hx1 and px2 + pw2 > hx0 and \
                py2 < hy1 and py3 > hy0:
            continue
        _strip(shell, xs, px2, px2 + pw2, py2, py3, z_road,
               M_PATCH, 'patches', 0.0015)

    # ---- sidewalk slabs + alley aprons (independent islands) ---------------
    walk = _Shell()
    walk.tag = 'sidewalk'
    for sg, y_cf in ((-1, y_cf_s), (1, y_cf_n)):
        y_in = y_cf + sg * (CURB_W + GAP)
        y_out = y_in + sg * sw
        ya, yb = min(y_in, y_out), max(y_in, y_out)
        n_sl = max(2, round(L / SLAB_L))
        pitch = L / n_sl
        sk = 0.11
        for i9 in range(n_sl):
            a = i9 * pitch + GAP
            b = (i9 + 1) * pitch - GAP
            if any(msg == sg and a < mb and b > ma
                   for (msg, ma, mb) in mouths):
                continue
            lift = heave * rng.random() * 0.045
            tilt = heave * (rng.random() - 0.5) * 0.06
            if rng.random() < 0.5:
                z00 = z01 = Z_WALK + lift - tilt / 2.0
                z10 = z11 = Z_WALK + lift + tilt / 2.0
            else:
                z00 = z10 = Z_WALK + lift - tilt / 2.0
                z01 = z11 = Z_WALK + lift + tilt / 2.0
            walk.quad((a, ya, z00), (b, ya, z10), (b, yb, z11),
                      (a, yb, z01), M_CONCRETE)
            walk.quad((a, ya, sk), (b, ya, sk), (b, ya, z10),
                      (a, ya, z00), M_CONCRETE)
            walk.quad((b, yb, sk), (a, yb, sk), (a, yb, z01),
                      (b, yb, z11), M_CONCRETE)
            walk.quad((a, yb, sk), (a, ya, sk), (a, ya, z00),
                      (a, yb, z01), M_CONCRETE)
            walk.quad((b, ya, sk), (b, yb, sk), (b, yb, z11),
                      (b, ya, z10), M_CONCRETE)
    for (msg, ma, mb) in mouths:
        y_cf = y_cf_s if msg < 0 else y_cf_n
        y_in = y_cf + msg * (CURB_W + GAP)
        y_out = y_in + msg * sw
        z_hi, z_lo = Z_WALK, 0.045
        a, b = ma + GAP, mb - GAP
        if msg < 0:
            ya2, za2, yb2, zb2 = y_out, z_hi, y_in, z_lo
        else:
            ya2, za2, yb2, zb2 = y_in, z_lo, y_out, z_hi
        walk.quad((a, ya2, za2), (b, ya2, za2), (b, yb2, zb2),
                  (a, yb2, zb2), M_CONCRETE)
        for (xe, flip) in ((a, False), (b, True)):
            pts = [(xe, ya2, za2), (xe, yb2, zb2), (xe, yb2, 0.02),
                   (xe, ya2, 0.02)]
            if flip:
                pts.reverse()
            walk.quad(*pts, M_CONCRETE)
        walk.quad((a, ya2, 0.02), (b, ya2, 0.02), (b, ya2, za2),
                  (a, ya2, za2), M_CONCRETE)
        walk.quad((b, yb2, 0.02), (a, yb2, 0.02), (a, yb2, zb2),
                  (b, yb2, zb2), M_CONCRETE)

    # ---- story dressing ----------------------------------------------------
    story = _Shell()
    story.tag = 'story'
    any_story = False
    if scar:
        any_story = True
        for (ya, yb) in ((y0r + 0.05, m0), (m1, y1r - 0.05)):
            _strip(story, xs, scar[0], scar[1], ya, yb, z_road,
                   M_PATCH, 'story', 0.0018)
        # FLUSH steel base plates where the bollards were sheared off
        for rx in (scar[0] + 0.7, scar[1] - 0.7):
            y = y0r + 0.6
            while y < y1r - 0.4:
                if not (m1 > m0 and m0 - 0.2 < y < m1 + 0.2):
                    zb2 = z_road(y)
                    _box(story, (rx - 0.12, y - 0.12, zb2 - 0.02),
                         (rx + 0.12, y + 0.12, zb2 + 0.012), M_METAL)
                y += 1.2
    if p["sand_lane"]:
        any_story = True
        bx0, bx1 = L * 0.12, L * 0.88
        prof = [(y_cf_s - 0.10, 0.165, 0.10),
                (y_cf_s, 0.157, 0.22),
                (y_cf_s + 0.30, 0.012, 0.30),
                (y0r + 0.6, z_road(y0r + 0.6) + 0.004, 0.15),
                (y0r + lw * 0.9, z_road(y0r + lw * 0.9) + 0.004, 0.0)]
        nx = 8

        def fac(i9):
            return min(1.0, min(i9, nx - i9) * 0.55 + 0.12)

        for i9 in range(nx):
            a = bx0 + (bx1 - bx0) * i9 / nx
            b = bx0 + (bx1 - bx0) * (i9 + 1) / nx
            fa, fb = fac(i9), fac(i9 + 1)
            for ((ya, ba, ha), (yb, bb, hb)) in zip(prof, prof[1:]):
                story.quad((a, ya, ba + ha * fa), (b, ya, ba + ha * fb),
                           (b, yb, bb + hb * fb), (a, yb, bb + hb * fa),
                           M_SAND)
        for (xe, flip) in ((bx0, False), (bx1, True)):
            f9 = fac(0)
            for ((ya, ba, ha), (yb, bb, hb)) in zip(prof, prof[1:]):
                pts = [(xe, ya, ba + ha * f9), (xe, yb, bb + hb * f9),
                       (xe, yb, bb), (xe, ya, ba)]
                if flip:
                    pts.reverse()
                story.quad(*pts, M_SAND)
    if p["protest_stain"]:
        any_story = True
        scx = L * (0.35 + rng.random() * 0.3)
        scy = (m1 + y1r) / 2.0 if n_n else (y0r + m0) / 2.0
        for k9 in range(4):
            r9 = 0.7 + rng.random() * 1.1
            ox = scx + (rng.random() - 0.5) * 2.4
            oy = min(max(scy + (rng.random() - 0.5) * 1.6, y0r + r9 + 0.1),
                     y1r - r9 - 0.1)
            if m0 - r9 - 0.1 < oy < m1 + r9 + 0.1:
                continue
            ch = r9 * 0.42
            off9 = 0.0012 + k9 * 0.0004

            def zb9(y9):
                return z_road(y9) + off9

            g9 = [ox - r9, ox - r9 + ch, ox + r9 - ch, ox + r9]
            h9 = [oy - r9, oy - r9 + ch, oy + r9 - ch, oy + r9]
            for i9 in range(3):
                for j9 in range(3):
                    c4 = [(g9[i9], h9[j9]), (g9[i9 + 1], h9[j9]),
                          (g9[i9 + 1], h9[j9 + 1]), (g9[i9], h9[j9 + 1])]
                    if (i9, j9) == (0, 0):
                        c4[0] = (g9[0] + ch * 0.7, h9[0] + ch * 0.7)
                    if (i9, j9) == (2, 0):
                        c4[1] = (g9[3] - ch * 0.7, h9[0] + ch * 0.7)
                    if (i9, j9) == (2, 2):
                        c4[2] = (g9[3] - ch * 0.7, h9[3] - ch * 0.7)
                    if (i9, j9) == (0, 2):
                        c4[3] = (g9[0] + ch * 0.7, h9[3] - ch * 0.7)
                    story.quad((c4[0][0], c4[0][1], zb9(c4[0][1])),
                               (c4[1][0], c4[1][1], zb9(c4[1][1])),
                               (c4[2][0], c4[2][1], zb9(c4[2][1])),
                               (c4[3][0], c4[3][1], zb9(c4[3][1])),
                               M_SCORCH)

    # ---- apply the join-preserving transform, emit objects -----------------
    out = []
    for sh, nm in ((shell, "LA_Street_Road"), (walk, "LA_Street_Walk"),
                   (story, "LA_Street_Story")):
        if sh is story and not any_story:
            sh.bm.free()
            continue
        for v in sh.bm.verts:
            v.co = xform(v.co)
        out.append(sh.to_object(nm, mats))
    return out

# ===========================================================================
# INTERSECTION
# ===========================================================================
#
# Plan geometry (all at z levels shared with la_street; pad flat at Z_E):
#
#             |  arm k2 |
#             | r1      |
#             | apron   |
#          ___|_r0______|___
#     arc /   .          \
#   curb |    .   PAD     |  <- boundary: mouth chords (road rows) +
#  return \   .  (Z_E)   /      curb-inner segments (u = y0r / y1r) +
#          `--+---------'       inner-gutter arcs, ccw; EVEN vert count
#      wedge  |  arm k  |       (a quad-only disk needs an even boundary),
#      walk   |         |       filled: boundary -> scaled ring -> zipper.
#
# Fillets: between arm k's LEFT curb line and arm k+1's RIGHT curb line.
# wedge < 180 - 8: concave corner (center outside the pavement, sidewalk
# bulb toward the center, gutter arc at rc + GUT_W). wedge > 180 + 8:
# convex corner (a veer's outside bend -- mirrored offsets, gutter at
# rc - GUT_W). |wedge - 180| <= 8: STRAIGHT SEAM -- the through curb of a
# tee: arm k's left-side strips extend across, arm k2's right side skips.

def _arm_bearings(form, skew):
    """Outgoing arm bearings in degrees for each intersection form."""
    if form == 'veer':
        if abs(skew) < 9.0:           # a straight 2-way is just la_street
            skew = math.copysign(9.0, skew if skew else 1.0)
        return [0.0, 180.0 + skew]
    if form == 'tee':
        return [0.0, 90.0 + skew, 180.0]
    if form == 'cross':
        return [0.0, 90.0 + skew, 180.0, 270.0]
    return [0.0, 45.0 + skew, 90.0, 180.0, 270.0]     # five
#: Canonical intersection pad topologies, authored by the user as the
#: scene objects 4Intersection_Topo / 5Intersection_Topo and embedded
#: here so the operator is standalone. The generator DEFORMS these quad
#: layouts (poles pre-placed at the corner turns) onto the parametric
#: arms; edge-loop families carry the subdivision counts.
_T4_VERTS = [(-0.3643, -0.2601), (0.3222, 2.0), (1.0, 2.0), (2.0, -1.0),
    (-0.3244, -2.0), (1.0, -2.0), (-2.0, -1.0), (-2.0, 0.3014), (-0.3557,
    2.0), (-2.0, -0.2601), (2.0, 0.3122), (0.3613, 0.304), (1.0, -0.2546),
    (2.0, -0.2546), (-0.4609, 0.2701), (-0.4128, 0.0109), (-0.1324, 0.3018),
    (-0.3945, 0.3346), (0.9982, 0.344), (0.7992, 0.3059), (1.0, 0.106),
    (1.0506, 0.2918), (1.0295, -0.9949), (1.0, -0.7475), (0.7475, -1.0),
    (0.9604, -1.0641), (-0.3278, -1.1157), (-0.0428, -1.0), (0.3111, -1.0),
    (-0.3373, -0.7205), (-0.4176, -1.0258), (0.8128, 2.0), (-0.0856, 2.0),
    (2.0, -0.724), (0.768, -2.0), (0.2909, -2.0), (-0.0787, -2.0), (-2.0,
    -0.7309), (2.0, 0.133), (-2.0, -0.0121)]
_T4_FACES = [(11, 16, 15, 0), (14, 15, 16, 17), (8, 17, 16, 32), (18, 19, 20,
    21), (10, 21, 20, 38), (0, 28, 12, 11), (1, 11, 19, 31), (0, 15, 39, 9),
    (22, 23, 24, 25), (13, 12, 23, 33), (27, 28, 0, 29), (12, 28, 24, 23),
    (26, 27, 29, 30), (5, 25, 24, 34), (30, 29, 37, 6), (11, 12, 20, 19), (31,
    19, 18, 2), (32, 16, 11, 1), (33, 23, 22, 3), (34, 24, 28, 35), (35, 28,
    27, 36), (36, 27, 26, 4), (37, 29, 0, 9), (38, 20, 12, 13), (39, 15, 14,
    7)]
_T5_VERTS = [(3.8308, -0.2601), (4.1952, 0.0232), (3.6578, -0.1129), (4.5063,
    -1.0), (3.8579, -0.7205), (3.5559, -0.2601), (2.1952, -0.2601), (3.5784,
    -0.7199), (3.4119, -0.1145), (2.1952, -0.1227), (3.1277, 0.2768), (3.3161,
    0.2718), (2.1952, 0.3014), (1.7593, 1.5968), (1.9131, 1.7025), (2.151,
    1.9182), (2.5296, 2.3592), (3.6369, 0.5843), (3.9325, 0.9285), (4.0192,
    0.2115), (4.2807, 0.6966), (4.6108, 0.5577), (4.332, 0.9781), (4.6598,
    2.289), (4.6824, 0.8717), (3.9992, 1.2), (4.2838, 2.3591), (2.7206,
    2.5685), (4.9882, 2.2124), (5.0881, 0.7969), (5.287, 2.1561), (5.0415,
    0.4785), (5.3026, 0.7879), (5.2441, 0.4791), (5.5524, 2.1062), (5.2458,
    0.2918), (5.1952, 0.106), (6.1952, 0.3122), (6.1952, 0.1123), (6.1952,
    -0.2546), (5.1952, -0.2546), (6.1952, -0.7309), (6.1952, -1.0), (5.1952,
    -0.7475), (5.2247, -0.9949), (5.0503, -0.8924), (4.9426, -1.0), (4.8268,
    -0.6531), (5.184, -1.0356), (5.1555, -1.0641), (5.1952, -2.0), (4.9251,
    -2.0), (4.5035, -2.0), (4.1441, -2.0), (3.8708, -2.0), (4.1523, -1.0),
    (3.8673, -1.1157), (3.7775, -1.0258), (3.5116, -1.0215), (2.1952, -1.0),
    (2.1952, -0.7171)]
_T5_FACES = [(1, 19, 2, 0), (0, 3, 47, 1), (0, 2, 8, 5), (55, 3, 0, 4), (7, 4,
    0, 5), (60, 7, 5, 6), (5, 8, 9, 6), (9, 8, 10, 12), (8, 2, 11, 10), (13,
    10, 11, 14), (11, 17, 15, 14), (17, 18, 16, 15), (17, 19, 20, 18), (11, 2,
    19, 17), (21, 20, 19, 1), (25, 18, 20, 22), (22, 20, 21, 24), (26, 25, 22,
    23), (23, 22, 24, 28), (16, 18, 25, 27), (28, 24, 29, 30), (24, 21, 31,
    29), (30, 29, 32, 34), (29, 31, 33, 32), (33, 31, 36, 35), (37, 35, 36,
    38), (38, 36, 40, 39), (39, 40, 43, 41), (41, 43, 44, 42), (44, 43, 45,
    48), (40, 47, 45, 43), (48, 45, 46, 49), (45, 47, 3, 46), (50, 49, 46,
    51), (51, 46, 3, 52), (52, 3, 55, 53), (53, 55, 56, 54), (56, 55, 4, 57),
    (57, 4, 7, 58), (58, 7, 60, 59), (1, 47, 40, 21), (21, 40, 36, 31)]
_T4_MOUTHS = [[8, 32, 1, 31, 2], [10, 38, 13, 33, 3],
              [5, 34, 35, 36, 4], [6, 37, 9, 39, 7]]
_T5_MOUTHS = [[12, 9, 6, 60, 59], [54, 53, 52, 51, 50],
              [42, 41, 39, 38, 37], [34, 30, 28, 23, 26],
              [27, 16, 15, 14, 13]]
class _PadTopo:
    """A canonical pad topology prepared for deformation: boundary loop
    (ccw), mouth runs oriented right-to-left, ccw arm order with template
    bearings, corner runs between consecutive arms, and the edge-loop
    FAMILIES (opposite edges of each quad chained) that carry subdivision
    counts."""

    def __init__(self, verts, faces, mouths):
        self.verts = verts
        self.faces = faces

        def ek(a, b):
            return (a, b) if a < b else (b, a)

        # edge-loop families via union-find over opposite quad edges
        parent = {}

        def find(x):
            while parent[x] != x:
                parent[x] = parent[parent[x]]
                x = parent[x]
            return x

        for f in faces:
            es = [ek(f[i], f[(i + 1) % 4]) for i in range(4)]
            for e in es:
                parent.setdefault(e, e)
            for (a, b) in ((es[0], es[2]), (es[1], es[3])):
                ra, rb = find(a), find(b)
                if ra != rb:
                    parent[ra] = rb
        self.fam = {e: find(e) for e in parent}

        # ordered boundary loop, normalized ccw
        from collections import defaultdict
        cnt = defaultdict(int)
        for f in faces:
            for i in range(4):
                cnt[ek(f[i], f[(i + 1) % 4])] += 1
        adj = defaultdict(list)
        for (e, c) in cnt.items():
            if c == 1:
                adj[e[0]].append(e[1])
                adj[e[1]].append(e[0])
        loop = [next(iter(adj))]
        prev = None
        while True:
            nxt = [w for w in adj[loop[-1]] if w != prev]
            prev = loop[-1]
            loop.append(nxt[0])
            if loop[-1] == loop[0]:
                loop.pop()
                break
        area2 = sum(verts[loop[i]][0] * verts[loop[(i + 1) % len(loop)]][1] -
                    verts[loop[(i + 1) % len(loop)]][0] * verts[loop[i]][1]
                    for i in range(len(loop)))
        if area2 < 0:
            loop.reverse()
        self.loop = loop

        cx = sum(v[0] for v in verts) / len(verts)
        cy = sum(v[1] for v in verts) / len(verts)
        arms = []
        for mm in mouths:
            a, b = verts[mm[0]], verts[mm[-1]]
            dx, dy = b[0] - a[0], b[1] - a[1]
            for (nx, ny) in ((dy, -dx), (-dy, dx)):
                mx = (a[0] + b[0]) / 2.0 - cx
                my = (a[1] + b[1]) / 2.0 - cy
                if nx * mx + ny * my > 0:
                    brg = math.degrees(math.atan2(ny, nx)) % 360.0
                    # orient right -> left seen facing outward
                    ln = (-ny, nx)
                    if (dx * ln[0] + dy * ln[1]) < 0:
                        mm = list(reversed(mm))
                    arms.append((brg, mm))
                    break
        arms.sort(key=lambda t: t[0])
        self.arm_bearings = [t[0] for t in arms]
        self.arm_mouths = [t[1] for t in arms]

        # corner runs: ccw boundary walk from arm k's LEFT end to arm
        # (k+1)'s RIGHT end
        pos = {v: i for i, v in enumerate(self.loop)}
        self.corners = []
        n = len(self.loop)
        for k in range(len(arms)):
            k2 = (k + 1) % len(arms)
            i0 = pos[self.arm_mouths[k][-1]]
            i1 = pos[self.arm_mouths[k2][0]]
            run = [self.loop[i0]]
            i = i0
            while i != i1:
                i = (i + 1) % n
                run.append(self.loop[i])
            self.corners.append(run)
        self.boundary = set()
        for e, c in cnt.items():
            if c == 1:
                self.boundary.add(e)

        # mean-value-coordinate weights for every interior vert w.r.t.
        # the boundary loop (template geometry; reused on any target)
        self.interior = [i for i in range(len(verts))
                         if i not in pos]
        self.mvc = {}
        for vi in self.interior:
            p = verts[vi]
            w = []
            for j in range(n):
                a = verts[self.loop[(j - 1) % n]]
                b = verts[self.loop[j]]
                c = verts[self.loop[(j + 1) % n]]
                rb = math.hypot(b[0] - p[0], b[1] - p[1])

                def half_tan(q0, q1):
                    v0 = (q0[0] - p[0], q0[1] - p[1])
                    v1 = (q1[0] - p[0], q1[1] - p[1])
                    d0 = math.hypot(*v0)
                    d1 = math.hypot(*v1)
                    dot = v0[0] * v1[0] + v0[1] * v1[1]
                    crs = v0[0] * v1[1] - v0[1] * v1[0]
                    ang = math.atan2(abs(crs), dot)
                    del d0, d1
                    return math.tan(ang / 2.0)

                w.append((half_tan(a, b) + half_tan(b, c)) / max(rb, 1e-9))
            s9 = sum(w)
            self.mvc[vi] = [x / s9 for x in w]


_PAD4 = _PadTopo(_T4_VERTS, [list(f) for f in _T4_FACES], _T4_MOUTHS)
_PAD5 = _PadTopo(_T5_VERTS, [list(f) for f in _T5_FACES], _T5_MOUTHS)


def _coons_rect(bottom, top, left, right):
    """Rectangular bilinear Coons grid from 4 border polylines (2D).
    bottom/top run u (len nu+1), left/right run v (len nv+1); corners
    must agree: bottom[0]=left[0], bottom[-1]=right[0], top[0]=left[-1],
    top[-1]=right[-1]."""
    nu = len(bottom) - 1
    nv = len(left) - 1
    grid = []
    for j in range(nv + 1):
        v = j / nv
        row = []
        for i in range(nu + 1):
            u = i / nu
            x = ((1 - v) * bottom[i][0] + v * top[i][0] +
                 (1 - u) * left[j][0] + u * right[j][0] -
                 ((1 - u) * (1 - v) * bottom[0][0] +
                  u * (1 - v) * bottom[nu][0] +
                  (1 - u) * v * top[0][0] + u * v * top[nu][0]))
            y = ((1 - v) * bottom[i][1] + v * top[i][1] +
                 (1 - u) * left[j][1] + u * right[j][1] -
                 ((1 - u) * (1 - v) * bottom[0][1] +
                  u * (1 - v) * bottom[nu][1] +
                  (1 - u) * v * top[0][1] + u * v * top[nu][1]))
            row.append((x, y))
        grid.append(row)
    return grid
def build_intersection(p, rng):
    """N-way intersection whose pad DEFORMS the user-authored canonical
    quad topologies (_PAD4 / _PAD5): mouth runs land on the arm cross-
    sections, corner runs land on the fillet chains (arclength-sampled,
    G1-continuous segment-arc-segment), interior cage verts follow by
    mean-value coordinates, and every template quad fills with a matched
    Coons grid -- the poles stay exactly where the template author put
    them. cross/five map 1:1; tee maps the 4-way template with a PHANTOM
    fourth arm flattened onto the straight back curb; veer (n = 2) is a
    single rectangular through-grid. Arm interfaces carry the straight-
    street cross-section, so la_street segments plug on. Both modes emit
    the same geometry (open pavement kit)."""
    del rng
    lanes = p["lanes"]
    lw = p["lane_width"]
    sw = p["sidewalk_width"]
    apron = p["apron_length"]
    rc = p["corner_radius"]
    n_s, n_n, y0r, m0, m1, y1r, z_pk, z_road = _xsection(lanes, lw, 'none')
    WL = y1r + GUT_W
    WR = -y0r + GUT_W
    form = p["form"]
    bear = sorted(b % 360.0 for b in _arm_bearings(form, p["skew"]))
    phantom = None
    if form == 'tee':
        bear = sorted(bear + [270.0])
        phantom = bear.index(270.0)
    n_arm = len(bear)
    mats = [_material(nm) for nm in _MATS]

    def frame(deg):
        r = math.radians(deg)
        return ((math.cos(r), math.sin(r)),
                (-math.sin(r), math.cos(r)))

    frames = [frame(b) for b in bear]

    def pt(k, t, u, z):
        d, nl = frames[k]
        return (t * d[0] + u * nl[0], t * d[1] + u * nl[1], z)

    def lerp2(a, b, f):
        return (a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f)

    def q2d(shell9, a, b, c, d, mat, tag=None):
        ar = (b[0] - a[0]) * (c[1] - a[1]) - (c[0] - a[0]) * (b[1] - a[1])
        pts = [a, b, c, d] if ar > 0 else [d, c, b, a]
        shell9.quad((pts[0][0], pts[0][1], Z_E), (pts[1][0], pts[1][1], Z_E),
                    (pts[2][0], pts[2][1], Z_E), (pts[3][0], pts[3][1], Z_E),
                    mat, tag)

    # ---- fillets between REAL arm pairs (phantom wedges stay straight) ----
    fil = []
    for k in range(n_arm):
        k2 = (k + 1) % n_arm
        wedge = (bear[k2] - bear[k]) % 360.0
        if phantom in (k, k2) or abs(wedge - 180.0) <= 8.0:
            fil.append(None)
            continue
        (da, na), (db, nb) = frames[k], frames[k2]
        pv = 1.0 if wedge < 180.0 else -1.0
        oA, oB = WL + pv * rc, WR + pv * rc
        det = da[0] * db[1] - da[1] * db[0]
        bxv = -(oA * na[0] + oB * nb[0])
        byv = -(oA * na[1] + oB * nb[1])
        t9 = (bxv * db[1] - byv * db[0]) / det
        C = (t9 * da[0] + oA * na[0], t9 * da[1] + oA * na[1])
        Ta = (C[0] - pv * rc * na[0], C[1] - pv * rc * na[1])
        Tb = (C[0] + pv * rc * nb[0], C[1] + pv * rc * nb[1])
        ta = Ta[0] * da[0] + Ta[1] * da[1]
        tb = Tb[0] * db[0] + Tb[1] * db[1]
        a0 = math.atan2(Ta[1] - C[1], Ta[0] - C[0])
        a1 = math.atan2(Tb[1] - C[1], Tb[0] - C[0])
        if pv > 0:
            while a1 >= a0:
                a1 -= 2.0 * math.pi
        else:
            while a1 <= a0:
                a1 += 2.0 * math.pi
        fil.append(dict(C=C, ta=ta, tb=tb, a0=a0, a1=a1, pv=pv))

    r0s = []
    for k in range(n_arm):
        km = (k - 1) % n_arm
        need = 1.0
        if fil[k] is not None:
            need = max(need, fil[k]['ta'])
        if fil[km] is not None:
            need = max(need, fil[km]['tb'])
        r0s.append(need + 0.4)

    # phantom (tee) seam endpoints: the straight back curb runs between
    # the neighbouring real arms' mouth corners; the phantom mouth is its
    # middle portion
    if phantom is not None:
        kp9, kn9 = (phantom - 1) % n_arm, (phantom + 1) % n_arm
        A1p = pt(kp9, r0s[kp9], y1r, 0.0)[:2]
        A2p = pt(kn9, r0s[kn9], y0r, 0.0)[:2]
        ph_r = lerp2(A1p, A2p, 0.32)      # phantom mouth right end
        ph_l = lerp2(A1p, A2p, 0.68)      # phantom mouth left end

    # ---- chain geometry (piecewise seg/arc, G1 continuous) -----------------
    def mk_chain(k):
        """Pieces + total length of the wedge-k boundary chain A1 -> A2."""
        k2 = (k + 1) % n_arm
        A1 = ph_l if k == phantom else pt(k, r0s[k], y1r, 0.0)[:2]
        A2 = ph_r if k2 == phantom else pt(k2, r0s[k2], y0r, 0.0)[:2]
        f9 = fil[k]
        if f9 is None:
            return [('seg', A1, A2)], math.dist(A1, A2)
        rg = rc + f9['pv'] * GUT_W
        T1 = pt(k, f9['ta'], y1r, 0.0)[:2]
        T2 = pt(k2, f9['tb'], y0r, 0.0)[:2]
        pieces = []
        if math.dist(A1, T1) > 1e-6:
            pieces.append(('seg', A1, T1))
        pieces.append(('arc', f9['C'], rg, f9['a0'], f9['a1'], f9['pv']))
        if math.dist(T2, A2) > 1e-6:
            pieces.append(('seg', T2, A2))
        L9 = sum(math.dist(pc[1], pc[2]) if pc[0] == 'seg'
                 else pc[2] * abs(pc[4] - pc[3]) for pc in pieces)
        return pieces, L9

    def chain_at(pieces, L9, f):
        """(position, outward normal) at arclength fraction f."""
        s9 = f * L9
        for pc in pieces:
            pl = (math.dist(pc[1], pc[2]) if pc[0] == 'seg'
                  else pc[2] * abs(pc[4] - pc[3]))
            if s9 > pl + 1e-9:
                s9 -= pl
                continue
            if pc[0] == 'seg':
                (_t, a, b) = pc
                d9 = math.dist(a, b)
                q = lerp2(a, b, s9 / max(d9, 1e-9))
                t9 = ((b[0] - a[0]) / d9, (b[1] - a[1]) / d9)
                return q, (t9[1], -t9[0])
            (_t, C, rg, a0, a1, pv) = pc
            ang = a0 + (a1 - a0) * (s9 / pl)
            q = (C[0] + rg * math.cos(ang), C[1] + rg * math.sin(ang))
            n9 = (pv * (C[0] - q[0]) / rg, pv * (C[1] - q[1]) / rg)
            return q, n9
        pc = pieces[-1]
        if pc[0] == 'seg':
            a, b = pc[1], pc[2]
            d9 = max(math.dist(a, b), 1e-9)
            t9 = ((b[0] - a[0]) / d9, (b[1] - a[1]) / d9)
            return pc[2], (t9[1], -t9[0])
        (_t, C, rg, a0, a1, pv) = pc
        q = (C[0] + rg * math.cos(a1), C[1] + rg * math.sin(a1))
        return q, (pv * (C[0] - q[0]) / rg, pv * (C[1] - q[1]) / rg)

    # ---- VEER: rectangular through-grid (already a clean quad grid) --------
    shell = _Shell()
    walk = _Shell()
    chain_strips = []          # (samples [(pos, nrm)], slabbed walk?)

    if n_arm == 2:
        M2 = 2 * max(1, round(lanes / 2)) * 2
        rows_v = [y0r + (y1r - y0r) * j / M2 for j in range(M2 + 1)]
        mouthv = [[pt(k, r0s[k], y, Z_E)[:2] for y in rows_v]
                  for k in range(2)]
        ch_p = [mk_chain(k) for k in range(2)]
        nq2 = max(4, int(round(max(c[1] for c in ch_p) / 1.5)) // 2 * 2)
        chs = []
        for (pieces, L9) in ch_p:
            chs.append([chain_at(pieces, L9, j / nq2) for j in range(nq2 + 1)])
        bot = mouthv[0]
        top = [mouthv[1][M2 - j] for j in range(M2 + 1)]
        lef = [q for (q, _n) in reversed(chs[1])]
        rig = [q for (q, _n) in chs[0]]
        thru = _coons_rect(bot, top, lef, rig)
        _emit_grid(shell, thru, q2d, M_ASPHALT)
        chain_strips = [chs[0], chs[1]]
        rows_by_arm = [rows_v, rows_v]
    else:
        topo = _PAD4 if n_arm == 4 else _PAD5
        # cyclic alignment: match wedge-angle patterns (sharp template
        # corners must land on the sharp target wedges)
        wt = [(bear[(k + 1) % n_arm] - bear[k]) % 360.0
              for k in range(n_arm)]
        wp = [(topo.arm_bearings[(i + 1) % n_arm] -
               topo.arm_bearings[i]) % 360.0 for i in range(n_arm)]
        shift = min(range(n_arm), key=lambda s9: sum(
            abs(wp[(k + s9) % n_arm] - wt[k]) for k in range(n_arm)))

        def tmap(k):
            return (k + shift) % n_arm

        # ---- per-family subdivision counts ---------------------------------
        s = max(1, round(lanes / 2))
        cnt = {}
        for k in range(n_arm):
            mm = topo.arm_mouths[tmap(k)]
            for j in range(4):
                fm = topo.fam[tuple(sorted((mm[j], mm[j + 1])))]
                cnt[fm] = max(cnt.get(fm, 1), s)
        chain_geo = [mk_chain(k) for k in range(n_arm)]
        for k in range(n_arm):
            run = topo.corners[tmap(k)]
            E9 = len(run) - 1
            dem = max(1, int(round(chain_geo[k][1] / E9 / 1.5)))
            for j in range(E9):
                fm = topo.fam[tuple(sorted((run[j], run[j + 1])))]
                cnt[fm] = max(cnt.get(fm, 1), dem)

        def ecnt(a, b):
            return cnt.get(topo.fam[tuple(sorted((a, b)))], 1)

        # ---- boundary polylines + cage vert targets ------------------------
        edge_poly = {}     # (a, b) sorted key -> polyline a-first
        cage = {}

        def put_edge(a, b, poly):
            if a < b:
                edge_poly[(a, b)] = poly
            else:
                edge_poly[(b, a)] = list(reversed(poly))

        rows_by_arm = []
        for k in range(n_arm):
            mm = topo.arm_mouths[tmap(k)]
            Mk = sum(ecnt(mm[j], mm[j + 1]) for j in range(4))
            if k == phantom:
                # phantom mouth: middle of the straight seam between the
                # neighbouring real arms' corners
                kp, kn = (k - 1) % n_arm, (k + 1) % n_arm
                A1p = pt(kp, r0s[kp], y1r, 0.0)[:2]
                A2p = pt(kn, r0s[kn], y0r, 0.0)[:2]
                mpos = [lerp2(A1p, A2p, 0.32 + 0.36 * j / Mk)
                        for j in range(Mk + 1)]
                # A1p is the phantom's RIGHT side (ccw arrives there), so
                # the run is already ordered right -> left
                rows_by_arm.append(None)
            else:
                rows_k = [y0r + (y1r - y0r) * j / Mk for j in range(Mk + 1)]
                mpos = [pt(k, r0s[k], y, Z_E)[:2] for y in rows_k]
                rows_by_arm.append(rows_k)
            cum = 0
            for j in range(4):
                c9 = ecnt(mm[j], mm[j + 1])
                put_edge(mm[j], mm[j + 1], mpos[cum:cum + c9 + 1])
                cage[mm[j]] = mpos[cum]
                cum += c9
            cage[mm[4]] = mpos[cum]
        for k in range(n_arm):
            run = topo.corners[tmap(k)]
            (pieces, L9) = chain_geo[k]
            E9 = len(run) - 1
            cells = [ecnt(run[j], run[j + 1]) for j in range(E9)]
            Ct = sum(cells)
            samples = [chain_at(pieces, L9, j / Ct) for j in range(Ct + 1)]
            chain_strips.append(samples)
            spts = [q for (q, _n) in samples]
            cum = 0
            for j in range(E9):
                put_edge(run[j], run[j + 1], spts[cum:cum + cells[j] + 1])
                cage[run[j]] = spts[cum]
                cum += cells[j]
            cage[run[E9]] = spts[cum]
        # interior cage verts by mean-value coordinates over the loop
        for vi in topo.interior:
            w = topo.mvc[vi]
            x = sum(w[j] * cage[topo.loop[j]][0] for j in range(len(w)))
            y = sum(w[j] * cage[topo.loop[j]][1] for j in range(len(w)))
            cage[vi] = (x, y)
        # interior edges: straight polylines
        for f in topo.faces:
            for i in range(4):
                a, b = f[i], f[(i + 1) % 4]
                key = (a, b) if a < b else (b, a)
                if key in edge_poly:
                    continue
                c9 = ecnt(a, b)
                edge_poly[key] = [lerp2(cage[key[0]], cage[key[1]], j / c9)
                                  for j in range(c9 + 1)]

        def get_poly(a, b):
            key = (a, b) if a < b else (b, a)
            poly = edge_poly[key]
            return poly if a < b else list(reversed(poly))

        # ---- fill every template quad with a matched Coons grid ------------
        shell.tag = 'road'
        for f in topo.faces:
            bottom = get_poly(f[0], f[1])
            right = get_poly(f[1], f[2])
            top = get_poly(f[3], f[2])
            left = get_poly(f[0], f[3])
            _emit_grid(shell, _coons_rect(bottom, top, left, right),
                       q2d, M_ASPHALT)

    # ---- gutter / curb / walk strips along every boundary chain ------------
    def emit_chain_strip(samples):
        for i9 in range(len(samples) - 1):
            (qa, na9), (qb, nb9) = samples[i9], samples[i9 + 1]

            def off(q, n9, d9):
                return (q[0] + n9[0] * d9, q[1] + n9[1] * d9)

            ga, gb = off(qa, na9, GUT_W), off(qb, nb9, GUT_W)
            ba, bb = off(qa, na9, GUT_W + CURB_W), off(qb, nb9,
                                                       GUT_W + CURB_W)
            shell.quad((qb[0], qb[1], Z_E), (qa[0], qa[1], Z_E),
                       (ga[0], ga[1], 0.0), (gb[0], gb[1], 0.0),
                       M_CONCRETE, 'gutter')
            shell.quad((ga[0], ga[1], CURB_H), (gb[0], gb[1], CURB_H),
                       (gb[0], gb[1], 0.0), (ga[0], ga[1], 0.0),
                       M_CONCRETE, 'curb')                    # face (-n)
            shell.quad((gb[0], gb[1], CURB_H), (ga[0], ga[1], CURB_H),
                       (ba[0], ba[1], CURB_H), (bb[0], bb[1], CURB_H),
                       M_CONCRETE, 'curb')                    # top (+z)
            shell.quad((bb[0], bb[1], CURB_H), (ba[0], ba[1], CURB_H),
                       (ba[0], ba[1], 0.0), (bb[0], bb[1], 0.0),
                       M_CONCRETE, 'curb')                    # back (+n)
            # sidewalk wedge slab (its own island: shrink along the chain)
            wa0 = off(qa, na9, GUT_W + CURB_W + GAP)
            wb0 = off(qb, nb9, GUT_W + CURB_W + GAP)
            wa1 = off(qa, na9, GUT_W + CURB_W + GAP + sw)
            wb1 = off(qb, nb9, GUT_W + CURB_W + GAP + sw)
            wa0, wb0 = lerp2(wa0, wb0, 0.004), lerp2(wb0, wa0, 0.004)
            wa1, wb1 = lerp2(wa1, wb1, 0.004), lerp2(wb1, wa1, 0.004)
            walk.tag = 'sidewalk'
            ring4 = [wb0, wa0, wa1, wb1]
            walk.quad((wb0[0], wb0[1], Z_WALK), (wa0[0], wa0[1], Z_WALK),
                      (wa1[0], wa1[1], Z_WALK), (wb1[0], wb1[1], Z_WALK),
                      M_CONCRETE, 'sidewalk')
            for (v0, v1) in zip(ring4, ring4[1:] + ring4[:1]):
                walk.quad((v1[0], v1[1], 0.11), (v0[0], v0[1], 0.11),
                          (v0[0], v0[1], Z_WALK), (v1[0], v1[1], Z_WALK),
                          M_CONCRETE, 'sidewalk')

    for samples in chain_strips:
        emit_chain_strip(samples)
    if phantom is not None:
        # the phantom mouth is also back-curb: strip it too
        mm = topo.arm_mouths[tmap(phantom)]
        mpoly = []
        for j in range(4):
            seg9 = get_poly(mm[j], mm[j + 1])
            mpoly.extend(seg9 if not mpoly else seg9[1:])
        segs = []
        for q in mpoly:
            segs.append(q)
        samples = []
        for i9, q in enumerate(segs):
            a = segs[max(0, i9 - 1)]
            b = segs[min(len(segs) - 1, i9 + 1)]
            d9 = max(math.hypot(b[0] - a[0], b[1] - a[1]), 1e-9)
            t9 = ((b[0] - a[0]) / d9, (b[1] - a[1]) / d9)
            samples.append((q, (t9[1], -t9[0])))
        emit_chain_strip(samples)

    # ---- per-arm aprons + paint (real arms only) ---------------------------
    for k in range(n_arm):
        if k == phantom:
            continue
        rows_k = rows_by_arm[k]
        r0, r1 = r0s[k], r0s[k] + apron
        ncol = max(4, int(round(apron / 1.6)))
        cols = [r0 + apron * i9 / ncol for i9 in range(ncol + 1)]

        def zmix(r, y):
            u9 = (r - r0) / (r1 - r0)
            return Z_E + u9 * (z_road(y) - Z_E)

        shell.tag = 'road'
        for (ca, cb) in zip(cols, cols[1:]):
            for (ya, yb) in zip(rows_k, rows_k[1:]):
                shell.quad(pt(k, ca, ya, zmix(ca, ya)),
                           pt(k, cb, ya, zmix(cb, ya)),
                           pt(k, cb, yb, zmix(cb, yb)),
                           pt(k, ca, yb, zmix(ca, yb)), M_ASPHALT)
        for (ye, yc, sg) in ((y0r, y0r - GUT_W, -1), (y1r, y1r + GUT_W, 1)):
            shell.tag = 'gutter'
            for (ca, cb) in zip(cols, cols[1:]):
                pts = [pt(k, ca, ye, zmix(ca, ye)),
                       pt(k, cb, ye, zmix(cb, ye)),
                       pt(k, cb, yc, 0.0), pt(k, ca, yc, 0.0)]
                if sg < 0:
                    pts.reverse()
                shell.quad(*pts, M_CONCRETE)
            shell.tag = 'curb'
            y_bk = yc + sg * CURB_W
            for (ca, cb) in zip(cols, cols[1:]):
                pts = [pt(k, ca, yc, CURB_H), pt(k, cb, yc, CURB_H),
                       pt(k, cb, yc, 0.0), pt(k, ca, yc, 0.0)]
                if sg > 0:
                    pts.reverse()
                shell.quad(*pts, M_CONCRETE)
                pts = [pt(k, ca, yc, CURB_H), pt(k, ca, y_bk, CURB_H),
                       pt(k, cb, y_bk, CURB_H), pt(k, cb, yc, CURB_H)]
                if sg > 0:
                    pts.reverse()
                shell.quad(*pts, M_CONCRETE)
                pts = [pt(k, ca, y_bk, CURB_H), pt(k, ca, y_bk, 0.0),
                       pt(k, cb, y_bk, 0.0), pt(k, cb, y_bk, CURB_H)]
                if sg > 0:
                    pts.reverse()
                shell.quad(*pts, M_CONCRETE)
            walk.tag = 'sidewalk'
            yi = yc + sg * (CURB_W + GAP)
            yo = yi + sg * sw
            wy0, wy1 = min(yi, yo), max(yi, yo)
            nsl = max(1, round(apron / SLAB_L))
            for i9 in range(nsl):
                a = r0 + apron * i9 / nsl + GAP
                b = r0 + apron * (i9 + 1) / nsl - GAP
                walk.quad(pt(k, a, wy0, Z_WALK), pt(k, b, wy0, Z_WALK),
                          pt(k, b, wy1, Z_WALK), pt(k, a, wy1, Z_WALK),
                          M_CONCRETE)
                corners = [(a, wy0), (b, wy0), (b, wy1), (a, wy1)]
                for (c0, c1) in zip(corners, corners[1:] + corners[:1]):
                    walk.quad(pt(k, c1[0], c1[1], 0.11),
                              pt(k, c0[0], c0[1], 0.11),
                              pt(k, c0[0], c0[1], Z_WALK),
                              pt(k, c1[0], c1[1], Z_WALK), M_CONCRETE)
        shell.tag = 'paint'

        def pquad(ta9, tb9, ya, yb, mat):
            for (ca, cb) in ((ya, min(yb, m0)), (max(ya, m0), yb)):
                if cb - ca < 0.02:
                    continue
                shell.quad(pt(k, ta9, ca, zmix(ta9, ca) + 0.003),
                           pt(k, tb9, ca, zmix(tb9, ca) + 0.003),
                           pt(k, tb9, cb, zmix(tb9, cb) + 0.003),
                           pt(k, ta9, cb, zmix(ta9, cb) + 0.003),
                           mat, 'paint')

        for off9 in ((-0.15, -0.05), (0.05, 0.15)):
            pquad(r0 + 3.6, r1 - 0.2, m0 + off9[0], m0 + off9[1], M_PAINT_Y)
        pquad(r0 + 0.6, r0 + 0.75, y0r + 0.25, y1r - 0.25, M_PAINT_W)
        pquad(r0 + 2.6, r0 + 2.75, y0r + 0.25, y1r - 0.25, M_PAINT_W)
        pquad(r0 + 3.0, r0 + 3.45, m0 + 0.1, y1r - 0.25, M_PAINT_W)

    road_ob = shell.to_object("LA_Intersection_Road", mats)
    walk_ob = walk.to_object("LA_Intersection_Walk", mats)
    return [road_ob, walk_ob]

def _emit_grid(shell, grid, q2d, mat):
    """Emit a Coons grid as oriented flat quads."""
    for j9 in range(len(grid) - 1):
        for i9 in range(len(grid[0]) - 1):
            q2d(shell, grid[j9][i9], grid[j9][i9 + 1],
                grid[j9 + 1][i9 + 1], grid[j9 + 1][i9], mat)

STREET_SPEC = [
    params.MODE_PARAM,
    dict(name="length", type='FLOAT', default=30.0, min=12.0, max=60.0,
         unit='LENGTH', desc="Centerline length (arc length when curved)"),
    dict(name="lanes", type='INT', default=4, min=2, max=6,
         desc="Total travel lanes (odd counts widen the south approach)"),
    dict(name="lane_width", type='FLOAT', default=3.3, min=2.8, max=3.8,
         unit='LENGTH'),
    dict(name="sidewalk_width", type='FLOAT', default=3.0, min=1.5, max=5.0,
         unit='LENGTH'),
    dict(name="curve", type='FLOAT', default=0.0, min=-90.0, max=90.0,
         desc="Horizontal arc sweep in degrees (+ = left); end planes stay "
              "exact profile cross-sections, so segments always join"),
    dict(name="end_rise", type='FLOAT', default=0.0, min=-6.0, max=6.0,
         unit='LENGTH', desc="Elevation gain start -> end (slope only in "
              "the traffic direction; cross-section never tilts)"),
    dict(name="terrain_object", type='STRING', default="",
         desc="Mesh object name: the centerline projects onto it, smoothed "
              "and slope-clamped to 8%; start interface stays at cursor"),
    dict(name="centerline", type='ENUM', default='double_yellow',
         items=('double_yellow', 'solid_yellow', 'dashed_yellow')),
    dict(name="edge_lines", type='BOOL', default=True,
         desc="Solid white edge lines at the outer lanes"),
    dict(name="crosswalk_style", type='ENUM', default='transverse',
         items=('transverse', 'continental')),
    dict(name="heave", type='FLOAT', default=0.5, min=0.0, max=1.0,
         desc="Sidewalk slab lift/tilt at the expansion joints"),
    dict(name="paint_wear", type='FLOAT', default=0.4, min=0.0, max=1.0),
    dict(name="patches", type='FLOAT', default=0.35, min=0.0, max=1.0),
    # -- monotony breakers --
    dict(name="median", type='ENUM', default='none',
         items=('none', 'turn', 'palms'),
         desc="Painted centerline, two-way turn lane, or a flat raised "
              "island with a flush soil bed (palms plug in via D3)"),
    dict(name="crosswalks", type='ENUM', default='near',
         items=('none', 'near', 'far', 'both')),
    dict(name="alley_mouths", type='ENUM', default='none',
         items=('none', 'south', 'north', 'both')),
    dict(name="sinkhole", type='BOOL', default=False),
    # -- story options (off by default) --
    dict(name="checkpoint_scar", type='BOOL', default=False,
         desc="Resurfacing scar + flush steel plates where bollards were "
              "sheared off"),
    dict(name="sand_lane", type='BOOL', default=False),
    dict(name="protest_stain", type='BOOL', default=False),
]

INTERSECTION_SPEC = [
    params.MODE_PARAM,
    dict(name="form", type='ENUM', default='cross',
         items=('veer', 'tee', 'cross', 'five'),
         desc="veer = 2-way angled bend, tee = 3-way, cross = 4-way, "
              "five = 5-way with a diagonal arm"),
    dict(name="skew", type='FLOAT', default=0.0, min=-35.0, max=35.0,
         desc="Rotates the odd arm (veer bend angle / angled crossings)"),
    dict(name="lanes", type='INT', default=4, min=2, max=6),
    dict(name="lane_width", type='FLOAT', default=3.3, min=2.8, max=3.8,
         unit='LENGTH'),
    dict(name="sidewalk_width", type='FLOAT', default=3.0, min=1.5, max=5.0,
         unit='LENGTH'),
    dict(name="apron_length", type='FLOAT', default=6.0, min=3.0, max=12.0,
         unit='LENGTH', desc="Crown-blend run per arm; the outer end is an "
              "exact la_street interface"),
    dict(name="corner_radius", type='FLOAT', default=6.0, min=3.0, max=12.0,
         unit='LENGTH', desc="Curb return fillet radius"),
]

params.register_tool(idname="la_street", label="Street Section",
                     family="Streetscape", build=build_street,
                     spec=STREET_SPEC)
params.register_tool(idname="la_intersection", label="Intersection",
                     family="Streetscape", build=build_intersection,
                     spec=INTERSECTION_SPEC)
