"""D1 Street Section generator (rpg-psto) -- ferrum.la_street.

The curb-to-curb kit the lots plug into: crowned asphalt roadway, gutter
pans, curbs, HEAVED sidewalk slabs, lane paint with wear, median forms,
crosswalks/stop bars, asphalt patches, an optional sinkhole, alley mouths.

TOPOLOGY PLAN (welded-grid discipline)
======================================
The street runs along +X (length L); the section is laid out along Y with
z = 0 at the gutter flowline. ONE welded grid (shared global x/y line
families) covers roadway + gutter pans + curbs + median island -- every
feature that needs an x-split (sinkhole rect + ledge insets, scar band,
alley-mouth warps, median ends, regular ~5 m stations) contributes its
lines to the SAME family, so no edge ever crosses a foreign line.

Cross-section (south half; north mirrors):

      slab slab slab      curb                    crown
    ==================_  _ ____                     .
      heaved islands  |2mm|    |___                  ` .          (piecewise
                      gap      face `--..___gutter        ` .      linear)
                                            flowline z=0      ` z_pk
    |---- sw ----|      |0.15|  |--0.45--|------ lanes -------|

* CROWN: piecewise-linear z(y) -- z_e = 0.015 at the road edges rising to
  z_pk at the paint centerline / median edges. Every roadway row spans a
  single linear segment, so all surface cells are PLANAR quads. Paint and
  overlays are clipped at the kink lines for the same reason.
* SIDEWALK: independent slab islands (~1.7 m joints, 2 mm gaps, skirted
  sides) -- heave lifts/pitches each slab about ONE axis so tops stay
  planar, and the open joints read as real cracked expansion joints.
* PAINT / PATCHES: tagged overlay faces 1.5-3 mm above the surface
  (B1 lot-stripe pattern).
* SINKHOLE: the hole rect + its ledge insets join the global families;
  cells inside emit rim drops -> asphalt lip ledge -> shaft -> floor.
* MEDIAN 'palms': a welded curb-ring island (outer faces / top ring /
  inner drop / soil bed) sharing the m0/m1 rows and mx0/mx1 columns.
* INTERIOR MODE: the section reads as a SLICED street segment -- strata
  end caps at x = 0 / L (asphalt wear course, base course, soil bands),
  the sinkhole deepens with a soil ledge and an exposed utility pipe
  (open-ended octagonal prism, ends embedded 20 mm into the shaft walls).

Story options (off by default): checkpoint_scar (a ripped-out regime
checkpoint -- full-width resurfacing scar + sheared bollard stubs),
sand_lane (a drifted dune berm burying the south curb lane; its profile
carries a per-point BASE so the surface clears curb and sidewalk at the
tapered tips), and protest_stain (scorch blooms where barricades burned;
bloom verts follow the crown so they neither float nor embed).
"""
import bpy  # noqa: F401  (kept for parity with sibling tool modules)

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


def _cells(lines, lo, hi):
    """Consecutive (a, b) pairs of `lines` clipped to [lo, hi]."""
    seg = [v for v in lines if lo - 1e-9 <= v <= hi + 1e-9]
    return list(zip(seg, seg[1:]))


