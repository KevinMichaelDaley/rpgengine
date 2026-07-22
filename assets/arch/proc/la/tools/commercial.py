"""Family B -- commercial. B1: Mini-Mall (rpg-ezcn).

TOPOLOGY PLAN (rule: diagram before code).

One welded body shell on a GLOBAL LINE GRID (the A1 discipline): every
z-level runs around the whole outline, every pier/jamb owns an x (or y)
line, openings are grid CELLS. The footprint is a rectangle or an L:

      0                    Wm        Wm+D
   D  +---------------------+---------+      rear (service doors per tenant)
      |  t0  |  t1  |  t2   |  wing   |
      |      |      |       |  t3     |
   0  +==+===+===+===+===+==+   t4    |      == storefront (main, faces -y)
         canopy/deck         |        |
         walkway             +========+ -Wy  storefront (wing, faces -x)
         parking (striped, in the crook)

STOREFRONT BAY (one tenant, elevation; z-lines global):
  zPAR  +--------------------------+   parapet top (cap ring, cell-based)
  zROOF |        (roof plane)      |
  3.9   +--------------------------+   fascia top   [+ office storey above
  3.0   +--+====+==+============+--+   storefront head  when office_strip]
        |P |door|tr|  glazing   |P |   tr = transom ('window_transom' kind:
  2.6   |I +----+--+            |I |   a DIFFERENT kind from the glazing so
  0.25  |E |    |  +------------+E |   the 2D run absorption stays a
  0.0   +--+----+--+--bulkhead--+--+   RECTANGLE union -- an L-shaped same-
                                       kind region double-emits)
  Shuttered tenants: the whole opening is one 'window_shutter' cell run
  (ring + horizontal slat island at half reveal depth).

ROOF: cell-classified over the global grid -- a cell is CAP (parapet top,
zPAR) when inside the outline but within t of it, ROOF (zROOF plane)
otherwise; vertical DROP quads are emitted per cap/roof cell boundary
segment, so every edge welds and the L crook needs no special casing.
The t-inset lines are members of the grid, so no cell straddles the band.

OFFICE STRIP (breaker): a full second storey -- the classic 80s mini-mall.
The canopy thickens into the balcony DECK (top flush with the floor-2
line, doors flush, no threshold), solid stucco balcony rail, straight
footed-stringer flight from the lot to a deck-end landing.

Validation: la.topology.validate() -- 100% quads, 0 ngons/tris,
0 non-manifold, 0 doubles, 0 T-junctions on every emitted mesh.
"""
import bpy

from .. import params
from .. import topology
from ..geom import (
    _MATS, _Shell, _Wall, _box, _material, _sheared_box, _wall_solid,
    M_CONCRETE, M_GLASS, M_METAL, M_SHUTTER, M_SIGN_A, M_SIGN_B, M_SIGN_C,
    M_STUCCO, M_TRIM,
)

# fixed z-levels (metres) -- see the topology plan above.
Z_BLK, Z_SRV, Z_DR, Z_SF, Z_FAS = 0.25, 2.1, 2.6, 3.0, 3.9
# office storey rows (only in the zl when office_strip).
Z_ODR, Z_OSIL, Z_OHED = 6.0, 4.8, 6.0


def _tenant_bays(run_len, n, end_pier=0.30, mid_pier=0.60):
    """Per-tenant (bay0, bay1, open0, open1) along a storefront run."""
    tw = run_len / n
    bays = []
    for i in range(n):
        b0, b1 = i * tw, (i + 1) * tw
        o0 = b0 + (end_pier if i == 0 else mid_pier / 2.0)
        o1 = b1 - (end_pier if i == n - 1 else mid_pier / 2.0)
        bays.append((b0, b1, o0, o1))
    return bays


def _storefront_classify(bays, doors, fates, z_top, office,
                         void_span=None, recessed=frozenset(), wt=0.15,
                         run_w=1e9):
    """Classifier factory for one storefront wall (u = along-run coord).

    fates[i]: 'open' | 'shut' | 'void' (checkpoint / fortified bays).
    z_top: storefront head (Z_SF). office: adds the floor-2 door/window row.
    """
    def classify(u0, zc0):
        if void_span and void_span[0] - 1e-6 <= u0 < void_span[1] - 1e-6:
            return 'void'
        if office and Z_FAS - 1e-6 <= zc0 < Z_OHED - 1e-6:
            # recessed-loggia bays: floor-2 void; the strips one wall-
            # thickness either side keep the outer skin only (the room
            # corner turns at the cheek -- the dingbat loggia pattern).
            for i in recessed:
                (_b0, _b1, o0, o1) = bays[i]
                if o0 - 1e-6 <= u0 < o1 - 1e-6:
                    return 'void'
                if (max(o0 - wt, wt + 0.05) - 1e-6 <= u0 <
                        min(o1 + wt, run_w - wt - 0.05) - 1e-6):
                    return 'void_in'
        for i, (_b0, _b1, o0, o1) in enumerate(bays):
            if not (o0 - 1e-6 <= u0 < o1 - 1e-6):
                continue
            if zc0 < z_top - 1e-6:
                fate = fates[i]
                if fate == 'void':
                    return 'void'
                if fate == 'shut':
                    return 'window_shutter'
                d0, d1 = doors[i]
                if d0 - 1e-6 <= u0 < d1 - 1e-6:
                    if abs(zc0) < 1e-6:
                        return 'doorL'
                    if zc0 < Z_DR - 1e-6:
                        return 'doorU'
                    return 'window_transom'
                if zc0 < Z_BLK - 1e-6:
                    return 'wall'          # bulkhead under the glass
                return 'window'
            if office and Z_FAS - 1e-6 <= zc0 < Z_OHED - 1e-6:
                # floor-2: balcony door + ribbon window per bay.
                d0, d1 = doors[i]
                if d0 - 1e-6 <= u0 < d1 - 1e-6:
                    if abs(zc0 - Z_FAS) < 1e-6:
                        return 'doorL'
                    return 'doorU'
                if Z_OSIL - 1e-6 <= zc0:
                    return 'window'
            return 'wall'
        return 'wall'
    return classify


def _roof_cells(shell, xl, yl, inside, z_roof, z_par, t, in_peak=None):
    """Cell-classified flat roof: CAP ring at z_par within t of the outline,
    ROOF plane at z_roof elsewhere, vertical DROP quads at every cap/roof
    boundary segment. Requires the t-inset lines to be grid members.
    Cap cells matched by `in_peak` emit NO plane -- a welded _peak_wall
    covers that span (they still act as cap for the drop faces)."""
    def kind(cx, cy):
        if not inside(cx, cy):
            return None
        # probe axes AND diagonals: axis-only probes classify the cell at
        # a CONCAVE outline corner as roof, which puts a cap drop face on
        # the storefront wall plane (3 faces per edge).
        for dx in (-t, 0.0, t):
            for dy in (-t, 0.0, t):
                if not inside(cx + dx, cy + dy):
                    if in_peak is not None and in_peak(cx, cy):
                        return 'peak'
                    return 'cap'
        return 'roof'

    kinds = {}
    for ix in range(len(xl) - 1):
        for iy in range(len(yl) - 1):
            cx = (xl[ix] + xl[ix + 1]) / 2.0
            cy = (yl[iy] + yl[iy + 1]) / 2.0
            kinds[(ix, iy)] = kind(cx, cy)
    shell.tag = 'parapet'
    for (ix, iy), k in kinds.items():
        if k is None:
            continue
        x0, x1 = xl[ix], xl[ix + 1]
        y0, y1 = yl[iy], yl[iy + 1]
        z = z_par if k == 'cap' else z_roof
        if k != 'peak':
            if k == 'roof':
                shell.tag = 'roof'
            shell.quad((x0, y0, z), (x1, y0, z), (x1, y1, z), (x0, y1, z),
                       M_STUCCO if k == 'cap' else M_CONCRETE)
            shell.tag = 'parapet'
        # drop faces toward lower / absent neighbours in +x / +y only
        # (each boundary visited once). 'peak' cells act as cap here.
        kc = 'cap' if k == 'peak' else k
        for (jx, jy, axis) in ((ix + 1, iy, 'x'), (ix, iy + 1, 'y')):
            nk = kinds.get((jx, jy))
            nk = 'cap' if nk == 'peak' else nk
            k = kc
            if not (k == 'cap' and nk == 'roof') and \
                    not (k == 'roof' and nk == 'cap'):
                continue
            if axis == 'x':
                pts = [(x1, y0, z_roof), (x1, y1, z_roof),
                       (x1, y1, z_par), (x1, y0, z_par)]
                if k == 'cap':          # face toward +x (the roof side)
                    pts.reverse()
            else:
                pts = [(x0, y1, z_roof), (x1, y1, z_roof),
                       (x1, y1, z_par), (x0, y1, z_par)]
                if k == 'roof':         # face toward -y (the roof side)
                    pts.reverse()
            shell.quad(*pts, M_STUCCO)


