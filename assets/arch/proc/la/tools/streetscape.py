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
  the same lanes/lane_width/sidewalk_width join seamlessly. Terrain
  profiles are zeroed at x = 0 so the start interface stays at the
  cursor. When bending or terrain-fitting, extra x stations densify the
  grid; all overlays share the SAME piecewise mapping, so they ride
  exactly 3 mm above the surface everywhere.
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
    inv = ob.matrix_world.inverted()
    n9 = max(2, int(round(L / step)))
    st9 = [L * i / n9 for i in range(n9 + 1)]
    hs = []
    from mathutils import Vector
    for x in st9:
        wx, wy = bend(x, 0.0)
        origin = inv @ Vector((cursor[0] + wx, cursor[1] + wy, 10000.0))
        direction = inv.to_3x3() @ Vector((0.0, 0.0, -1.0))
        hit, loc, _n, _i = ob.ray_cast(origin, direction)
        hs.append((ob.matrix_world @ loc).z - cursor[2] if hit else None)
    last = next((h for h in hs if h is not None), 0.0)
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
    h0 = hs[0]
    hs = [h - h0 for h in hs]                    # start stays at cursor

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
    step = 5.0
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
    for k in range(1, n_n):
        ys.add(m1 + k * lw)
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


def build_intersection(p, rng):
    """N-way intersection: flat pad at Z_E, per-arm crown-blend aprons,
    filleted curb returns (concave, convex, or straight seams), sidewalk
    wedges, stop bars + transverse crosswalks. The outer apron end is the
    exact straight-street cross-section, so la_street segments plug on.
    Both modes emit the same geometry (an open pavement kit has no
    enclosed structure to add in interior mode)."""
    del rng
    lanes = p["lanes"]
    lw = p["lane_width"]
    sw = p["sidewalk_width"]
    apron = p["apron_length"]
    rc = p["corner_radius"]
    n_s, n_n, y0r, m0, m1, y1r, z_pk, z_road = _xsection(lanes, lw, 'none')
    WL = y1r + GUT_W                  # left curb-face offset
    WR = -y0r + GUT_W                 # right curb-face offset (positive)
    bear = sorted(b % 360.0 for b in _arm_bearings(p["form"], p["skew"]))
    n_arm = len(bear)
    mats = [_material(nm) for nm in _MATS]

    def frame(deg):
        r = math.radians(deg)
        return ((math.cos(r), math.sin(r)),
                (-math.sin(r), math.cos(r)))      # (dir, left normal)

    frames = [frame(b) for b in bear]

    def pt(k, t, u, z):
        d, nl = frames[k]
        return (t * d[0] + u * nl[0], t * d[1] + u * nl[1], z)

    # ---- fillet math per wedge (arm k left curb vs arm k+1 right curb) ----
    fil = []
    t_need = 1.0
    for k in range(n_arm):
        k2 = (k + 1) % n_arm
        (da, na), (db, nb) = frames[k], frames[k2]
        wedge = (bear[k2] - bear[k]) % 360.0
        if abs(wedge - 180.0) <= 8.0:
            fil.append(None)          # straight seam
            continue
        pv = 1.0 if wedge < 180.0 else -1.0       # concave / convex
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
        if pv > 0:                    # concave: walk cw around C
            while a1 >= a0:
                a1 -= 2.0 * math.pi
        else:                         # convex: walk ccw around C
            while a1 <= a0:
                a1 += 2.0 * math.pi
        na_seg = max(4, int(abs(a1 - a0) / 0.30) + 1)
        fil.append(dict(C=C, ta=ta, tb=tb, a0=a0, a1=a1, pv=pv,
                        na=na_seg))
        t_need = max(t_need, ta, tb)
    r0 = t_need + 0.4                 # shared mouth radius, all arms
    r1 = r0 + apron

    # per-arm side t-ranges for gutter/curb strips: (left, right); a seam
    # extends arm k's LEFT strips across to the opposite mouth and skips
    # arm k2's right strips entirely (same line, one owner).
    t_left = [fil[k]['ta'] if fil[k] else -r0 for k in range(n_arm)]
    t_right = [fil[(k - 1) % n_arm]['tb'] if fil[(k - 1) % n_arm] else None
               for k in range(n_arm)]

    rows = sorted({y0r, m0, y1r}
                  | {y0r + k9 * lw for k9 in range(1, n_s)}
                  | {m1 + k9 * lw for k9 in range(1, n_n)})

    def arc_pts(f9, radius):
        return [(f9['C'][0] + radius * math.cos(
                    f9['a0'] + (f9['a1'] - f9['a0']) * i9 / f9['na']),
                 f9['C'][1] + radius * math.sin(
                    f9['a0'] + (f9['a1'] - f9['a0']) * i9 / f9['na']))
                for i9 in range(f9['na'] + 1)]

    def boundary():
        """CCW pad boundary at z = Z_E: mouth chords + curb-inner segments
        + inner-gutter arcs. Consecutive duplicates dropped."""
        bnd = []

        def push(q):
            if not bnd or abs(q[0] - bnd[-1][0]) > 1e-6 or \
                    abs(q[1] - bnd[-1][1]) > 1e-6:
                bnd.append(q)

        for k in range(n_arm):
            k2 = (k + 1) % n_arm
            for y in rows:            # mouth chord, right -> left
                push(pt(k, r0, y, Z_E))
            f9 = fil[k]
            if f9 is None:            # seam: inner edge runs straight
                # across to the opposite arm's mouth corner
                push(pt(k2, r0, y0r, Z_E))
                continue
            push(pt(k, f9['ta'], y1r, Z_E))       # in along left inner edge
            for q in arc_pts(f9, rc + f9['pv'] * GUT_W)[1:]:
                push((q[0], q[1], Z_E))           # inner-gutter arc
            push(pt(k2, r0, y0r, Z_E))            # out along right edge
        if len(bnd) > 2 and abs(bnd[0][0] - bnd[-1][0]) < 1e-6 and \
                abs(bnd[0][1] - bnd[-1][1]) < 1e-6:
            bnd.pop()
        return bnd

    bnd = boundary()
    if len(bnd) % 2 == 1:             # quad-only disk: even boundary count
        for f9 in fil:
            if f9 is not None:
                f9['na'] += 1
                break
        bnd = boundary()

    shell = _Shell()
    walk = _Shell()

    # ---- per-arm aprons ----------------------------------------------------
    for k in range(n_arm):
        cols = [r0 + apron * i9 / 4.0 for i9 in range(5)]

        def zmix(r, y):
            u9 = (r - r0) / (r1 - r0)
            return Z_E + u9 * (z_road(y) - Z_E)

        shell.tag = 'road'
        for (ca, cb) in zip(cols, cols[1:]):
            for (ya, yb) in zip(rows, rows[1:]):
                shell.quad(pt(k, ca, ya, zmix(ca, ya)),
                           pt(k, cb, ya, zmix(cb, ya)),
                           pt(k, cb, yb, zmix(cb, yb)),
                           pt(k, ca, yb, zmix(ca, yb)), M_ASPHALT)
        for (side_t, ye, yc, sg) in ((t_right[k], y0r, y0r - GUT_W, -1),
                                     (t_left[k], y1r, y1r + GUT_W, 1)):
            if side_t is None:
                continue              # seam: the opposite arm owns it
            scols = sorted({side_t, r0} | set(cols))
            shell.tag = 'gutter'
            for (ca, cb) in zip(scols, scols[1:]):
                za = zmix(ca, ye) if ca >= r0 else Z_E
                zb = zmix(cb, ye) if cb >= r0 else Z_E
                pts = [pt(k, ca, ye, za), pt(k, cb, ye, zb),
                       pt(k, cb, yc, 0.0), pt(k, ca, yc, 0.0)]
                if sg < 0:
                    pts.reverse()
                shell.quad(*pts, M_CONCRETE)
            shell.tag = 'curb'
            y_bk = yc + sg * CURB_W
            for (ca, cb) in zip(scols, scols[1:]):
                pts = [pt(k, ca, yc, CURB_H), pt(k, cb, yc, CURB_H),
                       pt(k, cb, yc, 0.0), pt(k, ca, yc, 0.0)]
                if sg > 0:
                    pts.reverse()
                shell.quad(*pts, M_CONCRETE)      # face
                pts = [pt(k, ca, yc, CURB_H), pt(k, ca, y_bk, CURB_H),
                       pt(k, cb, y_bk, CURB_H), pt(k, cb, yc, CURB_H)]
                if sg > 0:
                    pts.reverse()
                shell.quad(*pts, M_CONCRETE)      # top
                pts = [pt(k, ca, y_bk, CURB_H), pt(k, ca, y_bk, 0.0),
                       pt(k, cb, y_bk, 0.0), pt(k, cb, y_bk, CURB_H)]
                if sg > 0:
                    pts.reverse()
                shell.quad(*pts, M_CONCRETE)      # back
            walk.tag = 'sidewalk'                 # slab islands along side
            yi = yc + sg * (CURB_W + GAP)
            yo = yi + sg * sw
            wy0, wy1 = min(yi, yo), max(yi, yo)
            w_lo = min(side_t + GAP, r0)
            nsl = max(1, round((r1 - w_lo) / SLAB_L))
            for i9 in range(nsl):
                a = w_lo + (r1 - w_lo) * i9 / nsl + GAP
                b = w_lo + (r1 - w_lo) * (i9 + 1) / nsl - GAP
                walk.quad(pt(k, a, wy0, Z_WALK), pt(k, b, wy0, Z_WALK),
                          pt(k, b, wy1, Z_WALK), pt(k, a, wy1, Z_WALK),
                          M_CONCRETE)
                corners = [(a, wy0), (b, wy0), (b, wy1), (a, wy1)]
                for (c0, c1) in zip(corners, corners[1:] + corners[:1]):
                    walk.quad(pt(k, c1[0], c1[1], 0.11),
                              pt(k, c0[0], c0[1], 0.11),
                              pt(k, c0[0], c0[1], Z_WALK),
                              pt(k, c1[0], c1[1], Z_WALK), M_CONCRETE)
        # ---- paint: centerline, stop bar, transverse crosswalk ------------
        shell.tag = 'paint'

        def pquad(ta9, tb9, ya, yb, mat):
            # split at the crown kink so the overlay never dips under the
            # blended surface
            for (ca, cb) in ((ya, min(yb, m0)), (max(ya, m0), yb)):
                if cb - ca < 0.02:
                    continue
                shell.quad(pt(k, ta9, ca, zmix(ta9, ca) + 0.003),
                           pt(k, tb9, ca, zmix(tb9, ca) + 0.003),
                           pt(k, tb9, cb, zmix(tb9, cb) + 0.003),
                           pt(k, ta9, cb, zmix(ta9, cb) + 0.003),
                           mat, 'paint')

        for off in ((-0.15, -0.05), (0.05, 0.15)):
            pquad(r0 + 3.6, r1 - 0.2, m0 + off[0], m0 + off[1], M_PAINT_Y)
        pquad(r0 + 0.6, r0 + 0.75, y0r + 0.25, y1r - 0.25, M_PAINT_W)
        pquad(r0 + 2.6, r0 + 2.75, y0r + 0.25, y1r - 0.25, M_PAINT_W)
        pquad(r0 + 3.0, r0 + 3.45, m0 + 0.1, y1r - 0.25, M_PAINT_W)

    # ---- fillet arc strips: gutter, curb, sidewalk wedge -------------------
    for k in range(n_arm):
        f9 = fil[k]
        if f9 is None:
            continue
        pv = f9['pv']
        gi = arc_pts(f9, rc + pv * GUT_W)         # gutter inner (pavement)
        cf = arc_pts(f9, rc)                      # curb face
        cb2 = arc_pts(f9, rc - pv * CURB_W)       # curb back
        w0 = arc_pts(f9, rc - pv * (CURB_W + GAP))
        w1 = arc_pts(f9, rc - pv * (CURB_W + GAP + sw))
        na_seg = f9['na']
        shell.tag = 'gutter'
        for i9 in range(na_seg):
            pts = [(gi[i9][0], gi[i9][1], Z_E),
                   (gi[i9 + 1][0], gi[i9 + 1][1], Z_E),
                   (cf[i9 + 1][0], cf[i9 + 1][1], 0.0),
                   (cf[i9][0], cf[i9][1], 0.0)]
            if pv < 0:
                pts.reverse()
            shell.quad(*pts, M_CONCRETE)
        shell.tag = 'curb'
        for i9 in range(na_seg):
            pts = [(cf[i9][0], cf[i9][1], CURB_H),
                   (cf[i9 + 1][0], cf[i9 + 1][1], CURB_H),
                   (cf[i9 + 1][0], cf[i9 + 1][1], 0.0),
                   (cf[i9][0], cf[i9][1], 0.0)]
            if pv < 0:
                pts.reverse()
            shell.quad(*pts, M_CONCRETE)          # face
            pts = [(cf[i9 + 1][0], cf[i9 + 1][1], CURB_H),
                   (cf[i9][0], cf[i9][1], CURB_H),
                   (cb2[i9][0], cb2[i9][1], CURB_H),
                   (cb2[i9 + 1][0], cb2[i9 + 1][1], CURB_H)]
            if pv < 0:
                pts.reverse()
            shell.quad(*pts, M_CONCRETE)          # top
            pts = [(cb2[i9][0], cb2[i9][1], CURB_H),
                   (cb2[i9][0], cb2[i9][1], 0.0),
                   (cb2[i9 + 1][0], cb2[i9 + 1][1], 0.0),
                   (cb2[i9 + 1][0], cb2[i9 + 1][1], CURB_H)]
            if pv < 0:
                pts.reverse()
            shell.quad(*pts, M_CONCRETE)          # back
        walk.tag = 'sidewalk'
        r_in = rc - pv * (CURB_W + GAP)
        r_out = rc - pv * (CURB_W + GAP + sw)
        span9 = f9['a1'] - f9['a0']
        ga = math.copysign(0.0025 / max(min(abs(r_in), abs(r_out)), 1.0),
                           span9)
        for i9 in range(na_seg):
            # angular 2 mm insets make each arc slab its own island --
            # welded radial edges collapse the facing skirts into one
            # face and leave 3-face nonmanifold edges.
            aa0 = f9['a0'] + span9 * i9 / na_seg + ga
            aa1 = f9['a0'] + span9 * (i9 + 1) / na_seg - ga
            q = []
            for (rr, aa) in ((r_in, aa0), (r_in, aa1),
                             (r_out, aa1), (r_out, aa0)):
                q.append((f9['C'][0] + rr * math.cos(aa),
                          f9['C'][1] + rr * math.sin(aa)))
            walk.quad((q[0][0], q[0][1], Z_WALK), (q[1][0], q[1][1], Z_WALK),
                      (q[2][0], q[2][1], Z_WALK), (q[3][0], q[3][1], Z_WALK),
                      M_CONCRETE)
            for (v0, v1) in zip(q, q[1:] + q[:1]):
                walk.quad((v1[0], v1[1], 0.11), (v0[0], v0[1], 0.11),
                          (v0[0], v0[1], Z_WALK), (v1[0], v1[1], Z_WALK),
                          M_CONCRETE)

    # ---- pad fill: boundary -> scaled ring -> zipper cap -------------------
    shell.tag = 'road'
    M = len(bnd)
    cx = sum(q[0] for q in bnd) / M
    cy = sum(q[1] for q in bnd) / M
    ring2 = [(cx + (q[0] - cx) * 0.55, cy + (q[1] - cy) * 0.55) for q in bnd]
    for i9 in range(M):
        j9 = (i9 + 1) % M
        shell.quad((bnd[i9][0], bnd[i9][1], Z_E),
                   (bnd[j9][0], bnd[j9][1], Z_E),
                   (ring2[j9][0], ring2[j9][1], Z_E),
                   (ring2[i9][0], ring2[i9][1], Z_E), M_ASPHALT)
    # zipper cap (even M). The start index matters: pairing verts that all
    # lie on one colinear boundary run (an arm's mouth chord) makes
    # zero-area quads -- score every rotation and keep the one whose
    # worst quad is largest.
    def zip_quads(s9):
        out9 = []
        for i9 in range(M // 2 - 1):
            a9 = (s9 + i9) % M
            b9 = (s9 + i9 + 1) % M
            c9 = (s9 - i9 - 2) % M
            d9 = (s9 - i9 - 1) % M
            out9.append((ring2[a9], ring2[b9], ring2[c9], ring2[d9]))
        return out9

    def quad_area(q4):
        (ax, ay), (bx, by), (cx2, cy2), (dx, dy) = q4
        return 0.5 * abs((cx2 - ax) * (dy - by) - (dx - bx) * (cy2 - ay))

    best = max(range(M), key=lambda s9: min(quad_area(q4)
                                            for q4 in zip_quads(s9)))
    for q4 in zip_quads(best):
        shell.quad((q4[0][0], q4[0][1], Z_E), (q4[1][0], q4[1][1], Z_E),
                   (q4[2][0], q4[2][1], Z_E), (q4[3][0], q4[3][1], Z_E),
                   M_ASPHALT)

    road_ob = shell.to_object("LA_Intersection_Road", mats)
    walk_ob = walk.to_object("LA_Intersection_Walk", mats)
    return [road_ob, walk_ob]

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