def _strip(shell, xs, x0, x1, y0, y1, zf, mat, tag, lift=0.0):
    """A planar overlay strip on the road surface: z = zf(y) + lift at both
    y edges (zf must be linear over [y0, y1]); split at global xs."""
    za, zb = zf(y0) + lift, zf(y1) + lift
    for (a, b) in _cells(xs, x0, x1):
        shell.quad((a, y0, za), (b, y0, za), (b, y1, zb), (a, y1, zb),
                   mat, tag)


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

    n_s = (lanes + 1) // 2            # south approach lanes (odd -> extra)
    n_n = lanes // 2
    med_w = {'none': 0.0, 'turn': 3.3, 'palms': 1.8}[med]
    road_w = lanes * lw + med_w
    y0r = -road_w / 2.0               # south road edge
    m0 = y0r + n_s * lw               # median south edge / paint centerline
    m1 = m0 + med_w
    y1r = m1 + n_n * lw               # north road edge
    z_e = 0.015
    z_pk = z_e + 0.015 * (n_s * lw)   # crown from the longer approach

    def z_road(y):
        if y <= m0 + 1e-9:
            t5 = (y - y0r) / max(m0 - y0r, 1e-9)
            return z_e + (z_pk - z_e) * min(max(t5, 0.0), 1.0)
        if y >= m1 - 1e-9:
            t5 = (y1r - y) / max(y1r - m1, 1e-9)
            return z_e + (z_pk - z_e) * min(max(t5, 0.0), 1.0)
        return z_pk

    y_cf_s = y0r - GUT_W              # curb face planes
    y_cf_n = y1r + GUT_W

    # ---- feature placement (seeded; clamped clear of each other) -----------
    hole = None
    if p["sinkhole"]:
        hw2 = min(1.5 + rng.random() * 0.9, max(0.9, (L - 9.4) / 2.0))
        hd2 = 1.1 + rng.random() * 0.5
        hcx = L * (0.55 + rng.random() * 0.15)
        hcx = min(max(hcx, 4.6 + hw2), L - 4.6 - hw2)   # clear of crosswalks
        hy_c = (y0r + m0) / 2.0       # south lanes
        hole = (hcx - hw2, hcx + hw2,
                max(y0r + 0.30, hy_c - hd2), min(m0 - 0.30, hy_c + hd2))
    scar = None
    if p["checkpoint_scar"]:
        sx0 = L * (0.22 + rng.random() * 0.08)
        if hole and sx0 + 4.0 > hole[0] - 0.5:
            sx0 = max(0.5, hole[0] - 4.5)
        scar = (sx0, sx0 + 4.0)
    mouths = []                       # (side sgn, x0, x1) incl. 0.7 warps
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
    k = 1
    while k * 5.0 < L - 0.5:
        xs.add(k * 5.0)
        k += 1
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
                continue              # sinkhole opening
            if in_med_rows and mx0 - 1e-9 <= a and b <= mx1 + 1e-9:
                continue              # under the raised median island
            shell.quad((a, ya, za), (b, ya, za), (b, yb, zb), (a, yb, zb),
                       M_ASPHALT)

    # ---- gutter pans + curbs (both sides) ----------------------------------
    def curb_top_z(sg, x):
        """Rolled-down curb across alley mouths, warped at the ends."""
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
            pts = [(a, y_re, z_e), (b, y_re, z_e),
                   (b, y_cf, 0.0), (a, y_cf, 0.0)]
            if sg < 0:                # keep the pan's normal +z
                pts.reverse()
            shell.quad(*pts, M_CONCRETE)
        shell.tag = 'curb'
        y_bk = y_cf + sg * CURB_W     # back-of-curb line (away from road)
        for (a, b) in _cells(xs, 0.0, L):
            cta, ctb = curb_top_z(sg, a), curb_top_z(sg, b)
            # face (toward the road)
            pts = [(a, y_cf, cta), (b, y_cf, ctb),
                   (b, y_cf, 0.0), (a, y_cf, 0.0)]
            if sg > 0:
                pts.reverse()
            shell.quad(*pts, M_CONCRETE)
            # top
            pts = [(a, y_cf, cta), (a, y_bk, cta),
                   (b, y_bk, ctb), (b, y_cf, ctb)]
            if sg > 0:
                pts.reverse()
            shell.quad(*pts, M_CONCRETE)
            # back (closes the 2 mm slab-gap view; drops to z=0 so the
            # interior end caps share a canonical split list)
            pts = [(a, y_bk, cta), (a, y_bk, 0.0),
                   (b, y_bk, 0.0), (b, y_bk, ctb)]
            if sg > 0:
                pts.reverse()
            shell.quad(*pts, M_CONCRETE)

    # ---- median island (palms form) ----------------------------------------
    if med_on:
        shell.tag = 'median'
        zt = z_pk + 0.18              # curb-ring top
        zs = z_pk + 0.08              # soil bed
        i0, i1 = m0 + CURB_W, m1 - CURB_W
        ix0, ix1 = mx0 + CURB_W, mx1 - CURB_W
        for (a, b) in _cells(xs, mx0, mx1):
            shell.quad((a, m0, z_pk), (b, m0, z_pk), (b, m0, zt),
                       (a, m0, zt), M_CONCRETE)                # S face (-y)
            shell.quad((a, m1, zt), (b, m1, zt), (b, m1, z_pk),
                       (a, m1, z_pk), M_CONCRETE)              # N face (+y)
        for xe, sgn2 in ((mx0, -1), (mx1, 1)):                 # end faces
            # split at i0/i1: the top ring and the roadway rows both put
            # verts on those lines -- an unsplit end edge T-junctions.
            for (ya, yb) in ((m0, i0), (i0, i1), (i1, m1)):
                pts = [(xe, ya, z_pk), (xe, yb, z_pk), (xe, yb, zt),
                       (xe, ya, zt)]
                if sgn2 < 0:
                    pts.reverse()     # west end faces -x
                shell.quad(*pts, M_CONCRETE)
        for (a, b) in _cells(xs, mx0, mx1):                    # top ring E-W
            for (ya, yb) in ((m0, i0), (i1, m1)):
                shell.quad((a, ya, zt), (b, ya, zt), (b, yb, zt),
                           (a, yb, zt), M_CONCRETE)
        for (ya, yb) in (((i0, i1)),):                         # top ring ends
            for (a, b) in ((mx0, ix0), (ix1, mx1)):
                shell.quad((a, ya, zt), (b, ya, zt), (b, yb, zt),
                           (a, yb, zt), M_CONCRETE)
        for (a, b) in _cells(xs, ix0, ix1):                    # inner drops
            shell.quad((a, i0, zt), (b, i0, zt), (b, i0, zs),
                       (a, i0, zs), M_CONCRETE)
            shell.quad((a, i1, zs), (b, i1, zs), (b, i1, zt),
                       (a, i1, zt), M_CONCRETE)
        for xe, sgn2 in ((ix0, -1), (ix1, 1)):
            pts = [(xe, i0, zt), (xe, i1, zt), (xe, i1, zs), (xe, i0, zs)]
            if sgn2 < 0:
                pts.reverse()         # west inner wall faces +x (into bed)
            shell.quad(*pts, M_CONCRETE)
        for (a, b) in _cells(xs, ix0, ix1):                    # soil bed
            shell.quad((a, i0, zs), (b, i0, zs), (b, i1, zs),
                       (a, i1, zs), M_SOIL, 'median')

    # ---- sinkhole ----------------------------------------------------------
    if hole:
        shell.tag = 'sinkhole'
        z_lip = -0.35                 # under the asphalt + base lip
        z_soil = -0.95                # interior soil ledge
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

        drops((hx0, hx1, hy0, hy1), None, z_lip, ztop_f=z_road)  # rim
        ring((hx0, hx1, hy0, hy1), l1, z_lip, M_CONCRETE)
        if interior_on:
            drops(l1, z_lip, z_soil)
            ring(l1, l2, z_soil, M_SOIL)
            drops(l2, z_soil, z_fl)
        else:
            drops(l1, z_lip, z_fl)
        for (a, b) in _cells(xs, l2[0], l2[1]):                  # floor
            for (ya, yb) in _cells(ys, l2[2], l2[3]):
                shell.quad((a, ya, z_fl), (b, ya, z_fl), (b, yb, z_fl),
                           (a, yb, z_fl), M_SOIL)
        for _r in range(3):                                      # rubble
            rx = l2[0] + 0.2 + rng.random() * max(l2[1] - l2[0] - 0.9, 0.1)
            ry = l2[2] + 0.2 + rng.random() * max(l2[3] - l2[2] - 0.8, 0.1)
            rs = 0.25 + rng.random() * 0.3
            _box(shell, (rx, ry, z_fl - 0.02),
                 (rx + rs, ry + rs * 0.8, z_fl + rs * 0.6), M_CONCRETE)
        if interior_on:
            # exposed utility pipe crossing the cavity along x, ends
            # embedded 20 mm into the shaft walls (open ends hidden).
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
                # the gutter/curb caps must SHARE the strata band splits:
                # their vertical edges weld to the roadway caps at y_re
                # and to each other at y_cf.
                north = y_cf > 0.0
                for (d0, d1, m9) in strata:
                    pts = [(xe, y_re, z_e - d0), (xe, y_cf, -d0),
                           (xe, y_cf, -d1), (xe, y_re, z_e - d1)]
                    if (sgn2 > 0) == north:
                        pts.reverse()
                    shell.quad(*pts, m9 if d0 > 0.2 else M_CONCRETE,
                               'gutter')
                sg9 = 1 if north else -1
                ct = curb_top_z(sg9, xe)
                y_bk = y_cf + sg9 * CURB_W
                # the ct->0 band matches the curb face/back end edges;
                # the rest mirrors the strata splits.
                for (d0, d1) in ((-ct, 0.0), (0.0, 0.12), (0.12, 0.45),
                                 (0.45, 1.05)):
                    pts = [(xe, y_cf, -d0), (xe, y_bk, -d0),
                           (xe, y_bk, -d1), (xe, y_cf, -d1)]
                    if (sgn2 > 0) == north:
                        pts.reverse()
                    shell.quad(*pts, M_CONCRETE if d1 < 0.5 else M_SOIL,
                               'curb')

    # ---- paint (overlays, +3 mm) -------------------------------------------
    shell.tag = 'paint'

    def dashes(y_l, mat):
        x = 1.0 + rng.random() * 3.0
        while x + 1.2 < L - 1.0:
            dl = 2.4
            if rng.random() < wear * 0.30:
                dl = 1.0 + rng.random() * 1.0
            if rng.random() >= wear * 0.55:
                d1 = min(x + dl, L - 1.0)
                if not (hole and x < hx1 and d1 > hx0 and
                        hy0 - 0.1 < y_l < hy1 + 0.1):
                    _strip(shell, xs, x, d1, y_l - 0.06, y_l + 0.06,
                           z_road, mat, 'paint', 0.003)
            x += 2.4 + 4.8

    for k in range(1, n_s):
        dashes(y0r + k * lw, M_PAINT_W)
    for k in range(1, n_n):
        dashes(m1 + k * lw, M_PAINT_W)
    if med == 'none':
        for off in (-0.15, 0.05):
            _strip(shell, xs, 0.5, L - 0.5, m0 + off, m0 + off + 0.10,
                   z_road, M_PAINT_Y, 'paint', 0.003)
    elif med == 'turn':
        for y_l, sgn2 in ((m0, 1), (m1, -1)):
            _strip(shell, xs, 0.5, L - 0.5, y_l + sgn2 * 0.05,
                   y_l + sgn2 * 0.15, z_road, M_PAINT_Y, 'paint', 0.003)
            dashes(y_l + sgn2 * 0.35, M_PAINT_Y)

    cw_bands = []
    if p["crosswalks"] in ('near', 'both'):
        cw_bands.append((1.4, 4.1, 1))
    if p["crosswalks"] in ('far', 'both') and L > 14.0:
        cw_bands.append((L - 4.1, L - 1.4, -1))
    for (xa, xb, ap) in cw_bands:
        y = y0r + 0.30
        while y + 0.42 < y1r - 0.25:
            if rng.random() >= wear * 0.35:
                # split every bar at the crown kinks; skip the island span
                for (ca, cb) in ((max(y, y0r), min(y + 0.42, m0)),
                                 (max(y, m0), min(y + 0.42, m1)),
                                 (max(y, m1), min(y + 0.42, y1r))):
                    if cb - ca < 0.04:
                        continue
                    if med_on and ca >= m0 - 1e-9 and cb <= m1 + 1e-9:
                        continue
                    _strip(shell, xs, xa, xb, ca, cb, z_road,
                           M_PAINT_W, 'paint', 0.003)
            y += 1.0
        bx = (xa - 1.25, xa - 0.80) if ap > 0 else (xb + 0.80, xb + 1.25)
        if 0.0 < bx[0] and bx[1] < L:                # approach stop bar
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

    road_ob = shell.to_object("LA_Street_Road", mats)

    # ---- sidewalk slabs + alley aprons (independent islands) ---------------
    walk = _Shell()
    walk.tag = 'sidewalk'
    for sg, y_cf in ((-1, y_cf_s), (1, y_cf_n)):
        y_in = y_cf + sg * (CURB_W + GAP)         # slab edge at curb back
        y_out = y_in + sg * sw
        ya, yb = min(y_in, y_out), max(y_in, y_out)
        n_sl = max(2, round(L / SLAB_L))
        pitch = L / n_sl
        sk = 0.11                                  # skirt bottom
        for i9 in range(n_sl):
            a = i9 * pitch + GAP
            b = (i9 + 1) * pitch - GAP
            if any(msg == sg and a < mb and b > ma
                   for (msg, ma, mb) in mouths):
                continue                           # apron replaces these
            lift = heave * rng.random() * 0.045
            tilt = heave * (rng.random() - 0.5) * 0.06
            if rng.random() < 0.5:                 # pitch about y axis
                z00 = z01 = Z_WALK + lift - tilt / 2.0
                z10 = z11 = Z_WALK + lift + tilt / 2.0
            else:                                  # pitch about x axis
                z00 = z10 = Z_WALK + lift - tilt / 2.0
                z01 = z11 = Z_WALK + lift + tilt / 2.0
            walk.quad((a, ya, z00), (b, ya, z10), (b, yb, z11),
                      (a, yb, z01), M_CONCRETE)                # top
            walk.quad((a, ya, sk), (b, ya, sk), (b, ya, z10),
                      (a, ya, z00), M_CONCRETE)                # -y skirt
            walk.quad((b, yb, sk), (a, yb, sk), (a, yb, z01),
                      (b, yb, z11), M_CONCRETE)                # +y skirt
            walk.quad((a, yb, sk), (a, ya, sk), (a, ya, z00),
                      (a, yb, z01), M_CONCRETE)                # -x skirt
            walk.quad((b, ya, sk), (b, yb, sk), (b, yb, z11),
                      (b, ya, z10), M_CONCRETE)                # +x skirt
    for (msg, ma, mb) in mouths:                   # sloped alley aprons
        y_cf = y_cf_s if msg < 0 else y_cf_n
        y_in = y_cf + msg * (CURB_W + GAP)
        y_out = y_in + msg * sw
        z_hi, z_lo = Z_WALK, 0.045                 # back -> rolled curb
        a, b = ma + GAP, mb - GAP
        if msg < 0:
            ya2, za2, yb2, zb2 = y_out, z_hi, y_in, z_lo
        else:
            ya2, za2, yb2, zb2 = y_in, z_lo, y_out, z_hi
        walk.quad((a, ya2, za2), (b, ya2, za2), (b, yb2, zb2),
                  (a, yb2, zb2), M_CONCRETE)                   # sloped top
        for (xe, flip) in ((a, False), (b, True)):
            pts = [(xe, ya2, za2), (xe, yb2, zb2), (xe, yb2, 0.02),
                   (xe, ya2, 0.02)]
            if flip:
                pts.reverse()
            walk.quad(*pts, M_CONCRETE)
        walk.quad((a, ya2, 0.02), (b, ya2, 0.02), (b, ya2, za2),
                  (a, ya2, za2), M_CONCRETE)                   # -y skirt
        walk.quad((b, yb2, 0.02), (a, yb2, 0.02), (a, yb2, zb2),
                  (b, yb2, zb2), M_CONCRETE)                   # +y skirt
    walk_ob = walk.to_object("LA_Street_Walk", mats)

    # ---- story dressing ----------------------------------------------------
    out = [road_ob, walk_ob]
    story = _Shell()
    story.tag = 'story'
    any_story = False
    if scar:
        any_story = True
        for (ya, yb) in ((y0r + 0.05, m0), (m1, y1r - 0.05)):
            _strip(story, xs, scar[0], scar[1], ya, yb, z_road,
                   M_PATCH, 'story', 0.0018)
        for rx in (scar[0] + 0.7, scar[1] - 0.7):
            y = y0r + 0.6
            while y < y1r - 0.4:
                if not (med_w and m0 - 0.2 < y < m1 + 0.2):
                    zb2 = z_road(y)
                    _box(story, (rx - 0.09, y - 0.09, zb2 - 0.02),
                         (rx + 0.09, y + 0.09, zb2 + 0.22), M_METAL)
                y += 1.2
    if p["sand_lane"]:
        any_story = True
        bx0, bx1 = L * 0.12, L * 0.88
        # (y, base, height): base clears whatever the berm crosses (slab
        # backs, curb top, gutter, crowned lane) even at the taper tips.
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
                continue              # blooms never cross a crown kink
            ch = r9 * 0.42            # chamfered-corner bloom (all quads)
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
    if any_story:
        out.append(story.to_object("LA_Street_Story", mats))
    else:
        story.bm.free()
    return out