def _peak_wall(shell, lines, p0, p1, xc, aw, z_par, z_peak, t, emit,
               stub_l=True, stub_r=True):
    """Peaked parapet rising FROM the wall (welded into the body shell, not
    a separate prism): front face continues the wall plane up from its top
    edge, back face continues the cap inner drop, a sloped cap band closes
    the top, short vertical end stubs weld to the cap-plane edges. The
    profile keeps a small apex flat and 0.12 m end stubs so every face is
    a planar quad and no corner degenerates to a triangle. `lines` must
    contain every grid line in [p0, p1] INCLUDING the apex-flat kinks (the
    wall-top verts weld into the segment bases)."""
    stub = z_par + 0.12

    def prof(u):
        a2 = xc - aw / 2.0
        b2 = xc + aw / 2.0
        if u <= a2 + 1e-9:
            return stub + (z_peak - stub) * (u - p0) / max(a2 - p0, 1e-6)
        if u >= b2 - 1e-9:
            return stub + (z_peak - stub) * (p1 - u) / max(p1 - b2, 1e-6)
        return z_peak

    segs = [v for v in lines if p0 - 1e-9 <= v <= p1 + 1e-9]
    q = shell.quad
    for si in range(len(segs) - 1):
        ua, ub = segs[si], segs[si + 1]
        za, zb = prof(ua), prof(ub)
        q(emit(ua, 0.0, z_par), emit(ub, 0.0, z_par),
          emit(ub, 0.0, zb), emit(ua, 0.0, za), M_STUCCO)      # front
        q(emit(ua, t, za), emit(ub, t, zb),
          emit(ub, t, z_par), emit(ua, t, z_par), M_STUCCO)    # back
        q(emit(ua, 0.0, za), emit(ub, 0.0, zb),
          emit(ub, t, zb), emit(ua, t, za), M_STUCCO)          # cap slope
    if stub_l:
        q(emit(p0, 0.0, z_par), emit(p0, 0.0, stub),
          emit(p0, t, stub), emit(p0, t, z_par), M_STUCCO)     # end stub L
    if stub_r:
        q(emit(p1, 0.0, stub), emit(p1, 0.0, z_par),
          emit(p1, t, z_par), emit(p1, t, stub), M_STUCCO)     # end stub R


def _store_sign(shell, rng, emit, u0, u1, z0, z1, smats, dead):
    """One LEGIBLE name sign per store: a primary landscape cabinet sized
    to the bay and centred (small jitter), plus occasionally a thin
    secondary strip below it or a projecting blade at the bay edge --
    structured, street-readable variance, not clutter."""
    def panel(ua, ub, za, zb, proud, m):
        pts = {}
        for pk, pd in (('f', proud + 0.055), ('b', proud)):
            pts[pk] = [emit(ua, pd, za), emit(ub, pd, za),
                       emit(ub, pd, zb), emit(ua, pd, zb)]
        f, b2 = pts['f'], pts['b']
        q = shell.quad
        q(f[0], f[1], f[2], f[3], m)
        q(b2[3], b2[2], b2[1], b2[0], m)
        q(b2[0], b2[1], f[1], f[0], m)
        q(b2[3], f[3], f[2], b2[2], m)
        q(b2[0], f[0], f[3], b2[3], m)
        q(f[1], b2[1], b2[2], f[2], m)

    span = u1 - u0
    band = z1 - z0
    m0 = smats[rng.randrange(3)]
    hw = span * (0.31 + rng.random() * 0.13)          # 62-88% of the bay
    cu = (u0 + u1) / 2.0 + (rng.random() - 0.5) * 0.3
    hh = band * (0.24 + rng.random() * 0.11)          # 48-70% of the band
    cz = z0 + band * (0.5 + (rng.random() - 0.5) * 0.16)
    panel(cu - hw, cu + hw, cz - hh, cz + hh, 0.002, m0)
    if rng.random() < 0.3:
        # thin secondary strip below ("SANDWICHES - BEER - LOTTO").
        panel(cu - hw * 0.85, cu + hw * 0.85, z0 + 0.03, cz - hh - 0.05,
              0.002, smats[rng.randrange(3)])
    if not dead and rng.random() < 0.3:
        bu = u0 + 0.03 if rng.random() < 0.5 else u1 - 0.13
        panel(bu, bu + 0.10, z0 - 0.85, z1 - 0.1, 0.06,
              smats[rng.randrange(3)])                # stub mount
        pts = {}
        for pk, pd in (('n', 0.10), ('f', 0.85)):
            pts[pk] = [emit(bu, pd, z0 - 0.8), emit(bu + 0.10, pd, z0 - 0.8),
                       emit(bu + 0.10, pd, z1 - 0.15), emit(bu, pd, z1 - 0.15)]
        nq, fq = pts['n'], pts['f']
        q = shell.quad
        q(nq[0], nq[3], nq[2], nq[1], m0)
        q(fq[0], fq[1], fq[2], fq[3], m0)
        q(nq[0], nq[1], fq[1], fq[0], m0)
        q(nq[3], fq[3], fq[2], nq[2], m0)
        q(nq[1], nq[2], fq[2], fq[1], m0)
        q(nq[0], fq[0], fq[3], nq[3], m0)


