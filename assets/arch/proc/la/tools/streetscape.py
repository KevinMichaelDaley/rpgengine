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
class _OffShell:
    """Coordinate-offset proxy over a _Shell: several junctions emit into
    ONE shell without their weld caches colliding at local coordinates."""

    def __init__(self, sh, origin):
        self._sh = sh
        self._o = origin
        self.tag = None

    def quad(self, a, b, c, d, mat=0, tag=None):
        (ox, oy, oz) = self._o
        t = tag if tag is not None else self.tag
        return self._sh.quad((a[0] + ox, a[1] + oy, a[2] + oz),
                             (b[0] + ox, b[1] + oy, b[2] + oz),
                             (c[0] + ox, c[1] + oy, c[2] + oz),
                             (d[0] + ox, d[1] + oy, d[2] + oz), mat, t)


def _ix_geo(shell_raw, walk_raw, bear, phantom, profiles, sw, apron, rc,
            origin=(0.0, 0.0, 0.0), network=False, dens=None):
    """Emit one intersection into the given shells at `origin`.

    `bear` = ccw-sorted outgoing arm bearings; `profiles[k]` = (lanes, lw)
    PER ARM -- the pad deforms smoothly between differing arm widths, so
    the intersection scales to its incoming roads. `phantom` = synthetic
    arm whose mouth lies ON the host wedge's chain (tee back curb / Y).

    network mode: every mouth carries exactly 4*dens cells so a swept
    road with 4*dens rows WELDS vert-for-vert at the mouth line; no
    aprons/paint (the sweep owns the approach); sidewalks emit as
    CONTINUOUS welded bands (no expansion-joint islands) whose ends
    weld to the road walk bands.

    Returns per-REAL-arm interfaces:
    [(bearing_deg, iface_center_world_2d, r_iface)] -- r_iface is the
    mouth radius r0 in network mode (the weld line), r0 + apron for the
    preset operator."""
    n_arm = len(bear)
    prof = []
    for k in range(n_arm):
        (lanes_k, lw_k) = profiles[k]
        prof.append(_xsection(lanes_k, lw_k, 'none'))
    WLs = [prof[k][5] + GUT_W for k in range(n_arm)]      # y1r + gutter
    WRs = [-prof[k][2] + GUT_W for k in range(n_arm)]     # -y0r + gutter
    shell = _OffShell(shell_raw, origin)
    walk = _OffShell(walk_raw, origin)

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

    # ---- fillets between REAL arm pairs ------------------------------------
    def pairfil(ka, kb):
        (da, na), (db, nb) = frames[ka], frames[kb]
        wedge = (bear[kb] - bear[ka]) % 360.0
        if abs(wedge - 180.0) <= 8.0:
            return None
        pv = 1.0 if wedge < 180.0 else -1.0
        oA, oB = WLs[ka] + pv * rc, WRs[kb] + pv * rc
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
        return dict(C=C, ta=ta, tb=tb, a0=a0, a1=a1, pv=pv)

    kp = kn = hostf = None
    fil = [None] * n_arm
    if phantom is not None:
        kp, kn = (phantom - 1) % n_arm, (phantom + 1) % n_arm
        hostf = pairfil(kp, kn)
    for k in range(n_arm):
        k2 = (k + 1) % n_arm
        if phantom in (k, k2):
            continue
        fil[k] = pairfil(k, k2)

    r0s = []
    for k in range(n_arm):
        km = (k - 1) % n_arm
        need = 1.0
        if fil[k] is not None:
            need = max(need, fil[k]['ta'])
        if fil[km] is not None:
            need = max(need, fil[km]['tb'])
        if phantom is not None and hostf is not None:
            if k == kp:
                need = max(need, hostf['ta'])
            if k == kn:
                need = max(need, hostf['tb'])
        r0s.append(need + 0.4)

    # ---- chain geometry ----------------------------------------------------
    def pieces_of(f9, A1, A2, ka, kb):
        if f9 is None:
            return [('seg', A1, A2)]
        rg = rc + f9['pv'] * GUT_W
        T1 = pt(ka, f9['ta'], prof[ka][5], 0.0)[:2]
        T2 = pt(kb, f9['tb'], prof[kb][2], 0.0)[:2]
        pieces = []
        if math.dist(A1, T1) > 1e-6:
            pieces.append(('seg', A1, T1))
        pieces.append(('arc', f9['C'], rg, f9['a0'], f9['a1'], f9['pv']))
        if math.dist(T2, A2) > 1e-6:
            pieces.append(('seg', T2, A2))
        return pieces

    def plen(pieces):
        return sum(math.dist(pc[1], pc[2]) if pc[0] == 'seg'
                   else pc[2] * abs(pc[4] - pc[3]) for pc in pieces)

    def chain_at(pieces, L9, f):
        s9 = min(max(f, 0.0), 1.0) * L9
        for pc in pieces:
            pl = (math.dist(pc[1], pc[2]) if pc[0] == 'seg'
                  else pc[2] * abs(pc[4] - pc[3]))
            if s9 > pl + 1e-9:
                s9 -= pl
                continue
            if pc[0] == 'seg':
                (_t, a, b) = pc
                d9 = max(math.dist(a, b), 1e-9)
                q = lerp2(a, b, s9 / d9)
                t9 = ((b[0] - a[0]) / d9, (b[1] - a[1]) / d9)
                return q, (t9[1], -t9[0])
            (_t, C, rg, a0, a1, pv) = pc
            ang = a0 + (a1 - a0) * (s9 / pl)
            q = (C[0] + rg * math.cos(ang), C[1] + rg * math.sin(ang))
            return q, (pv * (C[0] - q[0]) / rg, pv * (C[1] - q[1]) / rg)
        pc = pieces[-1]
        if pc[0] == 'seg':
            a, b = pc[1], pc[2]
            d9 = max(math.dist(a, b), 1e-9)
            t9 = ((b[0] - a[0]) / d9, (b[1] - a[1]) / d9)
            return b, (t9[1], -t9[0])
        (_t, C, rg, a0, a1, pv) = pc
        q = (C[0] + rg * math.cos(a1), C[1] + rg * math.sin(a1))
        return q, (pv * (C[0] - q[0]) / rg, pv * (C[1] - q[1]) / rg)

    host = None
    if phantom is not None:
        A1h = pt(kp, r0s[kp], prof[kp][5], 0.0)[:2]
        A2h = pt(kn, r0s[kn], prof[kn][2], 0.0)[:2]
        hp = pieces_of(hostf, A1h, A2h, kp, kn)
        host = (hp, plen(hp))

    def mk_chain(k):
        k2 = (k + 1) % n_arm
        if phantom is not None and k2 == phantom:
            (hp, hl) = host
            return (lambda f: chain_at(hp, hl, f * 0.32)), hl * 0.32
        if phantom is not None and k == phantom:
            (hp, hl) = host
            return (lambda f: chain_at(hp, hl, 0.68 + f * 0.32)), hl * 0.32
        A1 = pt(k, r0s[k], prof[k][5], 0.0)[:2]
        A2 = pt(k2, r0s[k2], prof[k2][2], 0.0)[:2]
        pieces = pieces_of(fil[k], A1, A2, k, k2)
        L9 = plen(pieces)
        return (lambda f: chain_at(pieces, L9, f)), L9

    # ---- VEER (preset only): rectangular through-grid ----------------------
    chain_strips = []
    if n_arm == 2:
        (n_s, n_n, y0r, m0, m1, y1r, z_pk, z_road) = prof[0]
        M2 = 4 * (dens or max(1, round(profiles[0][0] / 2)))
        rows_v = [y0r + (y1r - y0r) * j / M2 for j in range(M2 + 1)]
        mouthv = [[pt(k, r0s[k], y, Z_E)[:2] for y in rows_v]
                  for k in range(2)]
        ch_fn = [mk_chain(k) for k in range(2)]
        nq2 = max(4, int(round(max(c[1] for c in ch_fn) / 1.5)) // 2 * 2)
        chs = [[fn(j / nq2) for j in range(nq2 + 1)] for (fn, _l) in ch_fn]
        bot = mouthv[0]
        top = [mouthv[1][M2 - j] for j in range(M2 + 1)]
        lef = [q for (q, _n) in reversed(chs[1])]
        rig = [q for (q, _n) in chs[0]]
        _emit_grid(shell, _coons_rect(bot, top, lef, rig), q2d, M_ASPHALT)
        chain_strips = chs
        rows_by_arm = [rows_v, rows_v]
    else:
        topo = _PAD4 if n_arm == 4 else (_PAD5 if n_arm == 5 else _PAD4)
        wt = [(bear[(k + 1) % n_arm] - bear[k]) % 360.0
              for k in range(n_arm)]
        wp = [(topo.arm_bearings[(i + 1) % n_arm] -
               topo.arm_bearings[i]) % 360.0 for i in range(n_arm)]
        shift = min(range(n_arm), key=lambda s9: sum(
            abs(wp[(k + s9) % n_arm] - wt[k]) for k in range(n_arm)))

        def tmap(k):
            return (k + shift) % n_arm

        # mouth-edge families are FROZEN at `s` so every mouth carries
        # exactly 4*s cells (the swept roads weld vert-for-vert); corner
        # demands never bump them.
        s = dens if dens else max(1, round(max(pr9[0] for pr9 in profiles)
                                           / 2))
        cnt = {}
        frozen = set()
        for k in range(n_arm):
            mm = topo.arm_mouths[tmap(k)]
            for j in range(4):
                fm = topo.fam[tuple(sorted((mm[j], mm[j + 1])))]
                cnt[fm] = s
                frozen.add(fm)
        chain_fns = [mk_chain(k) for k in range(n_arm)]
        for k in range(n_arm):
            run = topo.corners[tmap(k)]
            E9 = len(run) - 1
            dem = max(1, int(round(chain_fns[k][1] / E9 / 1.5)))
            for j in range(E9):
                fm = topo.fam[tuple(sorted((run[j], run[j + 1])))]
                if fm not in frozen:
                    cnt[fm] = max(cnt.get(fm, 1), dem)

        def ecnt(a, b):
            return cnt.get(topo.fam[tuple(sorted((a, b)))], 1)

        edge_poly = {}
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
                (hp, hl) = host
                mpos = [chain_at(hp, hl, 0.32 + 0.36 * j / Mk)[0]
                        for j in range(Mk + 1)]
                rows_by_arm.append(None)
            else:
                (y0r, y1r) = prof[k][2], prof[k][5]
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
            (fn, L9) = chain_fns[k]
            E9 = len(run) - 1
            cells = [ecnt(run[j], run[j + 1]) for j in range(E9)]
            Ct = sum(cells)
            samples = [fn(j / Ct) for j in range(Ct + 1)]
            chain_strips.append(samples)
            spts = [q for (q, _n) in samples]
            cum = 0
            for j in range(E9):
                put_edge(run[j], run[j + 1], spts[cum:cum + cells[j] + 1])
                cage[run[j]] = spts[cum]
                cum += cells[j]
            cage[run[E9]] = spts[cum]
        for vi in topo.interior:
            w = topo.mvc[vi]
            x = sum(w[j] * cage[topo.loop[j]][0] for j in range(len(w)))
            y = sum(w[j] * cage[topo.loop[j]][1] for j in range(len(w)))
            cage[vi] = (x, y)
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

        shell.tag = 'road'
        for f in topo.faces:
            _emit_grid(shell, _coons_rect(get_poly(f[0], f[1]),
                                          get_poly(f[3], f[2]),
                                          get_poly(f[0], f[3]),
                                          get_poly(f[1], f[2])),
                       q2d, M_ASPHALT)
        if phantom is not None:
            # sample the HOST chain analytically so the strip's end
            # verts/normals match the sub-chain strips exactly (numeric
            # normals left the walk bands unwelded at the shared corners)
            mm = topo.arm_mouths[tmap(phantom)]
            Mk = sum(ecnt(mm[j], mm[j + 1]) for j in range(4))
            (hp, hl) = host
            samples = [chain_at(hp, hl, 0.32 + 0.36 * j / Mk)
                       for j in range(Mk + 1)]
            chain_strips.append(samples)

    # ---- gutter / curb / walk strips along every boundary chain ------------
    def emit_chain_strip(samples):
        for i9 in range(len(samples) - 1):
            (qa, na9), (qb, nb9) = samples[i9], samples[i9 + 1]

            def off(q, n9, d9):
                return (q[0] + n9[0] * d9, q[1] + n9[1] * d9)

            ga, gb = off(qa, na9, GUT_W), off(qb, nb9, GUT_W)
            ba = off(qa, na9, GUT_W + CURB_W)
            bb = off(qb, nb9, GUT_W + CURB_W)
            shell.quad((qb[0], qb[1], Z_E), (qa[0], qa[1], Z_E),
                       (ga[0], ga[1], 0.0), (gb[0], gb[1], 0.0),
                       M_CONCRETE, 'gutter')
            shell.quad((ga[0], ga[1], CURB_H), (gb[0], gb[1], CURB_H),
                       (gb[0], gb[1], 0.0), (ga[0], ga[1], 0.0),
                       M_CONCRETE, 'curb')
            shell.quad((gb[0], gb[1], CURB_H), (ga[0], ga[1], CURB_H),
                       (ba[0], ba[1], CURB_H), (bb[0], bb[1], CURB_H),
                       M_CONCRETE, 'curb')
            shell.quad((bb[0], bb[1], CURB_H), (ba[0], ba[1], CURB_H),
                       (ba[0], ba[1], 0.0), (bb[0], bb[1], 0.0),
                       M_CONCRETE, 'curb')
            wa0 = off(qa, na9, GUT_W + CURB_W + GAP)
            wb0 = off(qb, nb9, GUT_W + CURB_W + GAP)
            wa1 = off(qa, na9, GUT_W + CURB_W + GAP + sw)
            wb1 = off(qb, nb9, GUT_W + CURB_W + GAP + sw)
            if network:
                # CONTINUOUS welded band: shared verts cell to cell; the
                # open ends weld to the road walk bands at the mouths
                walk.quad((wb0[0], wb0[1], Z_WALK), (wa0[0], wa0[1], Z_WALK),
                          (wa1[0], wa1[1], Z_WALK), (wb1[0], wb1[1], Z_WALK),
                          M_CONCRETE, 'sidewalk')
                walk.quad((wa0[0], wa0[1], Z_WALK), (wb0[0], wb0[1], Z_WALK),
                          (wb0[0], wb0[1], 0.11), (wa0[0], wa0[1], 0.11),
                          M_CONCRETE, 'sidewalk')
                walk.quad((wb1[0], wb1[1], Z_WALK), (wa1[0], wa1[1], Z_WALK),
                          (wa1[0], wa1[1], 0.11), (wb1[0], wb1[1], 0.11),
                          M_CONCRETE, 'sidewalk')
                continue
            wa0, wb0 = lerp2(wa0, wb0, 0.004), lerp2(wb0, wa0, 0.004)
            wa1, wb1 = lerp2(wa1, wb1, 0.004), lerp2(wb1, wa1, 0.004)
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

    # ---- per-arm aprons + paint (preset operator only) ---------------------
    if not network:
        for k in range(n_arm):
            if k == phantom:
                continue
            (n_s, n_n, y0r, m0, m1, y1r, z_pk, z_road) = prof[k]
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
            for (ye, yc, sg) in ((y0r, y0r - GUT_W, -1),
                                 (y1r, y1r + GUT_W, 1)):
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
                pquad(r0 + 3.6, r1 - 0.2, m0 + off9[0], m0 + off9[1],
                      M_PAINT_Y)
            pquad(r0 + 0.6, r0 + 0.75, y0r + 0.25, y1r - 0.25, M_PAINT_W)
            pquad(r0 + 2.6, r0 + 2.75, y0r + 0.25, y1r - 0.25, M_PAINT_W)
            pquad(r0 + 3.0, r0 + 3.45, m0 + 0.1, y1r - 0.25, M_PAINT_W)

    out = []
    for k in range(n_arm):
        if k == phantom:
            continue
        d9 = frames[k][0]
        ri = r0s[k] if network else r0s[k] + apron
        out.append((bear[k], (origin[0] + d9[0] * ri,
                              origin[1] + d9[1] * ri), ri))
    return out


def build_intersection(p, rng):
    """Preset-form intersection operator (thin wrapper over _ix_geo)."""
    del rng
    form = p["form"]
    bear = sorted(b % 360.0 for b in _arm_bearings(form, p["skew"]))
    phantom = None
    if form == 'tee':
        bear = sorted(bear + [270.0])
        phantom = bear.index(270.0)
    shell = _Shell()
    walk = _Shell()
    profs = [(p["lanes"], p["lane_width"])] * len(bear)
    _ix_geo(shell, walk, bear, phantom, profs, p["sidewalk_width"],
            p["apron_length"], p["corner_radius"])
    mats = [_material(nm) for nm in _MATS]
    return [shell.to_object("LA_Intersection_Road", mats),
            walk.to_object("LA_Intersection_Walk", mats)]


def _emit_grid(shell, grid, q2d, mat):
    """Emit a Coons grid as oriented flat quads."""
    for j9 in range(len(grid) - 1):
        for i9 in range(len(grid[0]) - 1):
            q2d(shell, grid[j9][i9], grid[j9][i9 + 1],
                grid[j9 + 1][i9 + 1], grid[j9 + 1][i9], mat)

# ===========================================================================
# ROAD NETWORK from an edge-skin mesh (destructive operator)
# ===========================================================================

def _lane_fit(width):
    """Road width -> (lanes, lane_width): lanes have a set width range."""
    lanes = min(6, max(2, int(round(width / 3.3))))
    lw = min(3.8, max(2.8, width / lanes))
    return lanes, lw


def _cr_stations(pts, step, closed=False):
    """Catmull-Rom through `pts` [(x, y, z)], resampled to ~`step` m
    stations. Returns [(pos2d, dir2d, z)] plus the total length."""
    if closed:
        ctrl = [pts[-1]] + list(pts) + [pts[0], pts[1]]
    else:
        ctrl = [pts[0]] + list(pts) + [pts[-1]]
    dense = []
    for i in range(1, len(ctrl) - 2):
        p0, p1, p2, p3 = ctrl[i - 1], ctrl[i], ctrl[i + 1], ctrl[i + 2]
        nsub = max(4, int(math.dist(p1[:2], p2[:2]) / 0.5))
        for j in range(nsub):
            t = j / nsub
            q = []
            for c in range(3):
                q.append(0.5 * ((2 * p1[c]) + (-p0[c] + p2[c]) * t +
                                (2 * p0[c] - 5 * p1[c] + 4 * p2[c] -
                                 p3[c]) * t * t +
                                (-p0[c] + 3 * p1[c] - 3 * p2[c] +
                                 p3[c]) * t * t * t))
            dense.append(tuple(q))
    dense.append(tuple(pts[0] if closed else pts[-1]))
    cum = [0.0]
    for a, b in zip(dense, dense[1:]):
        cum.append(cum[-1] + math.dist(a[:2], b[:2]))
    L = cum[-1]
    n = max(2, int(round(L / step)))
    stations = []
    k = 0
    for i in range(n + 1):
        s = L * i / n
        while k < len(cum) - 2 and cum[k + 1] < s:
            k += 1
        f = (s - cum[k]) / max(cum[k + 1] - cum[k], 1e-9)
        a, b = dense[k], dense[k + 1]
        pos = (a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f)
        z = a[2] + (b[2] - a[2]) * f
        d = (b[0] - a[0], b[1] - a[1])
        dl = max(math.hypot(*d), 1e-9)
        stations.append((pos, (d[0] / dl, d[1] / dl), z))
    if closed:
        stations[-1] = stations[0]    # exact weld at the ring seam
    return stations, L


def _sweep_road(shell, walk, stations, lanes, lw, sw, wear, rng,
                n_rows=None, crown_at=None, jx_a=False, jx_b=False,
                continuous_walk=False):
    """Sweep the street cross-section along `stations` [(pos, dir, z)]:
    crowned roadway, gutter pans, curbs, sidewalks, paint. `n_rows`
    fixes the roadway row count (the road-network weld contract: mouths
    carry 4*dens cells, so the sweep uses the same); `crown_at(s)`
    scales crown amplitude (0 at a junction mouth -- the pad is flat --
    ramping to 1); jx_a/jx_b paint a crosswalk + stop bar at the s=0 /
    s=L end; continuous_walk emits welded sidewalk bands (merged with
    the junction wedge bands) instead of expansion-joint islands."""
    n_s, n_n, y0r, m0, m1, y1r, z_pk, z_road = _xsection(lanes, lw, 'none')
    M = n_rows if n_rows else 2 * lanes
    rows = [y0r + (y1r - y0r) * j / M for j in range(M + 1)]
    cum = [0.0]
    for (a, b) in zip(stations, stations[1:]):
        cum.append(cum[-1] + math.dist(a[0], b[0]))
    Lt = cum[-1]

    def P(i, u, z_off):
        (pos, d, z) = stations[i]
        nl = (-d[1], d[0])
        return (pos[0] + nl[0] * u, pos[1] + nl[1] * u, z + z_off)

    def zr(i, u):
        amp = crown_at(cum[i]) if crown_at else 1.0
        return Z_E + (z_road(u) - Z_E) * amp

    shell.tag = 'road'
    for i in range(len(stations) - 1):
        for (ya, yb) in zip(rows, rows[1:]):
            shell.quad(P(i, ya, zr(i, ya)), P(i + 1, ya, zr(i + 1, ya)),
                       P(i + 1, yb, zr(i + 1, yb)), P(i, yb, zr(i, yb)),
                       M_ASPHALT)
    for (ye, yc, sg) in ((y0r, y0r - GUT_W, -1), (y1r, y1r + GUT_W, 1)):
        shell.tag = 'gutter'
        for i in range(len(stations) - 1):
            pts = [P(i, ye, Z_E), P(i + 1, ye, Z_E),
                   P(i + 1, yc, 0.0), P(i, yc, 0.0)]
            if sg < 0:
                pts.reverse()
            shell.quad(*pts, M_CONCRETE)
        shell.tag = 'curb'
        y_bk = yc + sg * CURB_W
        for i in range(len(stations) - 1):
            pts = [P(i, yc, CURB_H), P(i + 1, yc, CURB_H),
                   P(i + 1, yc, 0.0), P(i, yc, 0.0)]
            if sg > 0:
                pts.reverse()
            shell.quad(*pts, M_CONCRETE)
            pts = [P(i, yc, CURB_H), P(i, y_bk, CURB_H),
                   P(i + 1, y_bk, CURB_H), P(i + 1, yc, CURB_H)]
            if sg > 0:
                pts.reverse()
            shell.quad(*pts, M_CONCRETE)
            pts = [P(i, y_bk, CURB_H), P(i, y_bk, 0.0),
                   P(i + 1, y_bk, 0.0), P(i + 1, y_bk, CURB_H)]
            if sg > 0:
                pts.reverse()
            shell.quad(*pts, M_CONCRETE)
        walk.tag = 'sidewalk'
        yi = yc + sg * (CURB_W + GAP)
        yo = yi + sg * sw
        wy0, wy1 = min(yi, yo), max(yi, yo)
        for i in range(len(stations) - 1):
            qs = [P(i, wy0, 0.0)[:2], P(i + 1, wy0, 0.0)[:2],
                  P(i + 1, wy1, 0.0)[:2], P(i, wy1, 0.0)[:2]]
            zi = stations[i][2] + Z_WALK
            zi1 = stations[i + 1][2] + Z_WALK
            if continuous_walk:
                walk.quad((qs[0][0], qs[0][1], zi), (qs[1][0], qs[1][1], zi1),
                          (qs[2][0], qs[2][1], zi1), (qs[3][0], qs[3][1], zi),
                          M_CONCRETE)
                walk.quad((qs[1][0], qs[1][1], zi1), (qs[0][0], qs[0][1], zi),
                          (qs[0][0], qs[0][1], zi - 0.05),
                          (qs[1][0], qs[1][1], zi1 - 0.05), M_CONCRETE)
                walk.quad((qs[3][0], qs[3][1], zi), (qs[2][0], qs[2][1], zi1),
                          (qs[2][0], qs[2][1], zi1 - 0.05),
                          (qs[3][0], qs[3][1], zi - 0.05), M_CONCRETE)
                continue

            def ar2(a9, b9, c9):
                return ((b9[0] - a9[0]) * (c9[1] - a9[1]) -
                        (c9[0] - a9[0]) * (b9[1] - a9[1]))

            if ar2(qs[0], qs[1], qs[2]) * ar2(qs[0], qs[2], qs[3]) <= 0:
                continue
            cl = max(math.dist(qs[0], qs[1]), 0.1)
            fi = min(0.05, 0.008 / cl)
            a0 = (qs[0][0] + (qs[1][0] - qs[0][0]) * fi,
                  qs[0][1] + (qs[1][1] - qs[0][1]) * fi)
            b0 = (qs[1][0] + (qs[0][0] - qs[1][0]) * fi,
                  qs[1][1] + (qs[0][1] - qs[1][1]) * fi)
            a1 = (qs[3][0] + (qs[2][0] - qs[3][0]) * fi,
                  qs[3][1] + (qs[2][1] - qs[3][1]) * fi)
            b1 = (qs[2][0] + (qs[3][0] - qs[2][0]) * fi,
                  qs[2][1] + (qs[3][1] - qs[2][1]) * fi)
            walk.quad((a0[0], a0[1], zi), (b0[0], b0[1], zi1),
                      (b1[0], b1[1], zi1), (a1[0], a1[1], zi),
                      M_CONCRETE)
            ring4 = [(a0, zi), (b0, zi1), (b1, zi1), (a1, zi)]
            for ((v0, z0), (v1, z1)) in zip(ring4, ring4[1:] + ring4[:1]):
                walk.quad((v1[0], v1[1], z1 - 0.05),
                          (v0[0], v0[1], z0 - 0.05),
                          (v0[0], v0[1], z0), (v1[0], v1[1], z1),
                          M_CONCRETE)

    # ---- paint -------------------------------------------------------------
    shell.tag = 'paint'

    def strip_cells(u0, u1, s0, s1, mat):
        for i in range(len(stations) - 1):
            if cum[i + 1] < s0 or cum[i] > s1:
                continue
            f0 = max(0.0, (s0 - cum[i]) / max(cum[i + 1] - cum[i], 1e-9))
            f1 = min(1.0, (s1 - cum[i]) / max(cum[i + 1] - cum[i], 1e-9))

            def mid(u, f):
                qa = P(i, u, zr(i, u) + 0.003)
                qb = P(i + 1, u, zr(i + 1, u) + 0.003)
                return (qa[0] + (qb[0] - qa[0]) * f,
                        qa[1] + (qb[1] - qa[1]) * f,
                        qa[2] + (qb[2] - qa[2]) * f)

            shell.quad(mid(u0, f0), mid(u0, f1), mid(u1, f1), mid(u1, f0),
                       mat, 'paint')

    # long lines STOP short of the junction paint (real striping does,
    # and coplanar overlaps with the crosswalk bars audit as defects)
    s_lo = 3.6 if jx_a else 0.5
    s_hi = (Lt - 3.6) if jx_b else (Lt - 0.5)
    strip_cells(y0r + 0.10, y0r + 0.22, s_lo, s_hi, M_PAINT_W)
    strip_cells(y1r - 0.22, y1r - 0.10, s_lo, s_hi, M_PAINT_W)
    strip_cells(m0 - 0.15, m0 - 0.05, s_lo, s_hi, M_PAINT_Y)
    strip_cells(m0 + 0.05, m0 + 0.15, s_lo, s_hi, M_PAINT_Y)
    for (side, cnt9) in (('s', n_s), ('n', n_n)):
        for k in range(1, cnt9):
            y_l = (y0r + k * lw) if side == 's' else (m1 + k * lw)
            x = s_lo + 0.5 + rng.random() * 3.0
            while x < s_hi - 0.5:
                if rng.random() >= wear * 0.55:
                    strip_cells(y_l - 0.06, y_l + 0.06, x,
                                min(x + 3.0, s_hi - 0.3), M_PAINT_W)
                x += 12.0
    # junction approaches: transverse crosswalk + stop bar
    if jx_a and Lt > 6.0:
        strip_cells(y0r + 0.25, y1r - 0.25, 0.8, 0.95, M_PAINT_W)
        strip_cells(y0r + 0.25, y1r - 0.25, 2.3, 2.45, M_PAINT_W)
        strip_cells(m0 + 0.1, y1r - 0.25, 2.8, 3.25, M_PAINT_W)
    if jx_b and Lt > 6.0:
        strip_cells(y0r + 0.25, y1r - 0.25, Lt - 0.95, Lt - 0.8, M_PAINT_W)
        strip_cells(y0r + 0.25, y1r - 0.25, Lt - 2.45, Lt - 2.3, M_PAINT_W)
        strip_cells(y0r + 0.25, m0 - 0.1, Lt - 3.25, Lt - 2.8, M_PAINT_W)


def _fillet_polyline(pts, R, closed=False):
    """Round every sharp interior corner of a polyline with an arc of
    radius ~R (clamped to the neighbouring segment lengths): an acutely
    bent path would otherwise fold the swept road -- the sidewalk, at
    the largest offset, cannot bend faster than its own radius."""
    n = len(pts)
    idxs = range(n) if closed else range(1, n - 1)
    out = [] if closed else [pts[0]]
    for i in idxs:
        a, b, c = pts[(i - 1) % n], pts[i], pts[(i + 1) % n]
        v0 = (b[0] - a[0], b[1] - a[1])
        v1 = (c[0] - b[0], c[1] - b[1])
        l0, l1 = math.hypot(*v0), math.hypot(*v1)
        if l0 < 1e-6 or l1 < 1e-6:
            out.append(b)
            continue
        d0 = (v0[0] / l0, v0[1] / l0)
        d1 = (v1[0] / l1, v1[1] / l1)
        dot = max(-1.0, min(1.0, d0[0] * d1[0] + d0[1] * d1[1]))
        turn = math.acos(dot)
        if turn < math.radians(12.0):
            out.append(b)
            continue
        t_need = R * math.tan(turn / 2.0)
        t_eff = min(t_need, 0.45 * l0, 0.45 * l1)
        R_eff = t_eff / max(math.tan(turn / 2.0), 1e-9)
        side = 1.0 if (d0[0] * d1[1] - d0[1] * d1[0]) > 0 else -1.0
        T0 = (b[0] - d0[0] * t_eff, b[1] - d0[1] * t_eff)
        n0 = (-d0[1] * side, d0[0] * side)
        C = (T0[0] + n0[0] * R_eff, T0[1] + n0[1] * R_eff)
        a0 = math.atan2(T0[1] - C[1], T0[0] - C[0])
        nseg = max(2, int(math.ceil(R_eff * turn / 1.2)))
        zb = b[2]
        for j in range(nseg + 1):
            ang = a0 + side * turn * j / nseg
            out.append((C[0] + R_eff * math.cos(ang),
                        C[1] + R_eff * math.sin(ang), zb))
    if not closed:
        out.append(pts[-1])
    return out


def _relax_stations(stations, R_lim, closed=False, iters=150):
    """Curvature-limited Laplacian relaxation: any station whose local
    circumradius is below R_lim eases toward its neighbours (endpoints
    fixed on open paths -- they sit on junction interfaces). Directions
    are recomputed from the relaxed positions."""
    n = len(stations)
    pts = [[s[0][0], s[0][1], s[2]] for s in stations]

    def rad(i):
        a = pts[(i - 1) % n]
        b = pts[i]
        c = pts[(i + 1) % n]
        la = math.dist(a[:2], b[:2])
        lb = math.dist(b[:2], c[:2])
        lc = math.dist(c[:2], a[:2])
        ar = abs((b[0] - a[0]) * (c[1] - a[1]) -
                 (c[0] - a[0]) * (b[1] - a[1])) / 2.0
        return (la * lb * lc) / (4.0 * ar) if ar > 1e-9 else 1e9

    rng9 = range(n) if closed else range(1, n - 1)
    for _it in range(iters):
        moved = False
        for i in rng9:
            if rad(i) >= R_lim:
                continue
            a = pts[(i - 1) % n]
            c = pts[(i + 1) % n]
            for k in range(3):
                pts[i][k] = pts[i][k] * 0.5 + (a[k] + c[k]) * 0.25
            moved = True
        if not moved:
            break
    out = []
    for i in range(n):
        if closed:
            a = pts[(i - 1) % n]
            c = pts[(i + 1) % n]
        else:
            a = pts[max(0, i - 1)]
            c = pts[min(n - 1, i + 1)]
        d = (c[0] - a[0], c[1] - a[1])
        dl = max(math.hypot(*d), 1e-9)
        out.append(((pts[i][0], pts[i][1]), (d[0] / dl, d[1] / dl),
                    pts[i][2]))
    return out


def build_road_network(p, rng, context):
    """DESTRUCTIVE: consume the active edge-network mesh (skin-modifier
    radii = road half-widths) and replace it with ONE welded road mesh
    (plus one welded sidewalk mesh). Vertex degree drives the junction
    form (3 = tee/Y, 4 = cross, 5 = five-way); skin width drives lanes;
    roads follow the Catmull-Rom curvature of the vert chains. The weld
    contract: every junction mouth carries exactly 4*dens cells and
    every road sweeps 4*dens rows, so roads join the intersections
    vert-for-vert; each intersection scales per-arm to its incoming
    road widths; sidewalks are continuous welded bands merged across
    the junction wedges."""
    src = context.active_object
    if src is None or src.type != 'MESH' or len(src.data.edges) == 0:
        raise ValueError("select an edge-network mesh (with a skin "
                         "modifier for widths)")
    mw = src.matrix_world
    me = src.data
    verts = [tuple(mw @ v.co) for v in me.vertices]
    edges = [(e.vertices[0], e.vertices[1]) for e in me.edges]
    if me.skin_vertices:
        rad = [max(0.05, (sv.radius[0] + sv.radius[1]) / 2.0)
               for sv in me.skin_vertices[0].data]
    else:
        rad = [p["default_width"] / 2.0] * len(verts)

    from collections import defaultdict
    adj = defaultdict(list)
    for (a, b) in edges:
        adj[a].append(b)
        adj[b].append(a)
    deg = {v: len(adj[v]) for v in adj}
    for v, d9 in deg.items():
        if d9 > 5:
            raise ValueError("vertex %d has degree %d (max 5)" % (v, d9))

    used = set()
    paths = []

    def walk_from(v0, v1):
        chain = [v0, v1]
        used.add(tuple(sorted((v0, v1))))
        while deg.get(chain[-1], 0) == 2:
            nxt = [w for w in adj[chain[-1]] if w != chain[-2]][0]
            key = tuple(sorted((chain[-1], nxt)))
            if key in used:
                break
            used.add(key)
            chain.append(nxt)
        return chain

    for v in sorted(deg):
        if deg[v] == 2:
            continue
        for w in adj[v]:
            if tuple(sorted((v, w))) not in used:
                paths.append(dict(verts=walk_from(v, w), closed=False))
    for (a, b) in edges:
        if tuple(sorted((a, b))) in used:
            continue
        chain = walk_from(a, b)
        paths.append(dict(verts=chain, closed=chain[0] == chain[-1]))

    for pa in paths:
        vs = pa['verts']
        mid = vs[1:-1] if len(vs) > 2 and not pa['closed'] else vs
        w9 = 2.0 * sum(rad[v] for v in mid) / len(mid)
        pa['lanes'], pa['lw'] = _lane_fit(w9)

    # ONE row density for the whole network: the weld contract
    dens = max(1, round(max(pa['lanes'] for pa in paths) / 2))

    shell = _Shell()
    walk = _Shell()
    sw = p["sidewalk_width"]
    rc = p["corner_radius"]
    juncs = {}
    for v in sorted(deg):
        if deg[v] < 3:
            continue
        arms = []
        for pa in paths:
            vs = pa['verts']
            if vs[0] == v:
                d9 = (verts[vs[1]][0] - verts[v][0],
                      verts[vs[1]][1] - verts[v][1])
                arms.append((math.degrees(math.atan2(d9[1], d9[0])) % 360.0,
                             pa, 'a'))
            if vs[-1] == v and not pa['closed']:
                d9 = (verts[vs[-2]][0] - verts[v][0],
                      verts[vs[-2]][1] - verts[v][1])
                arms.append((math.degrees(math.atan2(d9[1], d9[0])) % 360.0,
                             pa, 'b'))
        arms.sort(key=lambda t: t[0])
        for i in range(len(arms)):
            gap = (arms[(i + 1) % len(arms)][0] - arms[i][0]) % 360.0
            if gap < 16.0:
                raise ValueError("arms at vertex %d are only %.0f deg "
                                 "apart" % (v, gap))
        bear = [a[0] for a in arms]
        profs = [(a[1]['lanes'], a[1]['lw']) for a in arms]
        phantom = None
        if len(arms) == 3:
            wg = [(bear[(i + 1) % 3] - bear[i]) % 360.0 for i in range(3)]
            kw = max(range(3), key=lambda i: wg[i])
            ph_b = (bear[kw] + wg[kw] / 2.0) % 360.0
            order = sorted(range(4), key=lambda i: (bear + [ph_b])[i])
            bear2 = sorted(bear + [ph_b])
            phantom = bear2.index(ph_b)
            profs2 = []
            src_i = 0
            for i in range(4):
                if i == phantom:
                    profs2.append(profs[0])       # unused for the phantom
                else:
                    profs2.append(profs[src_i])
                    src_i += 1
            bear, profs = bear2, profs2
            del order
        ifaces = _ix_geo(shell, walk, bear, phantom, profs, sw, 0.0, rc,
                         origin=verts[v], network=True, dens=dens)
        info = {}
        ai = 0
        for (brg, ctr, ri) in ifaces:
            (b0, pa, end) = arms[ai]
            info[(id(pa), end)] = dict(center=ctr, bearing=brg, r=ri,
                                       z=verts[v][2])
            ai += 1
        juncs[v] = info

    # ---- sweep every path (welded into the same shells) --------------------
    def iface_for(pa, end):
        vs = pa['verts']
        v = vs[0] if end == 'a' else vs[-1]
        j9 = juncs.get(v)
        return None if j9 is None else j9.get((id(pa), end))

    for pa in paths:
        vs = pa['verts']
        pts = [verts[v] for v in vs]
        ifa = iface_for(pa, 'a')
        ifb = iface_for(pa, 'b') if not pa['closed'] else None
        for (iface, front) in ((ifa, True), (ifb, False)):
            if iface is None:
                continue
            seq = pts if front else pts[::-1]
            need = iface['r']
            out9 = [(iface['center'][0], iface['center'][1], iface['z'])]
            acc = 0.0
            for (a, b) in zip(seq, seq[1:]):
                d9 = math.dist(a[:2], b[:2])
                if acc + d9 > need:
                    out9.append(b)
                elif acc + d9 > need - 0.5:
                    pass
                acc += d9
            if len(out9) < 2:
                out9.append(seq[-1])
            pts = out9 if front else out9[::-1]
        if len(pts) < 2 or (not pa['closed'] and
                            math.dist(pts[0][:2], pts[-1][:2]) < 1.5):
            print("la_road_network: path too short after trims, skipped")
            continue
        if pa['closed'] and math.dist(pts[0][:2], pts[-1][:2]) < 1e-6:
            pts = pts[:-1]
        # acute bends fold the swept ribbon: round them to the full
        # cross-section half-width, then curvature-limit the stations
        (n_s9, _nn9, y0r9, _m09, _m19, y1r9, _zp9,
         _zr9) = _xsection(pa['lanes'], pa['lw'], 'none')
        R_lim = y1r9 + GUT_W + CURB_W + GAP + sw + 0.8
        pts = _fillet_polyline(pts, R_lim, pa['closed'])
        stations, Lt = _cr_stations(pts, 2.0, pa['closed'])
        # relax + RESAMPLE rounds: relaxation alone bunches stations at a
        # hairpin (Laplacian shrinkage) and the ribbon still folds; the
        # Catmull-Rom resample restores uniform spacing each round
        for _round in range(6):
            stations = _relax_stations(stations, R_lim, pa['closed'])
            worst = 1e9
            n9 = len(stations)
            rng9r = range(n9) if pa['closed'] else range(1, n9 - 1)
            for i9 in rng9r:
                a9 = stations[(i9 - 1) % n9][0]
                b9 = stations[i9][0]
                c9 = stations[(i9 + 1) % n9][0]
                la9 = math.dist(a9, b9)
                lb9 = math.dist(b9, c9)
                lc9 = math.dist(c9, a9)
                ar9 = abs((b9[0] - a9[0]) * (c9[1] - a9[1]) -
                          (c9[0] - a9[0]) * (b9[1] - a9[1])) / 2.0
                if ar9 > 1e-9:
                    worst = min(worst, la9 * lb9 * lc9 / (4.0 * ar9))
            pts2 = [(s[0][0], s[0][1], s[2]) for s in stations]
            stations, Lt = _cr_stations(pts2, 2.0, pa['closed'])
            if worst >= R_lim * 0.9:
                break
        cum9 = [0.0]
        for (a9, b9) in zip(stations, stations[1:]):
            cum9.append(cum9[-1] + math.dist(a9[0], b9[0]))
        Lt = cum9[-1]

        def pin(idx, d_pin, z_pin, into):
            stations[idx] = (stations[idx][0], d_pin, z_pin)
            for j9 in range(1, 4):
                i9 = idx + j9 * into
                if not 0 < i9 < len(stations) - 1:
                    break
                (pos9, d9, z9) = stations[i9]
                f9 = j9 / 4.0
                mx = d_pin[0] * (1 - f9) + d9[0] * f9
                my = d_pin[1] * (1 - f9) + d9[1] * f9
                ml = max(math.hypot(mx, my), 1e-9)
                stations[i9] = (pos9, (mx / ml, my / ml), z9)

        if ifa is not None:
            r9 = math.radians(ifa['bearing'])
            pin(0, (math.cos(r9), math.sin(r9)), ifa['z'], 1)
        if ifb is not None:
            r9 = math.radians(ifb['bearing'])
            pin(len(stations) - 1, (-math.cos(r9), -math.sin(r9)),
                ifb['z'], -1)

        # crown ramps from FLAT at a junction mouth (the pad is level)
        bl = min(6.0, Lt * 0.3)

        def crown_at(s9):
            amp = 1.0
            if ifa is not None and s9 < bl:
                amp = min(amp, s9 / bl)
            if ifb is not None and s9 > Lt - bl:
                amp = min(amp, (Lt - s9) / bl)
            return amp

        _sweep_road(shell, walk, stations, pa['lanes'], pa['lw'], sw,
                    p["paint_wear"], rng, n_rows=4 * dens,
                    crown_at=crown_at, jx_a=ifa is not None,
                    jx_b=ifb is not None, continuous_walk=True)

    src_name = src.name
    bpy.data.objects.remove(src, do_unlink=True)
    mats = [_material(nm) for nm in _MATS]
    return [shell.to_object("LA_RoadNet_%s_Road" % src_name, mats),
            walk.to_object("LA_RoadNet_%s_Walk" % src_name, mats)]


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


NET_SPEC = [
    params.MODE_PARAM,
    dict(name="sidewalk_width", type='FLOAT', default=3.0, min=1.5, max=5.0,
         unit='LENGTH'),
    dict(name="apron_length", type='FLOAT', default=6.0, min=3.0, max=12.0,
         unit='LENGTH'),
    dict(name="corner_radius", type='FLOAT', default=6.0, min=3.0, max=12.0,
         unit='LENGTH'),
    dict(name="paint_wear", type='FLOAT', default=0.4, min=0.0, max=1.0),
    dict(name="default_width", type='FLOAT', default=7.0, min=5.6, max=23.0,
         unit='LENGTH', desc="Road width used when the mesh carries no "
              "skin-modifier data"),
]

params.register_tool(idname="la_road_network",
                     label="Road Network (Skin Mesh)",
                     family="Streetscape", build=build_road_network,
                     spec=NET_SPEC, needs_context=True, at_cursor=False)