SPEC = [
    params.MODE_PARAM,
    dict(name="length", type='FLOAT', default=30.0, min=12.0, max=60.0,
         unit='LENGTH', desc="Segment length along the street"),
    dict(name="lanes", type='INT', default=4, min=2, max=6,
         desc="Total travel lanes (odd counts widen the south approach)"),
    dict(name="lane_width", type='FLOAT', default=3.3, min=2.8, max=3.8,
         unit='LENGTH'),
    dict(name="sidewalk_width", type='FLOAT', default=3.0, min=1.5, max=5.0,
         unit='LENGTH'),
    dict(name="heave", type='FLOAT', default=0.5, min=0.0, max=1.0,
         desc="Sidewalk slab lift/tilt at the expansion joints"),
    dict(name="paint_wear", type='FLOAT', default=0.4, min=0.0, max=1.0,
         desc="Fraction of paint dashes/bars missing or shortened"),
    dict(name="patches", type='FLOAT', default=0.35, min=0.0, max=1.0,
         desc="Asphalt patch density on the roadway"),
    # -- monotony breakers --
    dict(name="median", type='ENUM', default='none',
         items=('none', 'turn', 'palms'),
         desc="Center form: painted centerline, two-way turn lane, or a "
              "raised curb island with a soil bed (palms plug in via D3)"),
    dict(name="crosswalks", type='ENUM', default='near',
         items=('none', 'near', 'far', 'both'),
         desc="Continental crosswalk + stop bar at the segment ends"),
    dict(name="alley_mouths", type='ENUM', default='none',
         items=('none', 'south', 'north', 'both'),
         desc="Rolled-curb alley apron through the sidewalk"),
    dict(name="sinkhole", type='BOOL', default=False,
         desc="A collapsed rectangle of roadway: rim, ledge, shaft, rubble "
              "(interior mode deepens it and exposes a utility pipe)"),
    # -- story options (off by default) --
    dict(name="checkpoint_scar", type='BOOL', default=False,
         desc="A ripped-out regime checkpoint: full-width resurfacing scar "
              "+ sheared bollard stubs"),
    dict(name="sand_lane", type='BOOL', default=False,
         desc="A dune berm drifted against the south curb, burying a lane"),
    dict(name="protest_stain", type='BOOL', default=False,
         desc="Scorch blooms on the asphalt where barricades burned"),
]

params.register_tool(idname="la_street", label="Street Section",
                     family="Streetscape", build=build_street, spec=SPEC)