def _flight_straight(shell, x0, x1, y_base, y_top, z_base, z_top,
                     foot_lo, foot_hi):
    """Straight footed-stringer flight along y (the A1 stringer discipline:
    0.22 m channels, flat-clipped ends, riser/tread ribbon at a 6 mm
    channel reveal, recessed soffit panel)."""
    st, cover, depth = 0.09, 0.06, 0.22
    n = max(3, int(round(abs(z_top - z_base) / 0.185)))
    rise = (z_top - z_base) / n
    run = (y_top - y_base) / n
    rise_t = z_top - z_base
    t1 = max(0.0, min(0.45, (depth - cover - foot_lo) / rise_t))
    t2 = min(1.0, max(0.55, 1.0 - (depth - cover - foot_hi) / rise_t))
    flip = y_top < y_base

    def q(a, b, c, d, m):
        if flip:
            shell.quad(a, d, c, b, m)
        else:
            shell.quad(a, b, c, d, m)

    shell.tag = 'steps'
    for sx0, sx1 in ((x0, x0 + st), (x1 - st, x1)):
        ys4 = [y_base + (y_top - y_base) * tt for tt in (0.0, t1, t2, 1.0)]
        ts4 = [z_base + rise_t * tt + cover for tt in (0.0, t1, t2, 1.0)]
        bs4 = [z_base - foot_lo, z_base - foot_lo,
               z_top - foot_hi, z_top - foot_hi]
        for k4 in range(3):
            ya, yb = ys4[k4], ys4[k4 + 1]
            ta, tb = ts4[k4], ts4[k4 + 1]
            ba, bb = bs4[k4], bs4[k4 + 1]
            q((sx0, ya, ta), (sx1, ya, ta), (sx1, yb, tb), (sx0, yb, tb),
              M_METAL)
            q((sx0, ya, ba), (sx0, yb, bb), (sx1, yb, bb), (sx1, ya, ba),
              M_METAL)
            q((sx0, ya, ba), (sx0, ya, ta), (sx0, yb, tb), (sx0, yb, bb),
              M_METAL)
            q((sx1, ya, ta), (sx1, ya, ba), (sx1, yb, bb), (sx1, yb, tb),
              M_METAL)
        q((sx0, ys4[0], ts4[0]), (sx0, ys4[0], bs4[0]),
          (sx1, ys4[0], bs4[0]), (sx1, ys4[0], ts4[0]), M_METAL)
        q((sx0, ys4[3], bs4[3]), (sx0, ys4[3], ts4[3]),
          (sx1, ys4[3], ts4[3]), (sx1, ys4[3], bs4[3]), M_METAL)
    ix0, ix1 = x0 + st + 0.006, x1 - st - 0.006
    for k2 in range(n):
        ya, yb = y_base + run * k2, y_base + run * (k2 + 1)
        zlo, zhi = z_base + rise * k2, z_base + rise * (k2 + 1)
        q((ix0, ya, zlo), (ix1, ya, zlo), (ix1, ya, zhi), (ix0, ya, zhi),
          M_CONCRETE)
        q((ix0, ya, zhi), (ix1, ya, zhi), (ix1, yb, zhi), (ix0, yb, zhi),
          M_CONCRETE)
    ydir = 1.0 if y_top > y_base else -1.0
    ysf = [y_base + (y_top - y_base) * tt for tt in (0.0, t1, t2, 1.0)]
    ysf[0] += 0.006 * ydir
    ysf[3] -= 0.006 * ydir
    bsf = [z_base - foot_lo + 0.02, z_base - foot_lo + 0.02,
           z_top - foot_hi + 0.02, z_top - foot_hi + 0.02]
    for k4 in range(3):
        q((ix0, ysf[k4], bsf[k4]), (ix0, ysf[k4 + 1], bsf[k4 + 1]),
          (ix1, ysf[k4 + 1], bsf[k4 + 1]), (ix1, ysf[k4], bsf[k4]),
          M_CONCRETE)


def build_minimall(p, rng):
    """Build the mini-mall per the module topology plan. Returns objects."""
    n = p["tenants"]
    tw = p["tenant_width"]
    D = p["depth"]
    cd = p["canopy_depth"]
    Wm = n * tw
    corner = p["corner_lot"]
    office = p["office_strip"]
    dead = p["dead_mall"]
    interior_on = p["mode"] == 'interior'
    wt = 0.15
    t = 0.15                          # parapet ring width
    n_w = max(2, n // 2) if corner else 0
    Wy = n_w * tw                     # wing storefront run length (along -y)
    We = Wm + D if corner else Wm     # east extent

    z_roof = 6.5 if office else 4.15
    z_par = z_roof + 0.35
    z_ceil = z_roof - 0.12            # inner faces stop HERE -- and it must
    # be a GRID row (the dingbat lesson: a non-row inner_zmax lets cells
    # overshoot to the roof plane, 3 faces per edge).
    zl = [0.0, Z_BLK, Z_SRV, Z_DR, Z_SF, Z_FAS, z_ceil, z_roof, z_par]
    if office:
        zl += [Z_OSIL, Z_OHED]
    zl = sorted(set(zl))

    # ---- anchor tenant: double-width, taller, proud of the run -------------
    # (mutually exclusive with office_strip: the anchor mass would punch a
    # sky slot through the second storey).
    anchor_on = p["anchor"] and not office and n >= 4
    all_bays = _tenant_bays(Wm, n)
    ka = -1
    a0 = a1 = 0.0
    if anchor_on:
        ka = rng.choice([0, max(0, n // 2 - 1), n - 2])
        a0, a1 = all_bays[ka][0], all_bays[ka + 1][1]
    # ---- tenant fates: shuttered fraction + story bays ---------------------
    bays_m = [b3 for i3, b3 in enumerate(all_bays)
              if not (anchor_on and i3 in (ka, ka + 1))]
    bays_w = _tenant_bays(Wy, n_w) if corner else []
    shut_p = 1.0 if dead else p["shutters"]
    fates_m = ['shut' if rng.random() < shut_p else 'open' for _ in bays_m]
    fates_w = ['shut' if rng.random() < shut_p else 'open' for _ in bays_w]
    n_rem = len(bays_m)
    kf = kc = -1
    if p["one_fortified"]:
        kf = rng.randrange(n_rem)
        fates_m[kf] = 'void'
    if p["checkpoint_bay"]:
        kc = rng.randrange(n_rem)
        if kc == kf:
            kc = (kc + 1) % n_rem
        fates_m[kc] = 'void'

    # per-bay balcony treatments on the office storey (rule-2 variety):
    # 'run' flat deck, 'projecting' bump-out with rail wrap, 'recessed'
    # loggia (floor-2 wall recesses 1.3 m, deck floor continues in).
    bal_modes = {}
    recessed = set()
    if office:
        for i, (_b0, _b1, o0b, o1b) in enumerate(bays_m):
            sel = p["balconies"]
            if sel == 'mixed':
                sel = rng.choice(['run', 'projecting', 'recessed'])
            if sel == 'projecting' and corner and o1b > Wm - cd - 0.6:
                sel = 'recessed'      # a projection here would hang into
                # the wing deck (coincident geometry, physically wrong).
            bal_modes[i] = sel
            if sel == 'recessed':
                recessed.add(i)

    def door_span(o0, o1, left):
        # FLUSH against the pier: any gap strip narrower than the frame
        # inset becomes an inverted-ring sliver window (auditor-caught).
        return (o0, o0 + 0.95) if left else (o1 - 0.95, o1)

    doors_m = [door_span(o0, o1, rng.random() < 0.5)
               for (_b0, _b1, o0, o1) in bays_m]
    doors_w = [door_span(o0, o1, rng.random() < 0.5)
               for (_b0, _b1, o0, o1) in bays_w]

    # ---- peaked parapets: seeded per bay, spanning/centred on a bay --------
    aw_pk = 0.16
    peaks_m, peaks_w = [], []
    for (b0, b1, _o0, _o1) in bays_m:
        if rng.random() < p["peak_fraction"]:
            xc = (b0 + b1) / 2.0
            zpk = z_par + 0.55 + rng.random() * 0.5
            peaks_m.append((b0, b1, xc, zpk))
    for (b0, b1, _o0, _o1) in bays_w:
        if rng.random() < p["peak_fraction"]:
            xc = (b0 + b1) / 2.0
            zpk = z_par + 0.55 + rng.random() * 0.5
            peaks_w.append((b0, b1, xc, zpk))

    # ---- line families -----------------------------------------------------
    xl = {0.0, Wm, t, Wm - t, We, We - t}
    for (p0k, p1k, xck, _z) in peaks_m:
        xl |= {p0k, p1k, xck - aw_pk / 2.0, xck + aw_pk / 2.0}
    if corner:
        xl.add(Wm + t)                # wing-front cap band inset line
    if anchor_on:
        xl |= {a0, a1, a0 - t, a1 + t}   # run cut ends + their cap bands
    for i in recessed:
        (_b0, _b1, o0r, o1r) = bays_m[i]
        # end-bay clamp: o0-wt can COINCIDE with the side wall's inner
        # plane (verts on its edges); keep 50 mm clear.
        xl |= {max(o0r - wt, wt + 0.05), min(o1r + wt, Wm - wt - 0.05)}
    for (b0, b1, o0, o1) in bays_m:
        xl |= {b0, b1, o0, o1}
    for (d0, d1) in doors_m:
        xl |= {d0, d1}
    yl = {0.0, D, t, D - t}
    if corner:
        yl |= {-Wy, -Wy + t}
        for (p0k, p1k, xck, _z) in peaks_w:
            # wing peak u = -y: kink lines live on the y family.
            yl |= {-Wy + p0k, -Wy + p1k, -Wy + xck - aw_pk / 2.0,
                   -Wy + xck + aw_pk / 2.0}
        for (b0, b1, o0, o1) in bays_w:
            yl |= {b0 - Wy, b1 - Wy, o0 - Wy, o1 - Wy}
        for (d0, d1) in doors_w:
            yl |= {d0 - Wy, d1 - Wy}
    # rear service doors: one per tenant, centred in the bay.
    srv_m = [((b0 + b1) / 2.0 - 0.45, (b0 + b1) / 2.0 + 0.45)
             for (b0, b1, _o0, _o1) in bays_m]
    for pr in srv_m:
        xl |= set(pr)
    srv_w = [((b0 + b1) / 2.0 - Wy - 0.45, (b0 + b1) / 2.0 - Wy + 0.45)
             for (b0, b1, _o0, _o1) in bays_w]
    for pr in srv_w:
        yl |= set(pr)
    xl = sorted(v for v in xl if 0.0 - 1e-9 <= v <= We + 1e-9)
    yl = sorted(v for v in yl if (-Wy if corner else 0.0) - 1e-9 <= v
                <= D + 1e-9)

    def inside(x, y):
        if anchor_on and a0 < x < a1:
            return False              # the anchor mass owns this span
        if 0.0 < x < Wm and 0.0 < y < D:
            return True
        if corner and Wm < x < We and -Wy < y < D:
            return True
        # the main/wing seam column belongs to the footprint too.
        if corner and abs(x - Wm) < 1e-9 and -1e-9 < y < D:
            return True
        return False

    shell = _Shell()
    thick = wt if interior_on else 0.0
    iz_max = z_ceil

    # ---- storefront walls --------------------------------------------------
    shell.tag = 'storefront'
    cls_m = _storefront_classify(bays_m, doors_m, fates_m, Z_SF, office,
                                 void_span=(a0, a1) if anchor_on else None,
                                 recessed=recessed, wt=wt, run_w=Wm)
    _wf = _Wall(shell, (0, 0, 0), (1, 0, 0),
                [v for v in xl if v <= Wm + 1e-9], zl, (0, -1, 0),
                M_STUCCO, thickness=thick, inner_zmax=iz_max)
    _wf.inner_u0, _wf.inner_u1 = wt, Wm - (0.0 if corner else wt)
    _wf.fill(cls_m, frame=0.06, mat_frame=M_TRIM, mat_pane=M_GLASS)
    if corner:
        cls_w = _storefront_classify(bays_w, doors_w, fates_w, Z_SF, office)

        def cls_wing(u0, zc0):        # u = y + Wy along the wing run
            return cls_w(u0, zc0)

        _ww = _Wall(shell, (Wm, -Wy, 0), (0, 1, 0),
                    [v + Wy for v in yl if v <= 0.0 + 1e-9], zl, (-1, 0, 0),
                    M_STUCCO, thickness=thick, inner_zmax=iz_max)
        _ww.inner_u0, _ww.inner_u1 = wt, Wy - 0.0
        _ww.fill(cls_wing, frame=0.06, mat_frame=M_TRIM, mat_pane=M_GLASS)

    # ---- rear + side walls -------------------------------------------------
    shell.tag = 'facade_back'

    def rear_classify(u0, zc0):
        if anchor_on and a0 - 1e-6 <= u0 < a1 - 1e-6:
            return 'void'
        for (d0, d1) in srv_m:
            if d0 - 1e-6 <= u0 < d1 - 1e-6:
                if abs(zc0) < 1e-6:
                    return 'doorL'
                if zc0 < Z_SRV - 1e-6:
                    return 'doorU'
        return 'wall'

    _wr = _Wall(shell, (0, D, 0), (1, 0, 0), xl, zl, (0, 1, 0),
                M_STUCCO, thickness=thick, inner_zmax=iz_max)
    _wr.inner_u0, _wr.inner_u1 = wt, We - wt
    _wr.fill(rear_classify, frame=0.06, mat_frame=M_TRIM)

    def plain(u0, zc0):
        del u0, zc0
        return 'wall'

    shell.tag = 'facade_side'
    # west side x=0 (faces -x), y 0..D.
    _ws = _Wall(shell, (0, 0, 0), (0, 1, 0), [v for v in yl if v >= -1e-9],
                zl, (-1, 0, 0), M_STUCCO, thickness=thick, inner_zmax=iz_max)
    _ws.inner_u0, _ws.inner_u1 = wt, D - wt
    _ws.fill(plain)
    if corner:
        # wing south end y=-Wy (faces -y), x Wm..We.
        _wsx = _Wall(shell, (0, -Wy, 0), (1, 0, 0),
                     [v for v in xl if v >= Wm - 1e-9], zl, (0, -1, 0),
                     M_STUCCO, thickness=thick, inner_zmax=iz_max)
        _wsx.inner_u0, _wsx.inner_u1 = Wm + wt, We - wt
        _wsx.fill(plain)
        # east x=We (faces +x), y -Wy..D, with wing service doors.

        def rear_classify_w(u0, zc0):
            for (d0, d1) in srv_w:
                if d0 - 1e-6 <= u0 < d1 - 1e-6:
                    if abs(zc0) < 1e-6:
                        return 'doorL'
                    if zc0 < Z_SRV - 1e-6:
                        return 'doorU'
            return 'wall'

        _we = _Wall(shell, (We, 0, 0), (0, 1, 0), yl, zl, (1, 0, 0),
                    M_STUCCO, thickness=thick, inner_zmax=iz_max)
        _we.inner_u0, _we.inner_u1 = -Wy + wt, D - wt
        _we.fill(rear_classify_w, frame=0.06, mat_frame=M_TRIM)
    else:
        _we = _Wall(shell, (Wm, 0, 0), (0, 1, 0), yl, zl, (1, 0, 0),
                    M_STUCCO, thickness=thick, inner_zmax=iz_max)
        _we.inner_u0, _we.inner_u1 = wt, D - wt
        _we.fill(plain)

    # ---- recessed-loggia bays: cheeks + recessed wall + ceiling ------------
    # (the A1 loggia discipline: recess stops at the HEAD line, cheeks and
    # ceiling split at the inner-skin line, thick mode adds inner cheek
    # skins + row-split corner fillers, recessed wall inner face clips.)
    for i in sorted(recessed):
        (_b0r, _b1r, o0r, o1r) = bays_m[i]
        ld2 = 1.3
        l_rows = [v for v in zl if Z_FAS - 1e-6 <= v <= Z_OHED + 1e-6]
        cheek_y = [0.0, wt, ld2]
        shell.tag = 'loggia'
        for (cx2, sgn) in ((o0r, 1.0), (o1r, -1.0)):
            for ri in range(len(l_rows) - 1):
                r0, r1 = l_rows[ri], l_rows[ri + 1]
                for yi in range(len(cheek_y) - 1):
                    ya5, yb5 = cheek_y[yi], cheek_y[yi + 1]
                    pts = [(cx2, ya5, r0), (cx2, yb5, r0), (cx2, yb5, r1),
                           (cx2, ya5, r1)]
                    if sgn < 0:
                        pts.reverse()
                    shell.quad(*pts, M_STUCCO)
                if interior_on:
                    inx = (max(o0r - wt, wt + 0.05) if sgn > 0
                           else min(o1r + wt, Wm - wt - 0.05))
                    pts = [(inx, wt, r0), (inx, ld2 + wt, r0),
                           (inx, ld2 + wt, r1), (inx, wt, r1)]
                    if sgn > 0:
                        pts.reverse()
                    shell.quad(*pts, M_STUCCO)
        d0r, d1r = doors_m[i]

        def _lg_cls(u0, zc0, _d0=d0r, _d1=d1r):
            if _d0 - 1e-6 <= u0 < _d1 - 1e-6:
                if abs(zc0 - Z_FAS) < 1e-6:
                    return 'doorL'
                return 'doorU'
            if Z_OSIL - 1e-6 <= zc0:
                return 'window'
            return 'wall'

        lxl2 = [v for v in xl if o0r - 1e-6 <= v <= o1r + 1e-6]
        _wl2 = _Wall(shell, (0, ld2, 0), (1, 0, 0), lxl2, l_rows,
                     (0, -1, 0), M_STUCCO, thickness=thick,
                     inner_zmax=iz_max)
        _wl2.fill(_lg_cls, frame=0.06, mat_frame=M_TRIM, mat_pane=M_GLASS)
        if interior_on:
            for (fx0, fx1) in ((max(o0r - wt, wt + 0.05), o0r),
                               (o1r, min(o1r + wt, Wm - wt - 0.05))):
                for ri in range(len(l_rows) - 1):
                    r0, r1 = l_rows[ri], l_rows[ri + 1]
                    shell.quad((fx0, ld2 + wt, r0), (fx1, ld2 + wt, r0),
                               (fx1, ld2 + wt, r1), (fx0, ld2 + wt, r1),
                               M_STUCCO)
        # interior mode: the ceiling starts AT the inner-skin line (the
        # [0..wt] strip would put 3 faces on the inner wall's bottom edge
        # -- ceiling cell + ceiling cell + resuming inner face).
        ceil_y = cheek_y if not interior_on else [wt, ld2]
        for iu2 in range(len(lxl2) - 1):
            x0c, x1c = lxl2[iu2], lxl2[iu2 + 1]
            for yi in range(len(ceil_y) - 1):
                ya6, yb6 = ceil_y[yi], ceil_y[yi + 1]
                shell.quad((x0c, ya6, Z_OHED), (x0c, yb6, Z_OHED),
                           (x1c, yb6, Z_OHED), (x1c, ya6, Z_OHED),
                           M_STUCCO)

    # ---- roof (cell-classified cap ring / plane / drops) -------------------
    def in_peak(cx, cy):
        for (p0k, p1k, _xc, _z) in peaks_m:
            if p0k < cx < p1k and 0.0 < cy < t:
                return True
        for (p0k, p1k, _xc, _z) in peaks_w:
            if Wm < cx < Wm + t and -Wy + p0k < cy < -Wy + p1k:
                return True
        return False

    _roof_cells(shell, xl, yl, inside, z_roof, z_par, t, in_peak=in_peak)
    # welded peaked parapets over their bays (front face continues the
    # wall plane; back face continues the cap inner drop).
    shell.tag = 'parapet'
    starts_m = {round(p0k, 6) for (p0k, _p1, _xc, _z) in peaks_m}
    ends_m = {round(p1k, 6) for (_p0, p1k, _xc, _z) in peaks_m}
    for (p0k, p1k, xck, zpk) in peaks_m:
        # adjacent peaks MERGE at their shared bay line: both stubs there
        # would be coincident opposite-winding quads (doubled faces).
        _peak_wall(shell, xl, p0k, p1k, xck, aw_pk, z_par, zpk, t,
                   lambda u, d2, z: (u, d2, z),
                   stub_l=round(p0k, 6) not in ends_m,
                   stub_r=round(p1k, 6) not in starts_m)
    starts_w = {round(Wy - p1k, 6) for (_p0, p1k, _xc, _z) in peaks_w}
    ends_w = {round(Wy - p0k, 6) for (p0k, _p1, _xc, _z) in peaks_w}
    for (p0k, p1k, xck, zpk) in peaks_w:
        # u runs along -y (handedness for the -x-facing wing wall).
        u0k, u1k = Wy - p1k, Wy - p0k
        _peak_wall(shell, sorted(-v for v in yl), u0k, u1k,
                   Wy - xck, aw_pk, z_par, zpk, t,
                   lambda u, d2, z: (Wm + d2, -u - 0.0, z),
                   stub_l=round(u0k, 6) not in ends_w,
                   stub_r=round(u1k, 6) not in starts_w)

    mats = [_material(nm) for nm in _MATS]
    body = shell.to_object("LA_MiniMall_Body", mats)

    # ---- canopy / balcony deck + supports ----------------------------------
    can = _Shell()
    can.tag = 'canopy'
    deck_top = Z_FAS if office else 3.35
    deck_lo = deck_top - 0.5 if office else Z_SF
    x_deck0 = -1.45 if office else 0.0
    deck_segs = ([(x_deck0, a0 - 0.002), (a1 + 0.002, Wm)]
                 if anchor_on else [(x_deck0, Wm)])
    for (sx0, sx1) in deck_segs:
        _box(can, (sx0, -cd, deck_lo), (sx1, -0.002, deck_top), M_CONCRETE)
    if corner:
        _box(can, (Wm - cd, -Wy, deck_lo), (Wm - 0.002, -cd - 0.002,
             deck_top), M_CONCRETE)
    # (bal_modes precomputed before the walls -- the recessed bays void
    # their floor-2 cells in the storefront classifier.)
    if office:
        for i, (_b0, _b1, o0, o1) in enumerate(bays_m):
            sel = bal_modes[i]
            if sel == 'projecting':
                can.tag = 'canopy'
                p0, p1 = o0 + 0.35, o1 - 0.35
                _box(can, (p0, -cd - 0.95, deck_lo + 0.10),
                     (p1, -cd - 0.002, deck_top), M_CONCRETE)
                # rail FRAME: side pieces + front piece with 4 mm corner
                # clearances (overlapping solids read as tj/nm).
                can.tag = 'loggia'
                _wall_solid(can, 'y', p0 + 0.02, -cd - 0.93, -cd - 0.06,
                            deck_top + 0.002, deck_top + 0.92, 0.12, None,
                            M_STUCCO)
                _wall_solid(can, 'y', p1 - 0.14, -cd - 0.93, -cd - 0.06,
                            deck_top + 0.002, deck_top + 0.92, 0.12, None,
                            M_STUCCO)
                _wall_solid(can, 'x', -cd - 0.95, p0 + 0.144, p1 - 0.144,
                            deck_top + 0.002, deck_top + 0.92, 0.12, None,
                            M_STUCCO)
            elif sel == 'recessed' and not interior_on:
                # loggia floor: the deck continues INTO the recess (in
                # interior mode the office floor slab already IS it).
                can.tag = 'canopy'
                _box(can, (o0 + 0.002, 0.002, Z_FAS - 0.28),
                     (o1 - 0.002, 1.298, Z_FAS), M_CONCRETE)
        # solid stucco balcony rail on the deck edge (split at anchor +
        # projecting bays keep their own rail wraps; the run rail passes
        # in front of projecting decks so leave gaps there instead).
        can.tag = 'loggia'
        rail_segs = []
        cursor = x_deck0 + 0.02
        for i, (_b0, _b1, o0, o1) in enumerate(bays_m):
            if bal_modes.get(i) == 'projecting':
                rail_segs.append((cursor, o0 + 0.33))
                cursor = o1 - 0.33
        rail_segs.append((cursor, Wm - 0.02))
        for (r0, r1) in rail_segs:
            if r1 - r0 > 0.1:
                _wall_solid(can, 'x', -cd + 0.02, r0, r1,
                            deck_top + 0.002, deck_top + 0.92, 0.12, None,
                            M_STUCCO)
        if corner:
            _wall_solid(can, 'y', Wm - cd + 0.02, -Wy + 0.02, -cd - 0.06,
                        deck_top + 0.002, deck_top + 0.92, 0.12, None,
                        M_STUCCO)
    if p["arcade"]:
        can.tag = 'canopy'
        _box(can, (0.002, -cd, deck_lo - 0.45), (Wm - 0.002, -cd + 0.12,
             deck_lo + 0.02), M_STUCCO)   # fascia embeds 20 mm into the
        # slab; 2 mm end insets keep its edges off the slab's end planes
        can.tag = 'columns'
        for (b0, _b1, _o0, _o1) in bays_m[1:] + [(Wm, 0, 0, 0)] + [(0,) * 4]:
            x = min(max(b0, 0.2), Wm - 0.2)
            if anchor_on and a0 - 0.3 < x < a1 + 0.3:
                continue
            _box(can, (x - 0.18, -cd + 0.10, 0.0),
                 (x + 0.18, -cd + 0.46, deck_lo + 0.02), M_STUCCO)
    else:
        can.tag = 'columns'
        posts_x = [b0 for (b0, _b1, _o0, _o1) in bays_m[1:]] + [0.25,
                                                                Wm - 0.25]
        for x in posts_x:
            if anchor_on and a0 - 0.3 < x < a1 + 0.3:
                continue
            _box(can, (x - 0.05, -cd + 0.12, 0.0),
                 (x + 0.05, -cd + 0.22, deck_lo + 0.02), M_METAL)
        if corner:
            posts_y = [b0 - Wy for (b0, _b1, _o0, _o1) in bays_w[1:]] + \
                      [-Wy + 0.25, -cd - 0.35]
            for y in posts_y:
                _box(can, (Wm - cd + 0.12, y - 0.05, 0.0),
                     (Wm - cd + 0.22, y + 0.05, deck_lo + 0.02), M_METAL)
    canopy_ob = can.to_object("LA_MiniMall_Canopy", mats)

    # ---- office-strip access stair (straight, footed stringers) ------------
    stair_ob = None
    if office:
        stair = _Shell()
        rise = Z_FAS
        run = 0.26 * max(3, int(round(rise / 0.185)))
        _flight_straight(stair, -1.35, -0.15, -cd - run, -cd, 0.0, rise,
                         0.0, 0.16)
        stair_ob = stair.to_object("LA_MiniMall_Stair", mats)

    # ---- walkway + parking lot ---------------------------------------------
    lot = _Shell()
    lot.tag = 'walkway'
    walk_segs = ([( (x_deck0 if office else 0.0), a0 - 0.002),
                  (a1 + 0.002, Wm)] if anchor_on
                 else [((x_deck0 if office else 0.0), Wm)])
    for (wx0s, wx1s) in walk_segs:
        _box(lot, (wx0s, -cd - 0.45, 0.0), (wx1s, -0.002, 0.14), M_CONCRETE)
    if anchor_on:
        _box(lot, (a0 + 0.002, -cd - 0.45, 0.0),
             (a1 - 0.002, -1.602, 0.14), M_CONCRETE)   # anchor entry apron
    if corner:
        _box(lot, (Wm - cd - 0.45, -Wy, 0.0),
             (Wm - 0.002, -cd - 0.452, 0.14), M_CONCRETE)
    rows = p["parking_rows"]
    y_lot0 = -cd - 0.452
    y_lot1 = y_lot0
    if rows > 0:
        lot.tag = 'lot'
        y_lot1 = y_lot0 - rows * 5.0 - 6.5
        lot_x1 = (Wm - cd - 0.452) if corner else Wm
        _box(lot, (0.0, y_lot1, 0.0), (lot_x1, y_lot0 - 0.002, 0.05),
             M_CONCRETE)
        heads = [y_lot0 - 0.002]
        if rows == 2:
            heads.append(y_lot1 + 11.5)
        for hy in heads:
            nstall = int((lot_x1 - 1.0) / 2.7)
            for si in range(nstall + 1):
                sx = 0.5 + si * 2.7
                _box(lot, (sx - 0.05, hy - 5.0, 0.051),
                     (sx + 0.05, hy - 0.3, 0.055), M_TRIM)
            lot.tag = 'lot'
            for si in range(nstall):
                sx = 0.5 + si * 2.7 + 1.35
                _box(lot, (sx - 0.85, hy - 0.75, 0.051),
                     (sx + 0.85, hy - 0.60, 0.16), M_CONCRETE)  # wheel stop
    lot_ob = lot.to_object("LA_MiniMall_Lot", mats)

    # ---- ANCHOR tenant: its own taller, prouder mass -----------------------
    anchor_ob = None
    AF = -1.6
    zA_sf, zA_fas = 3.8, 4.9
    zA_ceil, zA_roof, zA_par = 5.48, 5.6, 5.95
    if anchor_on:
        ash = _Shell()
        ax0, ax1 = a0 + 0.002, a1 - 0.002
        axc = (ax0 + ax1) / 2.0
        aop0, aop1 = ax0 + 0.45, ax1 - 0.45
        ad0, ad1 = axc - 0.9, axc + 0.9         # double door
        asv = ((ax0 + ax1) / 2.0 - 0.45, (ax0 + ax1) / 2.0 + 0.45)
        zla = sorted({0.0, Z_BLK, Z_SRV, 3.0, zA_sf, zA_fas, zA_ceil,
                      zA_roof, zA_par})
        apk = ((a0 + a1) / 2.0 - 2.4, (a0 + a1) / 2.0 + 2.4)
        axl = sorted({ax0, ax1, ax0 + t, ax1 - t, aop0, aop1, ad0, ad1,
                      asv[0], asv[1], apk[0], apk[1],
                      axc - 0.09, axc + 0.09})
        ayl = sorted({AF, AF + t, 0.0, D, D - t})

        def a_inside(x, y):
            return ax0 < x < ax1 and AF < y < D

        def a_front(u0, zc0):
            if aop0 - 1e-6 <= u0 < aop1 - 1e-6 and zc0 < zA_sf - 1e-6:
                if dead:
                    return 'window_shutter'
                if ad0 - 1e-6 <= u0 < ad1 - 1e-6:
                    if abs(zc0) < 1e-6:
                        return 'doorL'
                    if zc0 < 3.0 - 1e-6:
                        return 'doorU'
                    return 'window_transom'
                if zc0 < Z_BLK - 1e-6:
                    return 'wall'
                return 'window'
            return 'wall'

        def a_rear(u0, zc0):
            if asv[0] - 1e-6 <= u0 < asv[1] - 1e-6:
                if abs(zc0) < 1e-6:
                    return 'doorL'
                if zc0 < Z_SRV - 1e-6:
                    return 'doorU'
            return 'wall'

        def a_plain(u0, zc0):
            del u0, zc0
            return 'wall'

        ash.tag = 'storefront'
        _wa = _Wall(ash, (0, AF, 0), (1, 0, 0), axl, zla, (0, -1, 0),
                    M_STUCCO, thickness=thick, inner_zmax=zA_ceil)
        _wa.inner_u0, _wa.inner_u1 = ax0 + wt, ax1 - wt
        _wa.fill(a_front, frame=0.06, mat_frame=M_TRIM, mat_pane=M_GLASS)
        ash.tag = 'facade_back'
        _wb = _Wall(ash, (0, D, 0), (1, 0, 0), axl, zla, (0, 1, 0),
                    M_STUCCO, thickness=thick, inner_zmax=zA_ceil)
        _wb.inner_u0, _wb.inner_u1 = ax0 + wt, ax1 - wt
        _wb.fill(a_rear, frame=0.06, mat_frame=M_TRIM)
        ash.tag = 'facade_side'
        for (sx4, nrm4) in ((ax0, (-1, 0, 0)), (ax1, (1, 0, 0))):
            _wsa = _Wall(ash, (sx4, 0, 0), (0, 1, 0),
                         [AF] + [v for v in ayl if v > AF], zla, nrm4,
                         M_STUCCO, thickness=thick, inner_zmax=zA_ceil)
            _wsa.inner_u0, _wsa.inner_u1 = AF + wt, D - wt
            _wsa.fill(a_plain)
        def a_in_peak(cx, cy):
            return apk[0] < cx < apk[1] and AF < cy < AF + t

        _roof_cells(ash, axl, ayl, a_inside, zA_roof, zA_par, t,
                    in_peak=a_in_peak)
        ash.tag = 'parapet'
        _peak_wall(ash, axl, apk[0], apk[1], axc, 0.18, zA_par,
                   zA_par + 1.15, t, lambda u, d2, z: (u, AF + d2, z))
        if interior_on:
            e = 0.001
            ash.tag = 'slabs'
            _box(ash, (ax0 + wt + e, AF + wt + e, 0.0),
                 (ax1 - wt - e, D - wt - e, 0.12), M_CONCRETE)
            _box(ash, (ax0 + wt + e, AF + wt + e, zA_roof - 0.12),
                 (ax1 - wt - e, D - wt - e, zA_roof - 0.002), M_CONCRETE)
        anchor_ob = ash.to_object("LA_MiniMall_Anchor", mats)

    # ---- sign clusters + pediment peaks + roof / pole signs ----------------
    # (recalc shell: every element is a closed solid, so windings are
    # normalised regardless of the per-wall emit maps.)
    sign = _Shell(recalc=True)
    sign.tag = 'signage'
    smats = [M_SIGN_A, M_SIGN_B, M_SIGN_C]

    def emit_front(u, pd, z):         # main storefront face (y = 0)
        return (u, -0.002 - pd, z)

    def emit_wing(u, pd, z):          # wing face (x = Wm), u runs -y
        return (Wm - 0.002 - pd, -u, z)

    if p["sign_band"] and not dead:
        for i, (_b0, _b1, o0, o1) in enumerate(bays_m):
            _store_sign(sign, rng, emit_front, o0, o1, Z_SF + 0.05,
                        Z_FAS - 0.05, smats, dead)
        for i, (_b0, _b1, o0, o1) in enumerate(bays_w):
            _store_sign(sign, rng, emit_wing, Wy - o1, Wy - o0,
                        Z_SF + 0.05, Z_FAS - 0.05, smats, dead)
    if anchor_on and not dead:
        _store_sign(sign, rng,
                    lambda u, pd, z: (u, AF - 0.002 - pd, z),
                    a0 + 0.6, a1 - 0.6, zA_sf + 0.06, zA_fas - 0.06,
                    smats, dead)
    if p["roof_sign"]:
        rsx = bays_m[min(n - 1, max(0, n // 2))][0] + tw / 2.0
        for px in (rsx - 1.6, rsx + 1.5):
            _box(sign, (px, 1.0, z_roof + 0.002),
                 (px + 0.10, 1.10, z_roof + 2.15), M_METAL)
        _box(sign, (rsx - 1.65, 1.02, z_roof + 0.9),
             (rsx + 1.65, 1.08, z_roof + 2.05),
             M_METAL if dead else M_SIGN_B)
    if p["pole_sign"]:
        # distinct pole-sign STYLES, seeded: 'cabinet' one big box;
        # 'directory' primary cabinet + aligned tenant strips; 'reader'
        # cabinet + tall reader board; 'twin' two poles, one wide panel.
        sign.tag = 'pole_sign'
        ph = p["pole_height"]
        px, py = 1.6, min(-cd - 1.5, y_lot1 + 1.2)
        style = rng.choice(['cabinet', 'directory', 'reader', 'twin'])
        pm = (lambda k: M_METAL) if dead else (lambda k: smats[k % 3])
        if style == 'twin':
            for tx in (px - 1.1, px + 1.0):
                _box(sign, (tx, py - 0.12, 0.0), (tx + 0.24, py + 0.12, ph),
                     M_METAL)
            _box(sign, (px - 1.55, py - 0.10, ph - 1.5),
                 (px + 1.55, py + 0.10, ph - 0.25), pm(0))
            _box(sign, (px - 1.1, py - 0.08, ph - 2.35),
                 (px + 1.1, py + 0.08, ph - 1.65), pm(1))
        else:
            _box(sign, (px - 0.15, py - 0.15, 0.0),
                 (px + 0.15, py + 0.15, ph), M_METAL)
            if style == 'cabinet':
                _box(sign, (px - 1.5, py - 0.16, ph - 2.1),
                     (px + 1.5, py + 0.16, ph - 0.2), pm(0))
            elif style == 'directory':
                _box(sign, (px - 1.35, py - 0.14, ph - 1.35),
                     (px + 1.35, py + 0.14, ph - 0.2), pm(0))
                zc = ph - 1.55
                for k in range(p["pole_panels"]):
                    _box(sign, (px - 1.0, py - 0.08, zc - 0.42),
                         (px + 1.0, py + 0.08, zc - 0.06), pm(k + 1))
                    zc -= 0.48
                    if zc < ph * 0.3:
                        break
            else:                     # reader
                _box(sign, (px - 1.4, py - 0.16, ph - 1.6),
                     (px + 1.4, py + 0.16, ph - 0.2), pm(0))
                _box(sign, (px - 1.0, py - 0.10, ph - 3.4),
                     (px + 1.0, py + 0.10, ph - 1.8), M_TRIM)
    sign_ob = sign.to_object("LA_MiniMall_Signs", mats)

    # ---- story dressing ----------------------------------------------------
    story = _Shell()
    story.tag = 'story'
    if kf >= 0:
        (_b0, _b1, o0, o1) = bays_m[kf]
        _box(story, (o0 - 0.05, -0.045, 0.0), (o1 + 0.05, -0.005, 1.45),
             M_METAL)
        _box(story, (o0 - 0.05, -0.045, 1.55), (o1 + 0.05, -0.005, Z_SF),
             M_METAL)                  # plate steel + firing slit
    if kc >= 0:
        (_b0, _b1, o0, o1) = bays_m[kc]
        cx = (o0 + o1) / 2.0
        _box(story, (o0 + 0.2, 0.3, 0.0), (o0 + 1.5, 1.5, 2.3), M_CONCRETE)
        _box(story, (o0 + 0.3, 0.35, 2.3), (o0 + 1.4, 1.45, 2.5), M_METAL)
        for gx in (o0 - 0.1, o1 - 0.1):
            # posts embed 20 mm into the beam (coplanar stack = nm/tj).
            _box(story, (gx, -cd + 0.5, 0.0), (gx + 0.12, -cd + 0.62, 2.87),
                 M_METAL)
        _box(story, (o0 - 0.102, -cd + 0.5, 2.85), (o1 + 0.022, -cd + 0.62,
             3.0), M_METAL)            # scan gantry beam
        _box(story, (cx - 0.06, -cd + 0.56, 0.95),
             (cx + 1.6, -cd + 0.62, 1.05), M_TRIM)   # barrier arm
    story_ob = story.to_object("LA_MiniMall_Story", mats)

    # ---- interior mode: slabs, demising walls, back corridor, hatch --------
    interior_obs = []
    if interior_on:
        e = 0.001
        slabs = _Shell()
        slabs.tag = 'slabs'
        _box(slabs, (wt + e, wt + e - (Wy if corner else 0.0) * 0.0, 0.0),
             (Wm - (0.0 if corner else wt) - e, D - wt - e, 0.12),
             M_CONCRETE)
        if corner:
            _box(slabs, (Wm + e, -Wy + wt + e, 0.0),
                 (We - wt - e, D - wt - e, 0.12), M_CONCRETE)
        _box(slabs, (wt + e, wt + e, z_roof - 0.12),
             (Wm - (0.0 if corner else wt) - e, D - wt - e, z_roof - 0.002),
             M_CONCRETE)
        if corner:
            _box(slabs, (Wm + e, -Wy + wt + e, z_roof - 0.12),
                 (We - wt - e, D - wt - e, z_roof - 0.002), M_CONCRETE)
        if office:
            _box(slabs, (wt + e, wt + e, Z_FAS - 0.30),
                 (Wm - (0.0 if corner else wt) - e, D - wt - e, Z_FAS),
                 M_CONCRETE)
        interior_obs.append(slabs.to_object("LA_MiniMall_Slabs", mats))

        walls = _Shell(recalc=True)
        walls.tag = 'demising'
        cor_y = D - 1.6
        zt2 = z_roof - 0.122
        zt_gnd = (Z_FAS - 0.302) if office else zt2   # stop at the office
        # floor slab -- running through it coplanar-overlapped the office
        # demising above.
        for (b0, _b1, _o0, _o1) in bays_m[1:]:
            _wall_solid(walls, 'y', b0 - 0.05, wt + 0.002, cor_y - 0.052,
                        0.12, zt_gnd, 0.10, None, M_STUCCO)
            if office:
                _wall_solid(walls, 'y', b0 - 0.05, wt + 0.002, D - wt - 0.002,
                            Z_FAS + 0.002, zt2, 0.10, None, M_STUCCO)
        if corner:
            _wall_solid(walls, 'y', Wm + 0.05, wt + 0.002, D - wt - 0.002,
                        0.12, zt_gnd, 0.10, None, M_STUCCO)
            for (b0, _b1, _o0, _o1) in bays_w[1:]:
                _wall_solid(walls, 'x', b0 - Wy - 0.05, Wm + 0.152,
                            We - wt - 0.002, 0.12, zt_gnd, 0.10, None,
                            M_STUCCO)
        # back corridor wall, one doored segment per tenant.
        walls.tag = 'corridor'
        for i, (b0, b1, _o0, _o1) in enumerate(bays_m):
            a2 = b0 + (0.052 if i else wt + 0.002)
            b2 = b1 - (0.052 if i < n - 1 else wt + 0.002)
            dxc = (b0 + b1) / 2.0
            _wall_solid(walls, 'x', cor_y, a2, b2, 0.12, zt_gnd, 0.10,
                        (dxc - 0.45, dxc + 0.45, 0.12 + 2.05), M_STUCCO)
        interior_obs.append(walls.to_object("LA_MiniMall_Interior", mats))

        hatch = _Shell()
        hatch.tag = 'roof'
        hx = Wm - 1.6
        _box(hatch, (hx, D - 1.45, z_roof + 0.002),
             (hx + 0.8, D - 0.65, z_roof + 0.27), M_CONCRETE)
        _box(hatch, (hx - 0.04, D - 1.49, z_roof + 0.272),
             (hx + 0.84, D - 0.61, z_roof + 0.33), M_METAL)
        interior_obs.append(hatch.to_object("LA_MiniMall_Hatch", mats))

    out = [body, anchor_ob, canopy_ob, stair_ob, lot_ob, sign_ob,
           story_ob] + interior_obs
    out = [ob for ob in out if ob is not None]
    for ob in out:
        ob["ferrum_lightmap_res"] = 0 if ob in (sign_ob, story_ob) else 128
    return out


SPEC = [
    params.MODE_PARAM,
    dict(name="tenants", type='INT', default=5, min=2, max=9),
    dict(name="tenant_width", type='FLOAT', default=5.5, min=4.0, max=8.0,
         unit='LENGTH', desc="Bay width per tenant"),
    dict(name="depth", type='FLOAT', default=12.0, min=8.0, max=16.0,
         unit='LENGTH'),
    dict(name="canopy_depth", type='FLOAT', default=2.5, min=1.5, max=3.5,
         unit='LENGTH'),
    dict(name="sign_band", type='BOOL', default=True,
         desc="Per-tenant mismatched sign panels on the fascia"),
    dict(name="parking_rows", type='INT', default=1, min=0, max=2,
         desc="Striped lot rows + wheel stops"),
    dict(name="pole_sign", type='BOOL', default=True),
    dict(name="pole_height", type='FLOAT', default=9.0, min=5.0, max=15.0,
         unit='LENGTH'),
    dict(name="pole_panels", type='INT', default=4, min=1, max=8),
    dict(name="shutters", type='FLOAT', default=0.3, min=0.0, max=1.0,
         desc="Fraction of roll-up shutters down"),
    dict(name="peak_fraction", type='FLOAT', default=0.25, min=0.0, max=1.0,
         desc="Fraction of bays with a peaked parapet pediment"),
    dict(name="balconies", type='ENUM', default='run',
         items=('run', 'projecting', 'recessed', 'mixed'),
         desc="Office-storey balcony treatment per bay"),
    # -- monotony breakers --
    dict(name="anchor", type='BOOL', default=True,
         desc="One double-width anchor tenant: taller, proud of the run, "
              "prominent roof + signage (auto-off with office_strip)"),
    dict(name="corner_lot", type='BOOL', default=False,
         desc="L-plan: a second storefront wing on the east, lot in the "
              "crook"),
    dict(name="arcade", type='BOOL', default=False,
         desc="Stucco arcade piers + hanging fascia instead of posts"),
    dict(name="roof_sign", type='BOOL', default=False,
         desc="Rooftop frame sign instead of / beside the pole sign"),
    dict(name="office_strip", type='BOOL', default=False,
         desc="Full second storey: balcony deck over the canopy, stair "
              "from the lot"),
    # -- story options (off by default) --
    dict(name="one_fortified", type='BOOL', default=False,
         desc="One tenant plate-steeled as a resistance safehouse"),
    dict(name="checkpoint_bay", type='BOOL', default=False,
         desc="One bay converted to a regime scan-lane"),
    dict(name="dead_mall", type='BOOL', default=False,
         desc="Every shutter down, signage stripped"),
]

params.register_tool(idname="la_minimall", label="Mini-Mall",
                     family="Commercial", build=build_minimall, spec=SPEC)


def smoke():
    """Headless validation: build variants, audit every object."""
    import random
    out = {}
    for label, over in [("facade", dict()),
                        ("interior", dict(mode="interior")),
                        ("corner_office", dict(mode="interior",
                                               corner_lot=True,
                                               office_strip=True))]:
        pr = params.defaults("la_minimall")
        pr.update(over)
        objs = build_minimall(pr, random.Random(1))
        for ob in objs:
            rep = topology.validate_object(ob)
            out[label + "/" + ob.name] = (topology.ok(rep),
                                          topology.summarize(rep))
            bpy.data.objects.remove(ob)
    return out
