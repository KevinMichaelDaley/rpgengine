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
from . import doors as doorkit
from . import elements as el2
from .storefront import emit_rollup, emit_security_bars, emit_storefront_bay
from ..geom import (
    _MATS, _Shell, _Wall, _box, _material, _sheared_box, _wall_solid,
    M_CONCRETE, M_GLASS, M_METAL, M_SHUTTER, M_SIGN_A, M_SIGN_B, M_SIGN_C,
    M_STUCCO, M_TRIM,
)

# fixed z-levels (metres) -- see the topology plan above.
Z_BLK, Z_SRV, Z_DR, Z_SF, Z_FAS = 0.25, 2.1, 2.6, 3.0, 3.9
Z_DHEAD2 = 3.55                      # loading-dock head
# office storey rows (only in the zl when office_strip).
Z_ODR, Z_OSIL, Z_OHED = 6.0, 4.8, 6.0


def plain_wall(u0, zc0):
    del u0, zc0
    return 'wall'


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
                         run_w=1e9, high_bk=None, barred_wins=None,
                         shop2=frozenset()):
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
                hi_bk = bool(high_bk and high_bk[i])
                if fate == 'void':
                    return 'void'
                if fate == 'shut':
                    # roll-up: full drop to grade, or stopped on a 0.9 m
                    # masonry knee wall (the reference look).
                    if hi_bk and zc0 < 0.9 - 1e-6:
                        return 'wall'
                    return 'window_shutter'
                d0, d1 = doors[i]
                if d0 - 1e-6 <= u0 < d1 - 1e-6:
                    if abs(zc0) < 1e-6:
                        return 'doorL'
                    if zc0 < Z_SRV - 1e-6:   # 2.1 m door + tall transom
                        return 'doorU'
                    return 'window_transom'
                if fate == 'barred':
                    # narrow high-sill windows instead of plate glass;
                    # grilles go over every opening (sign shell).
                    for (wj0, wj1) in (barred_wins or {}).get(i, []):
                        if wj0 - 1e-6 <= u0 < wj1 - 1e-6 and \
                                0.9 - 1e-6 <= zc0 < Z_SRV - 1e-6:
                            return 'window'
                    return 'wall'
                if zc0 < (0.9 if hi_bk else Z_BLK) - 1e-6:
                    return 'wall'          # bulkhead under the glass
                return 'window'
            if office and Z_FAS - 1e-6 <= zc0 < Z_OHED - 1e-6:
                if i in shop2:
                    # two-story shop: ribbon window only (no balcony door --
                    # the deck is broken in front of this bay).
                    if Z_OSIL - 1e-6 <= zc0:
                        return 'window'
                    return 'wall'
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


def _store_sign(shell, rng, emit, u0, u1, z0, z1, smats, dead,
                blade_ok=True):
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
    if not dead and blade_ok and rng.random() < 0.35:
        # projecting BLADE(s): seeded size; occasionally a vertical stack
        # of shorter cabinets on one mount. Suppressed when a canopy or
        # balcony deck would block them (blade_ok).
        bu = u0 + 0.03 if rng.random() < 0.5 else u1 - 0.13
        depth_b = 0.55 + rng.random() * 0.4
        if rng.random() < 0.4:
            nb = 3 if rng.random() < 0.4 else 2
            ztop = z1 - 0.12
            zbot = ztop
            for _k in range(nb):
                hb = 0.42 + rng.random() * 0.28
                _emit_box(shell, emit, bu, bu + 0.10, 0.10,
                          0.10 + depth_b, zbot - hb, zbot,
                          smats[rng.randrange(3)])
                zbot -= hb + 0.14
            _emit_box(shell, emit, bu + 0.02, bu + 0.08, 0.02, 0.12,
                      zbot + 0.10, ztop, M_METAL)     # mount bar (embeds
            # 20 mm into the cabinets -- a shared plane reads as tj)
        else:
            hb = 1.1 + rng.random() * 1.0
            _emit_box(shell, emit, bu + 0.02, bu + 0.08, 0.02, 0.12,
                      z1 - 0.15 - hb, z1 - 0.10, M_METAL)
            _emit_box(shell, emit, bu, bu + 0.10, 0.10, 0.10 + depth_b,
                      z1 - 0.15 - hb, z1 - 0.15, smats[rng.randrange(3)])


def _flat_front(shell, lines, p0, p1, z_par, z_top, t, emit,
                end_l=True, end_r=True, back_lo=None, back_hi=None,
                cap_full=True):
    """Raised FLAT false front: a rectangular parapet block with VERTICAL
    ends (a flat face must not ramp back down at its sides). Front face
    continues the wall plane from its top edge; back face + cap band can
    be clipped (back_lo/back_hi) so corner WRAPS mitre cleanly."""
    segs = [v for v in lines if p0 - 1e-9 <= v <= p1 + 1e-9]
    b_lo = p0 if back_lo is None else back_lo
    b_hi = p1 if back_hi is None else back_hi
    bsegs = [v for v in segs if b_lo - 1e-9 <= v <= b_hi + 1e-9]
    q = shell.quad
    for i in range(len(segs) - 1):
        ua, ub = segs[i], segs[i + 1]
        q(emit(ua, 0.0, z_par), emit(ub, 0.0, z_par),
          emit(ub, 0.0, z_top), emit(ua, 0.0, z_top), M_STUCCO)   # front
    for i in range(len(bsegs) - 1):
        ua, ub = bsegs[i], bsegs[i + 1]
        q(emit(ua, t, z_top), emit(ub, t, z_top),
          emit(ub, t, z_par), emit(ua, t, z_par), M_STUCCO)       # back
    for i in range(len(segs) - 1):
        ua, ub = segs[i], segs[i + 1]
        if cap_full or (b_lo - 1e-9 <= segs[i] and
                        segs[i + 1] <= b_hi + 1e-9):
            q(emit(ua, 0.0, z_top), emit(ub, 0.0, z_top),
              emit(ub, t, z_top), emit(ua, t, z_top), M_STUCCO)   # cap
    if end_l:
        q(emit(p0, 0.0, z_par), emit(p0, 0.0, z_top),
          emit(p0, t, z_top), emit(p0, t, z_par), M_STUCCO)
    if end_r:
        q(emit(p1, 0.0, z_top), emit(p1, 0.0, z_par),
          emit(p1, t, z_par), emit(p1, t, z_top), M_STUCCO)


def _emit_box(shell, emit, u0, u1, p0, p1, z0, z1, m):
    """Closed 6-quad box through an (u, proud, z) -> world map. Intended
    for recalc shells (windings normalised)."""
    c = [[[emit(u, pd, z) for z in (z0, z1)] for pd in (p0, p1)]
         for u in (u0, u1)]
    q = shell.quad
    q(c[0][0][0], c[1][0][0], c[1][0][1], c[0][0][1], m)
    q(c[0][1][0], c[0][1][1], c[1][1][1], c[1][1][0], m)
    q(c[0][0][0], c[0][1][0], c[1][1][0], c[1][0][0], m)
    q(c[0][0][1], c[1][0][1], c[1][1][1], c[0][1][1], m)
    q(c[0][0][0], c[0][0][1], c[0][1][1], c[0][1][0], m)
    q(c[1][0][0], c[1][1][0], c[1][1][1], c[1][0][1], m)


def _grille(shell, emit, u0, u1, z0, z1):
    """Security-bar grille over an opening: verticals + two rails."""
    u = u0 + 0.06
    while u < u1 - 0.05:
        _emit_box(shell, emit, u, u + 0.024, 0.03, 0.06, z0 + 0.02,
                  z1 - 0.02, M_METAL)
        u += 0.16
    for zr in (z0 + 0.22, z1 - 0.30):
        _emit_box(shell, emit, u0 + 0.02, u1 - 0.02, 0.055, 0.085,
                  zr, zr + 0.028, M_METAL)


def _awning(shell, emit, u0, u1, kind, depth, z_top=2.95):
    """Fabric awning over a storefront opening (open strips like the A1
    awnings; manual winding -- non-recalc shell). 'flat': one sloped
    panel + valance; 'barrel': quarter-arc strip."""
    keep = shell.tag
    shell.tag = 'awnings'
    if kind == 'flat':
        pts = [(0.03, z_top), (depth, z_top - 0.45)]
    else:
        import math as _m
        pts = [(0.03 + depth * _m.sin(_m.radians(a2)),
                z_top - depth + depth * _m.cos(_m.radians(a2)))
               for a2 in (0, 30, 60, 90)]
    for k in range(len(pts) - 1):
        (pa, za), (pb, zb) = pts[k], pts[k + 1]
        shell.quad(emit(u0, pb, zb), emit(u1, pb, zb),
                   emit(u1, pa, za), emit(u0, pa, za), M_TRIM)
    (pe, ze) = pts[-1]
    shell.quad(emit(u0, pe, ze - 0.22), emit(u1, pe, ze - 0.22),
               emit(u1, pe, ze), emit(u0, pe, ze), M_TRIM)   # valance
    # SIDE panels (awnings are not one surface): the region between the
    # top slope/arc and the valance-bottom line, per end.
    zb2 = ze - 0.22
    for (uu, rev) in ((u0, False), (u1, True)):
        for k in range(len(pts) - 1):
            (pa, za), (pb2, zb3) = pts[k], pts[k + 1]
            pts4 = [emit(uu, pa, za), emit(uu, pb2, zb3),
                    emit(uu, pb2, zb2), emit(uu, pa, zb2)]
            if rev:
                pts4.reverse()
            shell.quad(*pts4, M_TRIM)
    shell.tag = keep


def _rail_u(shell, x0, x1, yf, yb, zlo, zhi, th=0.12):
    """ONE welded U-shaped solid rail (two legs + front) with MITRED
    corners -- three abutting boxes left visible seams. Plan cells: legL /
    cornerL / front / cornerR / legR; only perimeter side faces emit, so
    the solid is manifold. yf = outer front plane, yb = leg-end plane
    (toward the building), legs along y."""
    xa0, xa1, xb1, xb0 = x0, x0 + th, x1 - th, x1
    yo, yi = yf, yf + th
    q = shell.quad
    cells = [(xa0, xa1, yi, yb), (xa0, xa1, yo, yi), (xa1, xb1, yo, yi),
             (xb1, xb0, yo, yi), (xb1, xb0, yi, yb)]
    for (cx0, cx1, cy0, cy1) in cells:
        q((cx0, cy0, zhi), (cx1, cy0, zhi), (cx1, cy1, zhi),
          (cx0, cy1, zhi), M_STUCCO)                         # top (+z)
        q((cx0, cy0, zlo), (cx0, cy1, zlo), (cx1, cy1, zlo),
          (cx1, cy0, zlo), M_STUCCO)                         # bottom (-z)
    for (sx0, sx1) in ((xa0, xa1), (xa1, xb1), (xb1, xb0)):
        q((sx0, yo, zlo), (sx1, yo, zlo), (sx1, yo, zhi),
          (sx0, yo, zhi), M_STUCCO)                          # outer front -y
    q((xa1, yi, zhi), (xb1, yi, zhi), (xb1, yi, zlo),
      (xa1, yi, zlo), M_STUCCO)                              # inner front +y
    for (sy0, sy1) in ((yo, yi), (yi, yb)):
        q((xa0, sy0, zhi), (xa0, sy1, zhi), (xa0, sy1, zlo),
          (xa0, sy0, zlo), M_STUCCO)                         # outer leg -x
        q((xb0, sy0, zlo), (xb0, sy1, zlo), (xb0, sy1, zhi),
          (xb0, sy0, zhi), M_STUCCO)                         # outer leg +x
    q((xa1, yi, zlo), (xa1, yb, zlo), (xa1, yb, zhi),
      (xa1, yi, zhi), M_STUCCO)                              # inner leg +x
    q((xb1, yi, zhi), (xb1, yb, zhi), (xb1, yb, zlo),
      (xb1, yi, zlo), M_STUCCO)                              # inner leg -x
    for (ex0, ex1) in ((xa0, xa1), (xb1, xb0)):
        q((ex0, yb, zlo), (ex1, yb, zlo), (ex1, yb, zhi),
          (ex0, yb, zhi), M_STUCCO)                          # leg ends +y


def _roof_band(shell, emit, u0, u1, kind, z_par, depth=1.6):
    """Sloped or barrel-curved roof plane BEHIND the parapet edge --
    part of the roof, rising from just above the cap ring backward (the
    funky-strip corrugated/tile look). `emit(u, pd, z)`: pd runs INWARD
    from the wall plane. A rear drop quad closes the silhouette."""
    keep = shell.tag
    shell.tag = 'roof'
    if kind == 'angled':
        pts = [(0.05, z_par + 0.02), (depth, z_par + 0.85)]
    else:
        import math as _m
        pts = [(0.05 + depth * _m.sin(_m.radians(a2)),
                z_par + 0.02 + 0.83 * (1.0 - _m.cos(_m.radians(a2))))
               for a2 in (0, 35, 65, 90)]
    for k in range(len(pts) - 1):
        (pa, za), (pb, zb) = pts[k], pts[k + 1]
        shell.quad(emit(u0, pa, za), emit(u1, pa, za),
                   emit(u1, pb, zb), emit(u0, pb, zb), M_METAL)
    (pe, ze) = pts[-1]
    shell.quad(emit(u0, pe, ze - 0.55), emit(u1, pe, ze - 0.55),
               emit(u1, pe, ze), emit(u0, pe, ze), M_METAL)  # rear drop
    shell.tag = keep


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


def _sloped_bar(shell, cx, hw, y0, z0, y1, z1, rt, mat):
    """A welded rectangular top-rail bar (width 2*hw in x, depth rt in z) swept in
    a straight line from (y0,z0) to (y1,z1) -- the pitched flight rail or a level
    return. Six quads, its own island, no coincident planes."""
    a0, a1 = (cx - hw, y0, z0), (cx + hw, y0, z0)             # base top edge
    a2, a3 = (cx + hw, y0, z0 - rt), (cx - hw, y0, z0 - rt)   # base bottom edge
    b0, b1 = (cx - hw, y1, z1), (cx + hw, y1, z1)             # far  top edge
    b2, b3 = (cx + hw, y1, z1 - rt), (cx - hw, y1, z1 - rt)   # far  bottom edge
    q = shell.quad
    q(a0, a1, b1, b0, mat)      # top (+z)
    q(a3, b3, b2, a2, mat)      # bottom (-z)
    q(a0, b0, b3, a3, mat)      # -x side
    q(a1, a2, b2, b1, mat)      # +x side
    q(a0, a3, a2, a1, mat)      # base cap
    q(b0, b1, b2, b3, mat)      # far cap


def _flight_railing(shell, x0, x1, y_base, y_top, z_base, z_top, foot_hi,
                    rail_h=0.92, post_w=0.045, rail_w=0.06, rail_t=0.05,
                    landing=0.34):
    """Welded pitched handrails on BOTH stringers of a straight flight (rpg-ro7o
    B1.3): posts embedded 20 mm into the stringer top, a top rail parallel to the
    pitch MITRED to a short level return over the top landing, and a taller newel
    at each end. Metal, tagged 'steps'; 100% quads, no coincident planes."""
    st, cover = 0.09, 0.06
    keep = shell.tag
    shell.tag = 'steps'
    span = y_top - y_base
    rise = z_top - z_base
    if abs(span) < 1e-4:
        shell.tag = keep
        return
    ydir = 1.0 if span >= 0 else -1.0

    def stop(y):                       # stringer top-surface z at plan y (pitch line)
        t = (y - y_base) / span
        return z_base + cover + rise * t

    hw = rail_w * 0.5
    for (sx0, sx1) in ((x0, x0 + st), (x1 - st, x1)):
        cx = 0.5 * (sx0 + sx1)
        # posts (embedded 20 mm into the stringer top), evenly along the pitch.
        n = max(2, int(round(abs(span) / 1.0)))
        for k in range(n + 1):
            y = y_base + span * (k / n)
            zt = stop(y)
            hnewel = rail_h + (0.12 if (k == 0 or k == n) else 0.0)   # taller newels
            _box(shell, (cx - post_w / 2, y - post_w / 2, zt - 0.02),
                        (cx + post_w / 2, y + post_w / 2, zt + hnewel), M_METAL)
        # pitched top rail, base -> just PAST the pitch break (overlaps the level
        # return below so the two bars are separate manifold islands that weld
        # visually at the newel -- butting them exactly shared edges = non-manifold).
        zb0, zb1 = stop(y_base) + rail_h, stop(y_top) + rail_h
        over = 0.12
        y_ext = y_top + ydir * over
        z_ext = zb1 + (rise / span) * (over * ydir)   # continue the pitch slope
        _sloped_bar(shell, cx, hw, y_base, zb0, y_ext, z_ext, rail_t, M_METAL)
        # level return over the top landing: starts BACK down the pitch (its base
        # cap buried inside the pitched bar) and runs level onto the deck.
        yl0 = y_top - ydir * over
        yl1 = y_top + ydir * landing
        _sloped_bar(shell, cx, hw - 0.003, yl0, zb1, yl1, zb1, rail_t,
                    M_METAL)
    shell.tag = keep


def build_minimall(p, rng):
    """Build the mini-mall per the module topology plan. Returns objects."""
    n = p["tenants"]
    tw = p["tenant_width"]
    D = p["depth"]
    cd = p["canopy_depth"]
    Wm = n * tw
    layout = p["layout"]
    corner = layout in ('L', 'C', 'E')   # east court arm present
    amw = 8.0                            # mid-arm width (E layout)
    # 'angled': the west end wall runs diagonally (wx at the front to 0 at
    # the rear) -- the boulevard-cut lot line.
    wx = 3.5 if layout == 'angled' else 0.0
    e_ang = wx + 0.25                    # east extent of the custom roof:
    # NARROW, so no tenant/pier grid line falls inside it -- rectilinear
    # splits crossing the slanted strips would cascade T-junctions (a
    # diagonal region can only stay all-quad if it is isolated from
    # foreign grid lines).
    office = p["office_strip"]
    if office:
        # the office storey's balcony deck IS the canopy: it must be a
        # walkable depth (canopy_depth=0 degenerated the deck boxes into
        # 2 mm slivers whose edges collided with each other).
        cd = max(cd, 1.5)
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
    zl = [0.0, Z_BLK, 0.9, 1.15, Z_SRV, Z_DR, Z_SF, Z_DHEAD2, Z_FAS,
          z_ceil, z_roof, z_par]
    if office:
        zl += [Z_OSIL, Z_OHED]
    zl = sorted(set(zl))

    # ---- anchor tenant: double-width, taller, proud of the run -------------
    # (mutually exclusive with office_strip: the anchor mass would punch a
    # sky slot through the second storey).
    anchor_on = p["anchor"] and not office and n >= 4 and layout != 'E'
    all_bays = [(b0 + wx, b1 + wx, o0 + wx, o1 + wx)
                for (b0, b1, o0, o1) in _tenant_bays(Wm - wx, n)]
    ka = -1
    a0 = a1 = 0.0
    aw_n = 2
    if anchor_on:
        # a grocery anchor is SOMETIMES wider: 3 bays when the run allows.
        aw_n = 3 if (n >= 6 and rng.random() < 0.4) else 2
        ka = rng.choice([0, max(0, n // 2 - 1), n - aw_n])
        a0, a1 = all_bays[ka][0], all_bays[ka + aw_n - 1][1]
    # ---- tenant fates: shuttered fraction + story bays ---------------------
    bays_m = [b3 for i3, b3 in enumerate(all_bays)
              if not (anchor_on and ka <= i3 < ka + aw_n)]
    bays_w = _tenant_bays(Wy, n_w) if corner else []
    shut_p = 1.0 if dead else p["shutters"]

    def roll_fate():
        if rng.random() < shut_p:
            return 'shut'
        if rng.random() < p["bars"]:
            return 'barred'
        return 'open'

    fates_m = [roll_fate() for _ in bays_m]
    fates_w = [roll_fate() for _ in bays_w]
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

    # per-bay HIGH BULKHEAD (the reference roll-up-on-knee-wall look):
    # glazing sits on 0.9 m masonry instead of 0.25 m; shutters stop there
    # instead of dropping to grade.
    high_bk_m = [rng.random() < p["high_bulkhead"] for _ in bays_m]

    # TWO-STORY SHOPS (rpg-a1ep integration): open bays promoted to a
    # two-level unit -- real floor slab + demising walls + internal stair,
    # no balcony door, and the deck/rail run BREAKS across their front.
    shop2 = set()
    if p["office_strip"]:
        for i9, f9 in enumerate(fates_m):
            if f9 == 'open' and rng.random() < p.get("two_story_shops", 0.25):
                shop2.add(i9)
    shop2_spans = [(bays_m[i9][2] - 0.002, bays_m[i9][3] + 0.002)
                   for i9 in sorted(shop2)]

    def _seg_subtract(segs, cuts):
        out9 = []
        for (s0, s1) in segs:
            cur = s0
            for (c0, c1) in sorted(cuts):
                if c1 < s0 or c0 > s1:
                    continue
                if c0 - 0.002 > cur:
                    out9.append((cur, c0 - 0.002))
                cur = max(cur, c1 + 0.002)
            if s1 > cur:
                out9.append((cur, s1))
        return out9
    high_bk_w = [rng.random() < p["high_bulkhead"] for _ in bays_w]
    # rear LOADING DOCKS: heavy sectional door at truck-sill height per
    # tenant, seeded ~40% inset into a recessed dock well.
    docks_on = p["loading_docks"]
    Z_DSILL, Z_DHEAD = 1.15, Z_DHEAD2   # ~2.4 m clear above the sill

    def dock_layout(b0, b1):
        c = (b0 + b1) / 2.0
        man = (b0 + 0.5, b0 + 1.4)
        dw = min(3.0, (b1 - b0) * 0.55)
        j1 = min(c + 0.3 + dw, b1 - 0.35)   # CLAMP inside the bay: the
        # last bay's dock overflowed past the building end wall.
        dock = (j1 - dw, j1)
        return man, dock

    dock_m, dock_w = [], []
    if docks_on:
        # a strip mall shares ONE dock (occasionally two) for the whole
        # building -- not one per tenant.  Undocked bays keep only their
        # rear man-door (dock=None).
        n_bays_all = len(bays_m) + len(bays_w)
        n_docks = min(n_bays_all, 2 if rng.random() < 0.35 else 1)
        dock_picks = set(rng.sample(range(n_bays_all), n_docks))
        for k9, (b0, b1, _o0, _o1) in enumerate(bays_m):
            man, dock = dock_layout(b0, b1)
            if k9 not in dock_picks:
                dock = None
            dock_m.append((man, dock,
                           dock is not None and rng.random() < 0.4))
        for k9, (b0, b1, _o0, _o1) in enumerate(bays_w):
            man, dock = dock_layout(b0, b1)
            if (k9 + len(bays_m)) not in dock_picks:
                dock = None
            dock_w.append((man, dock,
                           dock is not None and rng.random() < 0.4))

    def door_span(o0, o1, left):
        # FLUSH against the pier: any gap strip narrower than the frame
        # inset becomes an inverted-ring sliver window (auditor-caught).
        return (o0, o0 + 0.95) if left else (o1 - 0.95, o1)

    doors_m = [door_span(o0, o1, rng.random() < 0.5)
               for (_b0, _b1, o0, o1) in bays_m]
    doors_w = [door_span(o0, o1, rng.random() < 0.5)
               for (_b0, _b1, o0, o1) in bays_w]

    def barred_windows(bays, doors, fates):
        # one or two narrow (0.8 m) high-sill windows on the blank side
        # of the door.
        out = {}
        for i, (_b0, _b1, o0, o1) in enumerate(bays):
            if fates[i] != 'barred':
                continue
            d0, d1 = doors[i]
            lo2, hi2 = (d1, o1) if d1 - o0 < o1 - d0 else (o0, d0)
            span2 = hi2 - lo2
            wins = []
            if span2 > 1.6:
                wins.append((lo2 + 0.35, lo2 + 1.15))
            if span2 > 3.2:
                wins.append((hi2 - 1.15, hi2 - 0.35))
            out[i] = wins
        return out

    bw_m = barred_windows(bays_m, doors_m, fates_m)
    bw_w = barred_windows(bays_w, doors_w, fates_w)

    # ---- extra court arms: WEST ('[' / C, E) + MID (E). Each is a wing
    # mass off the run's south face; the EAST arm keeps the original wing
    # path. face 'e': storefront on the arm's east plane facing the court;
    # face 'w': storefront on its west plane. --------------------------------
    xarms = []
    if layout in ('C', 'E'):
        xarms.append(dict(face='e', fx=0.0, ox0=-D, ox1=0.0))
    if layout == 'E':
        xm0 = round((Wm / 2.0 - amw / 2.0) / tw) * tw
        xarms.append(dict(face='w', fx=xm0, ox0=xm0, ox1=xm0 + amw))
    for xa in xarms:
        ab = _tenant_bays(Wy, n_w)
        xa['bays'] = ab
        xa['fates'] = [roll_fate() for _ in ab]
        xa['doors'] = [door_span(o0, o1, rng.random() < 0.5)
                       for (_b0, _b1, o0, o1) in ab]
        xa['high'] = [rng.random() < p["high_bulkhead"] for _ in ab]
        xa['bw'] = barred_windows(ab, xa['doors'], xa['fates'])

    # per-bay balcony treatments on the office storey (rule-2 variety):
    # 'run' flat deck, 'projecting' bump-out with rail wrap, 'recessed'
    # loggia (floor-2 wall recesses 1.3 m, deck floor continues in).
    # Rolled AFTER the court arms exist: a projection is refused wherever
    # its bump-out would hang into a wing/arm deck strip or the prong
    # mass itself (coincident or interpenetrating geometry).
    bal_modes = {}
    recessed = set()
    if office:
        for i, (_b0, _b1, o0b, o1b) in enumerate(bays_m):
            sel = p["balconies"]
            if i in shop2:
                sel = 'run'                # two-story shop: flat run, and
            elif sel == 'mixed':           # the rail breaks there anyway
                sel = rng.choice(['run', 'projecting', 'recessed'])
            if sel == 'projecting' and corner and o1b > Wm - cd - 0.6:
                sel = 'recessed'      # would hang into the east wing deck
            if sel == 'projecting' and any(
                    o0b + 0.35 < xa['ox1'] + cd + 0.3 and
                    o1b - 0.35 > xa['ox0'] - cd - 0.3 for xa in xarms):
                sel = 'recessed'      # would hang into a court arm's
                # deck strip / stand inside the prong mass
            bal_modes[i] = sel
            if sel == 'recessed':
                recessed.add(i)

    # ---- mitred corner doorway (bar/L, rectilinear west corner) ------------
    cdoor_on = p["corner_door"] and layout in ('bar', 'L')
    c1d = 1.7
    # ---- raised parapet fronts: seeded per bay, gable PEAK or flat HIGH
    # FALSE FRONT (the funky-strip look); roof bands suppress them. --------
    aw_pk = 0.16
    band_on = p["roof_band"] != 'none'
    pk_frac = 0.0 if band_on else p["peak_fraction"]

    FLAT_AW = 1e6                     # aw sentinel: flat false front

    def roll_peak(pb0, pb1, prev):
        # ADJACENT raised fronts must share a form: a gable half-merging
        # into a flat block leaves coplanar/naked joints.
        xc = (pb0 + pb1) / 2.0
        if prev is not None and abs(prev[1] - pb0) < 1e-6:
            form_flat = prev[4] > 1.0
        else:
            form_flat = rng.random() < 0.5
        if form_flat:
            if prev is not None and abs(prev[1] - pb0) < 1e-6 and \
                    prev[4] > 1.0:
                zt5 = prev[3]         # merged flats share ONE height --
                # differing tops T-junction at the shared bay line
            else:
                zt5 = z_par + 0.7 + rng.random() * 0.7
            return (pb0, pb1, xc, zt5, FLAT_AW)
        return (pb0, pb1, xc, z_par + 0.55 + rng.random() * 0.5, aw_pk)

    peaks_m, peaks_w = [], []
    for (b0, b1, _o0, _o1) in bays_m:
        if rng.random() < pk_frac:
            pb0 = max(b0, e_ang) if layout == 'angled' else b0
            # (clamped east of the slant strip: a peak base over the
            # custom band would put 3 faces on the wall-top line)
            peaks_m.append(roll_peak(pb0, b1,
                                     peaks_m[-1] if peaks_m else None))
    for (b0, b1, _o0, _o1) in bays_w:
        if rng.random() < pk_frac:
            peaks_w.append(roll_peak(b0, b1,
                                     peaks_w[-1] if peaks_w else None))
    for xa in xarms:
        xa['peaks'] = []
        for (b0, b1, _o0, _o1) in xa['bays']:
            if rng.random() < pk_frac:
                xa['peaks'].append(
                    roll_peak(b0, b1,
                              xa['peaks'][-1] if xa['peaks'] else None))
    # a MIDDLE prong's raised front must stop short of the root when a
    # main-run raised front spans the prong mouth: the arm front runs
    # up the flank wall to y=0 at its own height and its top verts land
    # mid-edge on the (differently tall) main front's plane there. The
    # west corner arm is exempt -- _sync_corner + the join column own
    # that junction.
    for xa in xarms:
        if xa['ox1'] <= 0.0:
            continue
        if any(pk4[0] < xa['ox1'] - 1e-6 and pk4[1] > xa['ox0'] + 1e-6
               for pk4 in peaks_m):
            xa['peaks'] = [pk4 for pk4 in xa['peaks']
                           if abs(pk4[1] - Wy) > 1e-6]
    # raised fronts that MEET at a building corner (main run vs wing /
    # west arm) must share form and height -- mismatched tops put the
    # shorter one's verts on the taller one's corner edge.
    def _sync_corner(main_pk, other_list):
        for i4, pk4 in enumerate(other_list):
            if abs(pk4[1] - Wy) < 1e-6:
                other_list[i4] = (pk4[0], pk4[1],
                                  (pk4[0] + pk4[1]) / 2.0, main_pk[3],
                                  FLAT_AW if main_pk[4] > 1.0 else aw_pk)
                # cascade DOWN the arm's merged chain until it is
                # consistent again: adjacent fronts must share a form,
                # and adjacent FLATS must also share a height -- flipping
                # the corner front's form used to leave a chained peak
                # butting into a flat (its stub verts landed mid-edge on
                # the taller front's corner edge).
                new_flat = main_pk[4] > 1.0
                j4 = i4 - 1
                while (j4 >= 0 and
                       abs(other_list[j4][1] - other_list[j4 + 1][0])
                       < 1e-6):
                    o5 = other_list[j4]
                    o_flat = o5[4] > 1.0
                    if o_flat == new_flat and \
                            (not new_flat or
                             abs(o5[3] - main_pk[3]) < 1e-6):
                        break         # chain consistent from here down
                    if new_flat:
                        other_list[j4] = (o5[0], o5[1], o5[2],
                                          main_pk[3], FLAT_AW)
                    else:
                        # peak-peak junctions weld at the shared stub
                        # regardless of apex height: keep its own top.
                        other_list[j4] = (o5[0], o5[1],
                                          (o5[0] + o5[1]) / 2.0,
                                          o5[3], aw_pk)
                    j4 -= 1

    if corner and peaks_m and abs(peaks_m[-1][1] - Wm) < 1e-6:
        _sync_corner(peaks_m[-1], peaks_w)
    for xa in xarms:
        if xa['ox1'] <= 0.0 and peaks_m and \
                abs(peaks_m[0][0] - wx) < 1e-6:
            _sync_corner(peaks_m[0], xa['peaks'])
    # a flat front reaching a FREE building corner WRAPS around it: the
    # raised band returns 2.5 m along the side wall.
    wrap_ret = 2.5
    wrap_w = wrap_e = None
    if peaks_m and peaks_m[0][4] > 1.0 and layout not in ('angled',) and \
            not any(xa['ox1'] <= 0.0 for xa in xarms) and \
            abs(peaks_m[0][0] - wx) < 1e-6:
        wrap_w = peaks_m[0]
    if peaks_m and peaks_m[-1][4] > 1.0 and not corner and \
            abs(peaks_m[-1][1] - Wm) < 1e-6:
        wrap_e = peaks_m[-1]

    # ---- line families -----------------------------------------------------
    xl = {0.0, Wm, Wm - t, We, We - t}
    if layout != 'angled':
        xl.add(t)                     # west cap-corner line (rectilinear
        # corners only; the angled slant region replaces it)
    for (p0k, p1k, xck, _z, _aw) in peaks_m:
        xl |= {p0k, p1k}
        if _aw < 1.0:
            xl |= {xck - _aw / 2.0, xck + _aw / 2.0}
    if corner:
        xl.add(Wm + t)                # wing-front cap band inset line
    if anchor_on:
        xl |= {a0, a1, a0 - t, a1 + t}   # run cut ends + their cap bands
    for xa in xarms:
        xl |= {xa['ox0'], xa['ox1'], xa['ox0'] + t, xa['ox1'] - t}
    if layout == 'angled':
        dxt_a = t * ((wx * wx + D * D) ** 0.5) / D
        ei2_a = 0.002                 # the slant roof island's inset
        xl |= {wx, e_ang}
    if cdoor_on:
        xl.add(c1d)
        # NOTE the cap-line endpoints (wx+dxt, dxt) are NOT grid lines --
        # they are injected into the front/rear WALL u_lines only, so the
        # wall top edges weld the strip corners without polluting the
        # roof-strip / drop column splits (which must never split at a
        # line that crosses a slanted bound mid-row).
    for i in recessed:
        (_b0, _b1, o0r, o1r) = bays_m[i]
        # end-bay clamp: o0-wt can COINCIDE with the side wall's inner
        # plane (verts on its edges); keep 50 mm clear.
        xl |= {max(o0r - wt, wt + 0.05), min(o1r + wt, Wm - wt - 0.05)}
    for (_mn, dk9, ins) in dock_m:
        if dk9 is not None and ins:
            (j0d, j1d) = dk9
            xl |= {j0d - wt, j1d + wt}    # inset dock inner-skin strips
    for (b0, b1, o0, o1) in bays_m:
        xl |= {b0, b1, o0, o1}
    for (d0, d1) in doors_m:
        xl |= {d0, d1}
    for wins in bw_m.values():
        for pr in wins:
            xl |= set(pr)
    yl = {0.0, D, t, D - t}
    if wrap_w is not None or wrap_e is not None:
        yl.add(wrap_ret)              # the corner wrap's end line
    if cdoor_on:
        yl.add(c1d)
    if corner or xarms:
        yl |= {-Wy, -Wy + t}
    if office and any(xa['ox1'] > 0.0 for xa in xarms):
        yl |= {-1.5, -0.55}           # mid-arm pass-door jambs
    for xa in xarms:
        for (b0, b1, o0, o1) in xa['bays']:
            yl |= {b0 - Wy, b1 - Wy, o0 - Wy, o1 - Wy}
        for (d0, d1) in xa['doors']:
            yl |= {d0 - Wy, d1 - Wy}
        for wins in xa['bw'].values():
            for (w0b, w1b) in wins:
                yl |= {w0b - Wy, w1b - Wy}
        for (p0k, p1k, xck, _z, _aw) in xa['peaks']:
            yl |= {p0k - Wy, p1k - Wy}
            if _aw < 1.0:
                yl |= {xck - _aw / 2.0 - Wy, xck + _aw / 2.0 - Wy}
    if corner:
        # EAST wing lines (these were accidentally swallowed into the
        # xarms loop -- plain L has no xarms, so they never ran).
        for (p0k, p1k, xck, _z, _aw) in peaks_w:
            yl |= {-Wy + p0k, -Wy + p1k}
            if _aw < 1.0:
                yl |= {-Wy + xck - _aw / 2.0, -Wy + xck + _aw / 2.0}
        for (b0, b1, o0, o1) in bays_w:
            yl |= {b0 - Wy, b1 - Wy, o0 - Wy, o1 - Wy}
        for (d0, d1) in doors_w:
            yl |= {d0 - Wy, d1 - Wy}
        for wins in bw_w.values():
            for (w0b, w1b) in wins:
                yl |= {w0b - Wy, w1b - Wy}
    # rear service man-doors (+ dock jambs when loading docks are on).
    if docks_on:
        srv_m = [mn for (mn, _dk, _ins) in dock_m]
        for (mn, dk, _ins) in dock_m:
            xl |= set(mn) | (set(dk) if dk is not None else set())
        srv_w = [(mn[0] - Wy, mn[1] - Wy) for (mn, _dk, _ins) in dock_w]
        for (mn, dk, _ins) in dock_w:
            yl |= {mn[0] - Wy, mn[1] - Wy}
            if dk is not None:
                yl |= {dk[0] - Wy, dk[1] - Wy}
    else:
        srv_m = [((b0 + b1) / 2.0 - 0.45, (b0 + b1) / 2.0 + 0.45)
                 for (b0, b1, _o0, _o1) in bays_m]
        for pr in srv_m:
            xl |= set(pr)
        srv_w = [((b0 + b1) / 2.0 - Wy - 0.45, (b0 + b1) / 2.0 - Wy + 0.45)
                 for (b0, b1, _o0, _o1) in bays_w]
        for pr in srv_w:
            yl |= set(pr)
    x_min = min([0.0] + [xa['ox0'] for xa in xarms])
    xl = sorted(v for v in xl if x_min - 1e-9 <= v <= We + 1e-9)
    yl = sorted(v for v in yl if (-Wy if (corner or xarms) else 0.0) - 1e-9
                <= v <= D + 1e-9)

    def inside(x, y):
        if layout == 'angled' and x < e_ang:
            return False              # the custom slant roof owns this
        if anchor_on and a0 < x < a1 and -1e-9 < y:
            return False              # the anchor mass owns this span
        if 0.0 < x < Wm and 0.0 < y < D:
            return True
        if corner and Wm < x < We and -Wy < y < D:
            return True
        # the main/wing seam column belongs to the footprint too.
        if corner and abs(x - Wm) < 1e-9 and -1e-9 < y < D:
            return True
        for xa in xarms:
            if xa['ox0'] < x < xa['ox1'] and -Wy < y < \
                    (D if xa['ox1'] <= 0.0 else 0.0):
                return True
            if abs(x - xa['ox1']) < 1e-9 and xa['ox1'] <= 0.0 and \
                    -1e-9 < y < D:
                return True           # west-arm/run seam column
        return False

    shell = _Shell()
    thick = wt if interior_on else 0.0
    iz_max = z_ceil

    # ---- storefront walls --------------------------------------------------
    shell.tag = 'storefront'
    cls_m = _storefront_classify(bays_m, doors_m, fates_m, Z_SF, office,
                                 shop2=frozenset(shop2),
                                 void_span=(a0, a1) if anchor_on else None,
                                 recessed=recessed, wt=wt, run_w=Wm,
                                 high_bk=high_bk_m, barred_wins=bw_m)
    mid_span = next(((xa['ox0'], xa['ox1']) for xa in xarms
                     if xa['ox1'] > 0.0), None)
    if mid_span:
        cls_m_base = cls_m

        def cls_m(u0, zc0):           # the mid arm passes through the run
            if mid_span[0] - 1e-6 <= u0 < mid_span[1] - 1e-6:
                return 'void'
            return cls_m_base(u0, zc0)
    if cdoor_on:
        cls_m_base2 = cls_m

        def cls_m(u0, zc0):           # the mitred corner owns the ground
            if u0 < c1d - 1e-6 and zc0 < Z_SF - 1e-6:
                return 'void'
            return cls_m_base2(u0, zc0)

    # B1.4 (rpg-20cn): real door leafs hang in every doorL opening --
    # storefront glass singles/pairs on the fronts and balcony rows,
    # hollow-metal man-doors on the service walls; a seeded fraction
    # stands ajar (dead-mall tie-in).  One shared filler per style keeps
    # the rng draws inside the deterministic fill order.
    aj = p.get("door_ajar", 0.15)
    leaf_glass = doorkit.glass_leaf_filler(rng, aj)
    leaf_slab = doorkit.man_door_filler(rng, min(1.0, aj * 0.7))

    fw_lines = [v for v in xl if wx - 1e-9 <= v <= Wm + 1e-9]
    _wf = _Wall(shell, (0, 0, 0), (1, 0, 0), fw_lines, zl,
                (0, -1, 0), M_STUCCO, thickness=thick, inner_zmax=iz_max)
    _wf.inner_u0, _wf.inner_u1 = wx + wt, Wm - (0.0 if corner else wt)
    _wf.fill(cls_m, frame=0.06, mat_frame=M_TRIM, mat_pane=M_GLASS,
             leaf=leaf_glass)
    # storefront-bay dressing (rpg-a1ep): checkerboard tile piers + bulkhead,
    # aluminium mullions/transom/head channel, entry door frame -- applied
    # PROUD of the wall plane on every OPEN front bay. Left pier per bay
    # (plus a right pier on the last) so adjacent dressings never overlap.
    sf_extra_obs = []
    if p.get("storefront_detail", True):
        open_idx = [i for i, f9 in enumerate(fates_m) if f9 == 'open']
        sf_styles = ('checker', 'tile', 'stucco', 'panel')
        for i in open_idx:
            (_b0, _b1, o0, o1) = bays_m[i]
            bh9 = 0.9 if high_bk_m[i] else 0.62
            style9 = p.get("storefront_style", 'mixed')
            if style9 == 'mixed':
                style9 = sf_styles[rng.randrange(len(sf_styles))]
            glz9 = 'plate' if rng.random() < 0.3 else 'mullioned'
            emit_storefront_bay(shell, o0, o1, 0.0, doors_m[i], Z_SF,
                                bulkhead=bh9, bulkhead_style=style9,
                                glazing=glz9,
                                piers=(True, i == open_idx[-1]))
            # kit extras (rpg-a1ep variants): iron bars, awnings, blades.
            roll9 = rng.random()
            aw_here = False
            if roll9 < 0.22:
                # security is per-TENANT: bar every glazed span AND give
                # the door its own roll-up (mostly raised on open bays,
                # sometimes dropped) -- never a grille over one opening
                # with the next one naked, and never a door barred shut.
                (d0b, d1b) = doors_m[i]
                for (g0b, g1b) in ((o0 + 0.05, d0b - 0.05),
                                   (d1b + 0.05, o1 - 0.05)):
                    if g1b - g0b > 0.30:
                        emit_security_bars(shell, g0b, g1b, 0.0,
                                           bh9 + 0.10, Z_SF - 0.14)
                emit_rollup(shell, d0b + 0.078, d1b - 0.078, -0.012,
                            min(2.2, Z_SF - 0.35) + 0.42,
                            open_frac=0.75 if rng.random() < 0.6 else 0.0,
                            housing=True, z0=0.02)
            elif roll9 < 0.48 and (i in shop2 or
                                   not (office or cd >= 0.5)):
                aw9 = o1 - o0 - 0.12
                if aw9 > 1.1:
                    # ABOVE the window head (not against the glass), spanning
                    # the whole bay's window row.
                    aw_here = True
                    ap9 = dict(width=aw9, depth=0.95, drop=0.45,
                               valance=0.24, stripes=True)
                    for ob9 in el2.build_canvas_awning(ap9, rng):
                        ob9.location = (o0 + 0.06, -0.048, Z_SF + 0.04)
                        sf_extra_obs.append(ob9)
            if rng.random() < 0.18:
                bx9, tz9 = o0 + 0.06, Z_SF - 0.15
                if aw_here:
                    # awning + blade on one bay: the blade would thread the
                    # awning's skirt -- shove it WELL clear horizontally
                    # (centred on the tiled pier beside the opening) and
                    # drop it a touch below the fabric's underside.
                    bx9, tz9 = o0 - 0.094, Z_SF - 0.45
                bp9 = dict(height=1.7, projection=0.75, panels=3,
                           top_z=tz9)
                for ob9 in el2.build_blade_sign(bp9, rng):
                    ob9.rotation_euler = (0.0, 0.0, -1.5707963)
                    # mount plate ON the outer wall face (5 mm embed), the
                    # blade projecting out -- never buried inside the wall.
                    ob9.location = (bx9, 0.005, 0.0)
                    sf_extra_obs.append(ob9)

        # TWO-STORY SHOP structure: real floor slab (with a stairwell), full-
        # height demising walls, a rear partition, an internal straight
        # stair, and a mullioned floor-2 shopfront over the ribbon window.
        n_st9 = max(3, int(round(Z_FAS / 0.185)))
        run9 = 0.27 * n_st9
        for i in sorted(shop2):
            (b0, b1, o0, o1) = bays_m[i]
            dep9 = min(p["depth"] - 0.6, 7.4)
            wx0, wx1 = b0 + 0.16, b0 + 1.30        # stairwell strip (left)
            wy0 = 1.0 + run9 * 0.45                # well over the upper run
            shell.tag = 'slabs'
            # rear slab OVERLAPS the front slab 60 mm with 4 mm z-insets
            # (butted boxes shared faces -> non-manifold), and its far end
            # tucks inside the rear wall.
            _box(shell, (b0 + 0.05, wt + 0.054, Z_FAS - 0.16),
                 (b1 - 0.05, wy0, Z_FAS + 0.02), M_CONCRETE)
            _box(shell, (wx1 + 0.06, wy0 - 0.06, Z_FAS - 0.156),
                 (b1 - 0.055, dep9 + 0.03, Z_FAS + 0.016), M_CONCRETE)
            shell.tag = 'demising'
            # demising walls run PAST the rear plane (ends hidden inside the
            # rear wall); the rear wall spans strictly BETWEEN them with its
            # ends embedded in their thickness -- no shared corner planes.
            _wall_solid(shell, 'y', b0 + 0.06, wt + 0.06, dep9 + 0.04, 0.0,
                        Z_OHED - 0.12, 0.09, None, M_STUCCO)
            _wall_solid(shell, 'y', b1 - 0.15, wt + 0.06, dep9 + 0.04, 0.0,
                        Z_OHED - 0.12, 0.09, None, M_STUCCO)
            _wall_solid(shell, 'x', dep9, b0 + 0.10, b1 - 0.11, -0.004,
                        Z_OHED - 0.124, 0.09, None, M_STUCCO)
            shell.tag = 'steps'
            _flight_straight(shell, wx0, wx1, 1.0, 1.0 + run9,
                             0.0, Z_FAS - 0.14, 0.0, 0.14)
            # floor-2 shopfront: sill band + mullions over the ribbon.
            shell.tag = 'storefront'
            _box(shell, (o0 + 0.014, -0.035, Z_OSIL - 0.055),
                 (o1 - 0.014, 0.03, Z_OSIL + 0.006), M_METAL)
            nm9 = max(2, int(round((o1 - o0) / 1.0)))
            for k9 in range(nm9 + 1):
                mx9 = o0 + 0.014 + (o1 - o0 - 0.028) * k9 / nm9
                _box(shell, (mx9 - 0.024, -0.032, Z_OSIL + 0.02),
                     (mx9 + 0.024, 0.05, Z_OHED - 0.09), M_METAL)
    # court-arm storefronts + their outer/south walls.
    for xa in xarms:
        ab, ad, af, ah = xa['bays'], xa['doors'], xa['fates'], xa['high']
        cls_a2 = _storefront_classify(ab, ad, af, Z_SF, office, high_bk=ah,
                                      barred_wins=xa['bw'])
        fx = xa['fx']
        nrm = (1, 0, 0) if xa['face'] == 'e' else (-1, 0, 0)
        mid_pass = office and xa['ox1'] > 0.0

        def _with_pass_door(base):
            # E-layout middle bar: the balcony deck is cut around the
            # prong, so its floor-2 gets a door on EACH flank at the
            # deck line -- one can walk the whole balcony through it.
            def cls9(u0, zc0, _b=base):
                if (Wy - 1.5 - 1e-6 <= u0 < Wy - 0.55 - 1e-6 and
                        Z_FAS - 1e-6 <= zc0 < Z_OHED - 1e-6):
                    if abs(zc0 - Z_FAS) < 1e-6:
                        return 'doorL'
                    return 'doorU'
                return _b(u0, zc0)
            return cls9

        shell.tag = 'storefront'
        _wa2 = _Wall(shell, (fx, -Wy, 0), (0, 1, 0),
                     [v + Wy for v in yl if v <= 0.0 + 1e-9], zl, nrm,
                     M_STUCCO, thickness=thick, inner_zmax=iz_max)
        _wa2.inner_u0, _wa2.inner_u1 = wt, Wy
        _wa2.fill(_with_pass_door(cls_a2) if mid_pass else cls_a2,
                  frame=0.06, mat_frame=M_TRIM, mat_pane=M_GLASS,
                  leaf=leaf_glass)
        shell.tag = 'facade_side'
        if xa['ox1'] <= 0.0:
            # west arm: its outer plane is the building's west wall.
            _wo2 = _Wall(shell, (xa['ox0'], 0, 0), (0, 1, 0), yl, zl,
                         (-1, 0, 0), M_STUCCO, thickness=thick,
                         inner_zmax=iz_max)
            _wo2.inner_u0, _wo2.inner_u1 = -Wy + wt, D - wt
            _wo2.fill(plain_wall)
        else:
            # mid arm: plain east flank, court side only.
            _wo2 = _Wall(shell, (xa['ox1'], -Wy, 0), (0, 1, 0),
                         [v + Wy for v in yl if v <= 0.0 + 1e-9], zl,
                         (1, 0, 0), M_STUCCO, thickness=thick,
                         inner_zmax=iz_max)
            _wo2.inner_u0, _wo2.inner_u1 = wt, Wy
            _wo2.fill(_with_pass_door(plain_wall) if mid_pass
                      else plain_wall, frame=0.06, mat_frame=M_TRIM,
                      mat_pane=M_GLASS, leaf=leaf_glass)
        # south end wall.
        _ws2 = _Wall(shell, (0, -Wy, 0), (1, 0, 0),
                     [v for v in xl if xa['ox0'] - 1e-9 <= v <=
                      xa['ox1'] + 1e-9], zl, (0, -1, 0), M_STUCCO,
                     thickness=thick, inner_zmax=iz_max)
        _ws2.inner_u0, _ws2.inner_u1 = xa['ox0'] + wt, xa['ox1'] - wt
        _ws2.fill(plain_wall)
    if corner:
        cls_w = _storefront_classify(bays_w, doors_w, fates_w, Z_SF, office,
                                     high_bk=high_bk_w, barred_wins=bw_w)

        def cls_wing(u0, zc0):        # u = y + Wy along the wing run
            return cls_w(u0, zc0)

        _ww = _Wall(shell, (Wm, -Wy, 0), (0, 1, 0),
                    [v + Wy for v in yl if v <= 0.0 + 1e-9], zl, (-1, 0, 0),
                    M_STUCCO, thickness=thick, inner_zmax=iz_max)
        _ww.inner_u0, _ww.inner_u1 = wt, Wy - 0.0
        _ww.fill(cls_wing, frame=0.06, mat_frame=M_TRIM, mat_pane=M_GLASS,
                 leaf=leaf_glass)

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
        if docks_on:
            for (_mn, dk9, inset) in dock_m:
                if dk9 is None:
                    continue
                (j0, j1) = dk9
                in_row = Z_DSILL - 1e-6 <= zc0 < Z_DHEAD - 1e-6
                if j0 - 1e-6 <= u0 < j1 - 1e-6 and in_row:
                    return 'void' if inset else 'window_dock'
                if inset and in_row and interior_on and \
                        j0 - wt - 1e-6 <= u0 < j1 + wt - 1e-6:
                    return 'void_in'
        return 'wall'

    _wr = _Wall(shell, (0, D, 0), (1, 0, 0), xl, zl, (0, 1, 0),
                M_STUCCO, thickness=thick, inner_zmax=iz_max)
    _wr.inner_u0, _wr.inner_u1 = x_min + wt, We - wt
    _wr.fill(rear_classify, frame=0.06, mat_frame=M_TRIM, leaf=leaf_slab)
    # inset dock wells: the A1/loggia recess discipline mirrored onto the
    # rear wall (outward +y, recess depth 0.6 INTO the building).
    if docks_on:
        dk_rows = [v for v in zl if Z_DSILL - 1e-6 <= v <= Z_DHEAD + 1e-6]
        for (_mn, dk9, inset) in dock_m:
            if dk9 is None or not inset:
                continue
            (j0, j1) = dk9
            yb = D - 0.6              # recess back plane
            shell.tag = 'doors'
            # cheeks split at the inner-skin line in interior mode (the
            # clipped floor/ceiling corners land mid-edge otherwise).
            ch_y = [yb, D - wt, D] if interior_on else [yb, D]
            for (cx2, sgn) in ((j0, 1.0), (j1, -1.0)):
                for ri in range(len(dk_rows) - 1):
                    r0, r1 = dk_rows[ri], dk_rows[ri + 1]
                    for yi in range(len(ch_y) - 1):
                        ya7, yb7 = ch_y[yi], ch_y[yi + 1]
                        pts = [(cx2, ya7, r0), (cx2, yb7, r0),
                               (cx2, yb7, r1), (cx2, ya7, r1)]
                        if sgn < 0:
                            pts.reverse()
                        shell.quad(*pts, M_STUCCO)
                    if interior_on:
                        inx = j0 - wt if sgn > 0 else j1 + wt
                        pts = [(inx, yb - wt, r0), (inx, D - wt, r0),
                               (inx, D - wt, r1), (inx, yb - wt, r1)]
                        if sgn > 0:
                            pts.reverse()
                        shell.quad(*pts, M_STUCCO)
            # recessed back wall carrying the dock door -- u_lines include
            # every foreign grid line crossing the span (the rear wall's
            # void-cell verts must weld into ring/ceiling/floor edges).
            lxl3 = [v for v in xl if j0 - 1e-9 <= v <= j1 + 1e-9]
            _wd = _Wall(shell, (0, yb, 0), (1, 0, 0), lxl3, dk_rows,
                        (0, 1, 0), M_STUCCO, thickness=thick,
                        inner_zmax=iz_max)
            _wd.fill(lambda u0, zc0: 'window_dock', frame=0.06,
                     mat_frame=M_TRIM)
            if interior_on:
                for (fx0, fx1) in ((j0 - wt, j0), (j1, j1 + wt)):
                    for ri in range(len(dk_rows) - 1):
                        r0, r1 = dk_rows[ri], dk_rows[ri + 1]
                        shell.quad((fx0, yb - wt, r0), (fx1, yb - wt, r0),
                                   (fx1, yb - wt, r1), (fx0, yb - wt, r1),
                                   M_STUCCO)
            # well ceiling (faces down) + floor (faces up), x-split at the
            # same lines; interior mode clips both at the inner-skin line
            # (3-faces-per-edge lesson).
            y1c = (D - wt) if interior_on else D
            for iu3 in range(len(lxl3) - 1):
                xa7, xb7 = lxl3[iu3], lxl3[iu3 + 1]
                shell.quad((xa7, yb, Z_DHEAD), (xa7, y1c, Z_DHEAD),
                           (xb7, y1c, Z_DHEAD), (xb7, yb, Z_DHEAD),
                           M_STUCCO)
                shell.quad((xa7, yb, Z_DSILL), (xb7, yb, Z_DSILL),
                           (xb7, y1c, Z_DSILL), (xa7, y1c, Z_DSILL),
                           M_CONCRETE)

    def plain(u0, zc0):
        del u0, zc0
        return 'wall'

    shell.tag = 'facade_side'
    has_west_arm = any(xa['ox1'] <= 0.0 for xa in xarms)
    if layout == 'angled':
        # the SLANT wall: a plain planar _Wall whose u runs along the
        # diagonal; u-lines at every yl row crossing so its top edge
        # welds the custom roof's cap trapezoids.
        L_ang = (wx * wx + D * D) ** 0.5
        _wsl = _Wall(shell, (wx, 0, 0), (-wx / L_ang, D / L_ang, 0),
                     [y2 * L_ang / D for y2 in yl if y2 >= -1e-9], zl,
                     (-D / L_ang, -wx / L_ang, 0), M_STUCCO,
                     thickness=thick, inner_zmax=iz_max)
        _wsl.inner_u0, _wsl.inner_u1 = 0.6, L_ang - 0.6
        _wsl.fill(plain_wall)
    elif not has_west_arm:
        # west side x=0 (faces -x), y 0..D.
        def west_cls(u0, zc0):
            if cdoor_on and u0 < c1d - 1e-6 and zc0 < Z_SF - 1e-6:
                return 'void'
            return 'wall'

        _ws = _Wall(shell, (0, 0, 0), (0, 1, 0),
                    [v for v in yl if v >= -1e-9], zl, (-1, 0, 0), M_STUCCO,
                    thickness=thick, inner_zmax=iz_max)
        _ws.inner_u0, _ws.inner_u1 = wt, D - wt
        _ws.fill(west_cls)
    if cdoor_on:
        # the mitred storefront across the corner: a planar 45-degree
        # _Wall carrying a glazed door with a tall transom.
        L2 = c1d * (2.0 ** 0.5)
        ud0, ud1 = L2 / 2.0 - 0.475, L2 / 2.0 + 0.475
        shell.tag = 'storefront'

        def cham_cls(u0, zc0):
            if ud0 - 1e-6 <= u0 < ud1 - 1e-6:
                if abs(zc0) < 1e-6:
                    return 'doorL'
                if zc0 < Z_SRV - 1e-6:
                    return 'doorU'
                return 'window_transom'
            if zc0 < Z_BLK - 1e-6:
                return 'wall'
            return 'window'

        _wch = _Wall(shell, (c1d, 0, 0),
                     (-c1d / L2, c1d / L2, 0),
                     [0.0, ud0, ud1, L2],
                     [v for v in zl if v <= Z_SF + 1e-9],
                     (-c1d / L2, -c1d / L2, 0), M_STUCCO,
                     thickness=thick, inner_zmax=iz_max)
        _wch.inner_u0, _wch.inner_u1 = 0.25, L2 - 0.25
        _wch.fill(cham_cls, frame=0.06, mat_frame=M_TRIM, mat_pane=M_GLASS,
                  leaf=leaf_glass)
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
            if docks_on:
                for (_mn, dk9, inset) in dock_w:
                    if dk9 is None:
                        continue
                    (j0, j1) = dk9
                    jy0, jy1 = j0 - Wy, j1 - Wy
                    in_row = Z_DSILL - 1e-6 <= zc0 < Z_DHEAD - 1e-6
                    if jy0 - 1e-6 <= u0 < jy1 - 1e-6 and in_row:
                        # wing docks are always flush (the east wall's
                        # inset assembly is not worth its own mirror).
                        return 'window_dock'
            return 'wall'

        _we = _Wall(shell, (We, 0, 0), (0, 1, 0), yl, zl, (1, 0, 0),
                    M_STUCCO, thickness=thick, inner_zmax=iz_max)
        _we.inner_u0, _we.inner_u1 = -Wy + wt, D - wt
        _we.fill(rear_classify_w, frame=0.06, mat_frame=M_TRIM,
                 leaf=leaf_slab)
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
        _wl2.fill(_lg_cls, frame=0.06, mat_frame=M_TRIM, mat_pane=M_GLASS,
                  leaf=leaf_glass)
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
    # raised fronts MEETING at a building corner join with a mitred
    # corner COLUMN (both end faces suppressed): four faces piled on the
    # corner edge otherwise. Computed here because in_peak must also skip
    # the cap cell under the fill column.
    join_e = (corner and peaks_m and abs(peaks_m[-1][1] - Wm) < 1e-6 and
              any(abs(pk4[1] - Wy) < 1e-6 for pk4 in peaks_w))
    join_w = (peaks_m and abs(peaks_m[0][0] - wx) < 1e-6 and
              any(abs(pk4[1] - Wy) < 1e-6
                  for xa2 in xarms if xa2['ox1'] <= 0.0
                  for pk4 in xa2['peaks']))

    def in_peak(cx, cy):
        if wrap_w is not None and cx < wx + t and 0.0 < cy < wrap_ret:
            return True
        if wrap_e is not None and cx > Wm - t and 0.0 < cy < wrap_ret:
            return True
        if join_e and Wm < cx < Wm + t and 0.0 < cy < t:
            return True
        if join_w and -t < cx < 0.0 and 0.0 < cy < t:
            return True
        for (p0k, p1k, _xc, _z, _aw) in peaks_m:
            if p0k < cx < p1k and 0.0 < cy < t:
                return True
        for (p0k, p1k, _xc, _z, _aw) in peaks_w:
            if Wm < cx < Wm + t and -Wy + p0k < cy < -Wy + p1k:
                return True
        for xa in xarms:
            band = ((xa['fx'] - t, xa['fx']) if xa['face'] == 'e'
                    else (xa['fx'], xa['fx'] + t))
            for (p0k, p1k, _xc, _z, _aw) in xa['peaks']:
                if band[0] < cx < band[1] and \
                        -Wy + p0k < cy < -Wy + p1k:
                    return True
        return False

    _roof_cells(shell, xl, yl, inside, z_roof, z_par, t, in_peak=in_peak)
    if layout == 'angled':
        # the slant-region roof is a fully DECOUPLED island (2 mm inset
        # all round, boundary edges): no shared verts with the rectilinear
        # grid or the walls, so it needs NO internal grid at all -- four
        # rows (front band / body / rear band), each a cap trapezoid plus
        # a roof trapezoid and a slanted drop, every face a planar quad.
        # (Seven rounds of welded-seam attempts all ended in line/bound
        # tangencies only triangles could close; the island form is the
        # sanctioned abutting discipline instead.)
        L_ang = (wx * wx + D * D) ** 0.5
        dxt = t * L_ang / D
        ei2 = ei2_a

        def xw2(y2):
            return wx * (1.0 - y2 / D) + ei2

        e_i = e_ang - ei2
        yrows = [ei2, t, D - t, D - ei2]
        shell.tag = 'parapet'
        for ri4 in range(3):
            y0r, y1r = yrows[ri4], yrows[ri4 + 1]
            w0, w1 = xw2(y0r), xw2(y1r)
            c0r, c1r = w0 + dxt, w1 + dxt
            band = ri4 != 1
            shell.quad((w0, y0r, z_par), (c0r, y0r, z_par),
                       (c1r, y1r, z_par), (w1, y1r, z_par), M_STUCCO)
            if band:
                shell.quad((c0r, y0r, z_par), (e_i, y0r, z_par),
                           (e_i, y1r, z_par), (c1r, y1r, z_par), M_STUCCO)
            else:
                shell.tag = 'roof'
                shell.quad((c0r, y0r, z_roof), (e_i, y0r, z_roof),
                           (e_i, y1r, z_roof), (c1r, y1r, z_roof),
                           M_CONCRETE)
                shell.tag = 'parapet'
                shell.quad((c0r, y0r, z_roof), (c1r, y1r, z_roof),
                           (c1r, y1r, z_par), (c0r, y0r, z_par), M_STUCCO)
        for yb2, north in ((t, True), (D - t, False)):
            cxb = xw2(yb2) + dxt
            pts = [(cxb, yb2, z_roof), (e_i, yb2, z_roof),
                   (e_i, yb2, z_par), (cxb, yb2, z_par)]
            if north:
                pts.reverse()         # face toward the roof side
            shell.quad(*pts, M_STUCCO)
    # welded peaked parapets over their bays (front face continues the
    # wall plane; back face continues the cap inner drop).
    shell.tag = 'parapet'
    starts_m = {round(p0k, 6) for (p0k, _p1, _xc, _z, _aw) in peaks_m}
    ends_m = {round(p1k, 6) for (_p0, p1k, _xc, _z, _aw) in peaks_m}

    def corner_col(cx0, cx1, cy0, cy1, zhi5, west_face):
        q5 = shell.quad
        q5((cx0, cy0, zhi5), (cx1, cy0, zhi5), (cx1, cy1, zhi5),
           (cx0, cy1, zhi5), M_STUCCO)                       # top
        q5((cx0, cy1, zhi5), (cx1, cy1, zhi5), (cx1, cy1, z_par),
           (cx0, cy1, z_par), M_STUCCO)                      # north +y
        if west_face:
            q5((cx0, cy0, z_par), (cx0, cy0, zhi5), (cx0, cy1, zhi5),
               (cx0, cy1, z_par), M_STUCCO)                  # west -x
        else:
            q5((cx1, cy0, z_par), (cx1, cy1, z_par), (cx1, cy1, zhi5),
               (cx1, cy0, zhi5), M_STUCCO)                   # east +x

    if join_e:
        pk5 = peaks_m[-1]
        zj = pk5[3] if pk5[4] > 1.0 else z_par + 0.12
        corner_col(Wm, Wm + t, 0.0, t, zj, west_face=False)
    if join_w:
        pk5 = peaks_m[0]
        zj = pk5[3] if pk5[4] > 1.0 else z_par + 0.12
        corner_col(-t, 0.0, 0.0, t, zj, west_face=True)
    for (p0k, p1k, xck, zpk, awk) in peaks_m:
        # adjacent raised fronts MERGE at their shared bay line: both end
        # faces there would be coincident opposite-winding quads.
        e_l = round(p0k, 6) not in ends_m
        e_r = round(p1k, 6) not in starts_m
        if join_w and abs(p0k - wx) < 1e-6:
            e_l = False
        if join_e and abs(p1k - Wm) < 1e-6:
            e_r = False
        if awk > 1.0:
            wl = wrap_w is not None and wrap_w[0] == p0k
            wr = wrap_e is not None and wrap_e[1] == p1k
            _flat_front(shell, xl, p0k, p1k, z_par, zpk, t,
                        lambda u, d2, z: (u, d2, z),
                        end_l=e_l and not wl, end_r=e_r and not wr,
                        back_lo=(p0k + t) if wl else None,
                        back_hi=(p1k - t) if wr else None)
            if wl:
                # corner WRAP: the raised band returns along the west
                # side (u = -y for the -x-facing plane; back/cap clip at
                # the mitre so the front band's frame owns the corner).
                _flat_front(shell, sorted(-v for v in yl
                                          if -1e-9 <= v <= wrap_ret + 1e-9),
                            -wrap_ret, 0.0, z_par, zpk, t,
                            lambda u, d2, z: (wx + d2, -u, z),
                            end_l=True, end_r=False,
                            back_hi=-t, cap_full=False)
            if wr:
                _flat_front(shell, [v for v in yl
                                    if -1e-9 <= v <= wrap_ret + 1e-9],
                            0.0, wrap_ret, z_par, zpk, t,
                            lambda u, d2, z: (Wm - d2, u, z),
                            end_l=False, end_r=True,
                            back_lo=t, cap_full=False)
        else:
            _peak_wall(shell, xl, p0k, p1k, xck, awk, z_par, zpk, t,
                       lambda u, d2, z: (u, d2, z),
                       stub_l=e_l, stub_r=e_r)
    for xa in xarms:
        fx = xa['fx']
        pk = xa['peaks']
        if xa['face'] == 'e':
            st2 = {round(p0k, 6) for (p0k, _p1, _x, _z, _aw) in pk}
            en2 = {round(p1k, 6) for (_p0, p1k, _x, _z, _aw) in pk}
            for (p0k, p1k, xck, zpk, awk) in pk:
                em6 = (lambda f6: lambda u, d2, z:
                       (f6 - d2, -Wy + u, z))(fx)
                a_er = round(p1k, 6) not in st2
                if join_w and abs(p1k - Wy) < 1e-6 and xa['ox1'] <= 0.0:
                    a_er = False      # corner join owns this end
                if awk > 1.0:
                    _flat_front(shell, [v + Wy for v in yl], p0k, p1k,
                                z_par, zpk, t, em6,
                                end_l=round(p0k, 6) not in en2,
                                end_r=a_er)
                else:
                    _peak_wall(shell, [v + Wy for v in yl], p0k, p1k, xck,
                               awk, z_par, zpk, t, em6,
                               stub_l=round(p0k, 6) not in en2,
                               stub_r=a_er)
        else:
            st2 = {round(Wy - p1k, 6) for (_p0, p1k, _x, _z, _aw) in pk}
            en2 = {round(Wy - p0k, 6) for (p0k, _p1, _x, _z, _aw) in pk}
            for (p0k, p1k, xck, zpk, awk) in pk:
                u0k, u1k = Wy - p1k, Wy - p0k
                em6 = (lambda f6: lambda u, d2, z: (f6 + d2, -u, z))(fx)
                if awk > 1.0:
                    _flat_front(shell, sorted(-v for v in yl), u0k, u1k,
                                z_par, zpk, t, em6,
                                end_l=round(u0k, 6) not in en2,
                                end_r=round(u1k, 6) not in st2)
                else:
                    _peak_wall(shell, sorted(-v for v in yl), u0k, u1k,
                               Wy - xck, awk, z_par, zpk, t, em6,
                               stub_l=round(u0k, 6) not in en2,
                               stub_r=round(u1k, 6) not in st2)
    starts_w = {round(Wy - p1k, 6) for (_p0, p1k, _xc, _z, _aw) in peaks_w}
    ends_w = {round(Wy - p0k, 6) for (p0k, _p1, _xc, _z, _aw) in peaks_w}
    for (p0k, p1k, xck, zpk, awk) in peaks_w:
        # u runs along -y (handedness for the -x-facing wing wall).
        u0k, u1k = Wy - p1k, Wy - p0k
        w_el = round(u0k, 6) not in ends_w
        if join_e and abs(p1k - Wy) < 1e-6:
            w_el = False              # corner join owns this end
        if awk > 1.0:
            _flat_front(shell, sorted(-v for v in yl), u0k, u1k,
                        z_par, zpk, t,
                        lambda u, d2, z: (Wm + d2, -u - 0.0, z),
                        end_l=w_el,
                        end_r=round(u1k, 6) not in starts_w)
        else:
            _peak_wall(shell, sorted(-v for v in yl), u0k, u1k,
                       Wy - xck, awk, z_par, zpk, t,
                       lambda u, d2, z: (Wm + d2, -u - 0.0, z),
                       stub_l=w_el,
                       stub_r=round(u1k, 6) not in starts_w)

    mats = [_material(nm) for nm in _MATS]
    body = shell.to_object("LA_MiniMall_Body", mats)

    # ---- canopy / balcony deck + supports ----------------------------------
    can = _Shell()
    can.tag = 'canopy'
    deck_top = Z_FAS if office else 3.35
    deck_lo = deck_top - 0.5 if office else Z_SF
    x_deck0 = -1.45 if office else 0.0
    if office and p.get("stair_style", 'straight') == 'switchback':
        x_deck0 = -2.55                # overhang under the whole switchback
    cut_spans = ([(a0, a1)] if anchor_on else []) + \
        [(xa['ox0'], xa['ox1']) for xa in xarms if xa['ox1'] > 0.0]

    def split_segs(seg0, seg1):
        segs, cur = [], seg0
        for (c0, c1) in sorted(cut_spans):
            if c1 < seg0 or c0 > seg1:
                continue
            if c0 - 0.002 > cur:
                segs.append((cur, c0 - 0.002))
            cur = c1 + 0.002
        if seg1 > cur:
            segs.append((cur, seg1))
        return segs

    # the projecting canopy is OPTIONAL (canopy_depth < 0.5 = none: the
    # funky no-canopy strip -- awnings become the cover); the office deck
    # implies it.
    has_canopy = office or cd >= 0.5
    west_blocked = any(xa['ox1'] <= 0.0 for xa in xarms)
    # the deck's western overhang (stair landing) only exists where the
    # west end is OPEN AIR -- in C/E layouts that space is the west court
    # arm's mass, and both the deck and the access stair ran through it.
    x_deck_w = 0.002 if west_blocked else (wx + x_deck0)
    # stair sits on the western overhang, which starts at wx (nonzero on
    # the angled layout -- forgetting it left the stair floating in the
    # cut-off corner, landing on nothing).
    stair_x = (cd + 0.7, cd + 1.9) if west_blocked \
        else (wx - 1.35, wx - 0.15)
    if p.get("stair_style", 'straight') == 'switchback':
        # rail gap = the EXIT flight's span only (it lands ON the wider
        # overhang; the rest of the deck edge stays guarded).
        stair_x = (cd + 0.7, cd + 3.0) if west_blocked \
            else (wx - 2.48, wx - 1.36)
    deck_segs = _seg_subtract(split_segs(x_deck_w, Wm), shop2_spans)
    if has_canopy:
        for (sx0, sx1) in deck_segs:
            _box(can, (sx0, -cd, deck_lo), (sx1, -0.002, deck_top),
                 M_CONCRETE)
        if corner:
            _box(can, (Wm - cd, -Wy, deck_lo), (Wm - 0.002, -cd - 0.002,
                 deck_top), M_CONCRETE)
        for xa in xarms:
            fx = xa['fx']
            if xa['face'] == 'e':
                _box(can, (fx + 0.002, -Wy, deck_lo),
                     (fx + cd, -cd - 0.002, deck_top), M_CONCRETE)
            else:
                _box(can, (fx - cd, -Wy, deck_lo),
                     (fx - 0.002, -cd - 0.002, deck_top), M_CONCRETE)
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
                # picket rail wrap: ONE mitred U path (legs + front run
                # merged at the corners -- no overshooting butt joints).
                can.tag = 'loggia'
                el2.emit_railing_path(
                    can, [(p0 + 0.045, -cd + 0.11),
                          (p0 + 0.045, -cd - 0.895),
                          (p1 - 0.045, -cd - 0.895),
                          (p1 - 0.045, -cd + 0.11)],
                    z0=deck_top + 0.002, height=0.92, post_every=1.4)
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
        # CONTINUOUS deck railing: one mitred perimeter path per deck
        # segment (side return -> front run -> side return), split only at
        # the stair and at projecting balconies -- and those split ends
        # reach PAST the stair rails / wrap legs so the runs physically
        # JOIN (interpenetrate) instead of stopping short in the air.
        y_front = -cd + 0.075
        y_wall = -0.10
        gaps_all = []
        g0s, g1s = stair_x[0] + 0.06, stair_x[1] - 0.06   # crosses the
        gaps_all.append((g0s, g1s))                       # stair rails
        for i, (_b0, _b1, o0, o1) in enumerate(bays_m):
            if bal_modes.get(i) == 'projecting':
                gaps_all.append((o0 + 0.44, o1 - 0.44))   # crosses the wrap
        gaps_all.sort()
        for (s0, s1) in deck_segs:
            xi0, xi1 = s0 + 0.075, s1 - 0.075
            if xi1 - xi0 < 0.3:
                continue
            paths = []
            # the western overhang is SEPARATED from the building face --
            # wrap its BACK edge too, ending buried inside the west wall
            # corner (crosses y=0 into the wall solid: a real join, not a
            # free end floating by the corner).
            jut = (office and not west_blocked
                   and abs(s0 - x_deck_w) < 0.05 and wx - xi0 > 0.35)
            if jut:
                bx9 = min(wx + 0.13, xi1)
                cur_path = [(bx9, 0.06), (bx9, y_wall),
                            (xi0, y_wall), (xi0, y_front)]
            else:
                cur_path = [(xi0, y_wall), (xi0, y_front)]
            cur_x = xi0
            for (g0, g1) in gaps_all:
                if g1 < xi0 or g0 > xi1:
                    continue
                g0c, g1c = max(g0, xi0), min(g1, xi1)
                if g0c - cur_x > 0.25:
                    cur_path.append((g0c, y_front))
                    paths.append(cur_path)
                elif len(cur_path) >= 2 and \
                        abs(cur_path[0][1] - y_front) > 1e-6:
                    paths.append(cur_path)         # short: keep the return
                cur_x = max(cur_x, g1c)
                cur_path = [(cur_x, y_front)]
            if xi1 - cur_x > 0.25:
                cur_path.append((xi1, y_front))
                cur_path.append((xi1, y_wall))
                paths.append(cur_path)
            else:
                paths.append([(xi1, y_front), (xi1, y_wall)])
            for pth in paths:
                if len(pth) < 2:
                    continue
                span9 = abs(pth[-1][0] - pth[0][0]) + abs(pth[-1][1] -
                                                          pth[0][1])
                if span9 < 0.25:
                    continue
                el2.emit_railing_path(can, pth, z0=deck_top + 0.002,
                                      height=0.92, post_every=1.8)
        if corner:
            el2.emit_railing(can, -Wy + 0.05, -cd - 0.10, axis='y',
                             cross=Wm - cd + 0.075, z0=deck_top + 0.002,
                             height=0.92, post_every=1.8)
    if not has_canopy:
        pass                          # no canopy: no piers, no posts
    elif p["arcade"]:
        can.tag = 'canopy'
        _box(can, (0.002, -cd, deck_lo - 0.45), (Wm - 0.002, -cd + 0.12,
             deck_lo + 0.02), M_STUCCO)   # fascia embeds 20 mm into the
        # slab; 2 mm end insets keep its edges off the slab's end planes
        can.tag = 'columns'
        for (b0, _b1, _o0, _o1) in bays_m[1:] + [(Wm, 0, 0, 0)] + [(0,) * 4]:
            x = min(max(b0, 0.2), Wm - 0.2)
            if any(c0 - 0.3 < x < c1 + 0.3 for (c0, c1) in cut_spans):
                continue
            _box(can, (x - 0.18, -cd + 0.10, 0.0),
                 (x + 0.18, -cd + 0.46, deck_lo + 0.02), M_STUCCO)
    else:
        can.tag = 'columns'
        posts_x = [b0 for (b0, _b1, _o0, _o1) in bays_m[1:]] + \
            [wx + 0.25, Wm - 0.25]
        for x in posts_x:
            if any(c0 - 0.3 < x < c1 + 0.3 for (c0, c1) in cut_spans):
                continue
            _box(can, (x - 0.05, -cd + 0.12, 0.0),
                 (x + 0.05, -cd + 0.22, deck_lo + 0.02), M_METAL)
        if corner:
            posts_y = [b0 - Wy for (b0, _b1, _o0, _o1) in bays_w[1:]] + \
                      [-Wy + 0.25, -cd - 0.35]
            for y in posts_y:
                _box(can, (Wm - cd + 0.12, y - 0.05, 0.0),
                     (Wm - cd + 0.22, y + 0.05, deck_lo + 0.02), M_METAL)
        for xa in xarms:
            fx = xa['fx']
            px2 = (fx + cd - 0.22) if xa['face'] == 'e' else (fx - cd + 0.12)
            posts_y = [b0 - Wy for (b0, _b1, _o0, _o1) in xa['bays'][1:]] + \
                      [-Wy + 0.25, -cd - 0.35]
            for y in posts_y:
                _box(can, (px2, y - 0.05, 0.0),
                     (px2 + 0.10, y + 0.05, deck_lo + 0.02), M_METAL)
    if cdoor_on:
        # abutting-island soffit over the corner notch at storefront-head
        # level: a triangle quadified by centroid subdivision (3 quads).
        can.tag = 'canopy'
        A3 = (c1d - 0.004, 0.002)
        B3 = (0.002, c1d - 0.004)
        C3 = (0.002, 0.002)
        G3 = ((A3[0] + B3[0] + C3[0]) / 3.0,
              (A3[1] + B3[1] + C3[1]) / 3.0)
        mAB = ((A3[0] + B3[0]) / 2.0, (A3[1] + B3[1]) / 2.0)
        mBC = ((B3[0] + C3[0]) / 2.0, (B3[1] + C3[1]) / 2.0)
        mCA = ((C3[0] + A3[0]) / 2.0, (C3[1] + A3[1]) / 2.0)

        def zq3(pt):
            return (pt[0], pt[1], Z_SF)

        can.quad(zq3(A3), zq3(mCA), zq3(G3), zq3(mAB), M_STUCCO)
        can.quad(zq3(mAB), zq3(G3), zq3(mBC), zq3(B3), M_STUCCO)
        can.quad(zq3(G3), zq3(mCA), zq3(C3), zq3(mBC), M_STUCCO)
    # fabric awnings (seeded per open/barred bay) + roof bands.
    def emit_front2(u, pd, z):
        return (u, -0.002 - pd, z)

    def emit_wing2(u, pd, z):
        return (Wm - 0.002 - pd, -u, z)

    wall_sets = [(emit_front2, bays_m, fates_m, False)]
    if corner:
        wall_sets.append((emit_wing2, bays_w, fates_w, True))
    for xa in xarms:
        fx2 = xa['fx']
        if xa['face'] == 'e':
            wall_sets.append((
                (lambda fx3: lambda u, pd, z: (fx3 + 0.002 + pd,
                                               -Wy + u, z))(fx2),
                xa['bays'], xa['fates'], False))
        else:
            wall_sets.append((
                (lambda fx3: lambda u, pd, z: (fx3 - 0.002 - pd,
                                               -u, z))(fx2),
                xa['bays'], xa['fates'], True))
    # awnings are the ALTERNATIVE cover: never under the balcony deck
    # (office) or a projecting canopy -- only no-canopy strips get them.
    if not office and not has_canopy:
        for (em2, bys2, fts2, mir2) in wall_sets:
            for i2b, (_b0, _b1, o0b, o1b) in enumerate(bys2):
                if fts2[i2b] not in ('open', 'barred'):
                    continue
                if em2 is emit_front2 and anchor_on and \
                        a0 - 1e-6 < o0b < a1 + 1e-6:
                    continue
                if rng.random() >= p["awning_fraction"]:
                    continue
                kind2 = 'barrel' if rng.random() < 0.5 else 'flat'
                dp2 = 0.8 + rng.random() * 0.6
                ua2, ub2 = (Wy - o1b, Wy - o0b) if mir2 else (o0b, o1b)
                _awning(can, em2, ua2 + 0.08, ub2 - 0.08, kind2, dp2)
    if p["roof_band"] != 'none':
        # pd runs INWARD (behind the wall edge, over the roof plane).
        for (s0b, s1b) in split_segs(wx + 0.05, Wm - 0.05):
            _roof_band(can, lambda u, pd, z: (u, pd, z), s0b, s1b,
                       p["roof_band"], z_par)
        if corner:
            _roof_band(can, lambda u, pd, z: (Wm + pd, -u, z), 0.05,
                       Wy - 0.05, p["roof_band"], z_par)
        for xa in xarms:
            fx4 = xa['fx']
            if xa['face'] == 'e':
                _roof_band(can, (lambda f5: lambda u, pd, z:
                                 (f5 - pd, -Wy + u, z))(fx4),
                           0.05, Wy - 0.05, p["roof_band"], z_par)
            else:
                _roof_band(can, (lambda f5: lambda u, pd, z:
                                 (f5 + pd, -u, z))(fx4),
                           0.05, Wy - 0.05, p["roof_band"], z_par)
    canopy_ob = can.to_object("LA_MiniMall_Canopy", mats)

    # ---- office-strip access stair (straight, footed stringers) ------------
    stair_ob = None
    if office:
        st_style = p.get("stair_style", 'straight')
        rise = Z_FAS
        if st_style == 'straight':
            stair = _Shell()
            run = 0.26 * max(3, int(round(rise / 0.185)))
            _flight_straight(stair, stair_x[0], stair_x[1], -cd - run, -cd,
                             0.0, rise, 0.0, 0.16)
            # B1.3 (rpg-ro7o): welded pitched handrails on both stringers +
            # a level return over the deck landing.
            _flight_railing(stair, stair_x[0], stair_x[1], -cd - run, -cd,
                            0.0, rise, 0.16)
            stair_ob = stair.to_object("LA_MiniMall_Stair", mats)
        elif st_style in ('parapet', 'curb'):
            n9 = max(3, int(round(rise / 0.185)))
            run9 = 0.27 * n9
            for ob9 in el2.build_stucco_stair(
                    dict(width=1.1, height=rise, cheek=st_style,
                         rail_height=0.9), rng):
                ob9.name = "LA_MiniMall_Stair"
                ob9.location = (stair_x[0], -cd - run9, 0.0)
                sf_extra_obs.append(ob9)
        else:                                       # switchback
            w9, gap9 = 1.0, 0.06
            n9 = max(6, int(round(rise / 0.185)))
            n1_9 = (n9 + 1) // 2
            n2_9 = n9 - n1_9
            run1_9 = 0.27 * n1_9
            ytop9 = run1_9 + 0.02 - 0.27 * n2_9
            for ob9 in el2.build_switchback_stair(
                    dict(width=w9, height=rise, rail_height=0.95), rng):
                ob9.name = "LA_MiniMall_Stair"
                # rotate 180 so the return flight tops out FACING the deck.
                ob9.rotation_euler = (0.0, 0.0, 3.14159265)
                ob9.location = (stair_x[0] + 2 * w9 + gap9 + 0.05,
                                -cd + ytop9 - 0.02, 0.0)
                sf_extra_obs.append(ob9)

    # ---- walkway + parking lot ---------------------------------------------
    lot = _Shell()
    lot.tag = 'walkway'
    walk_segs = split_segs(x_deck_w if office else wx, Wm)
    for (wx0s, wx1s) in walk_segs:
        _box(lot, (wx0s, -cd - 0.45, 0.0), (wx1s, -0.002, 0.14), M_CONCRETE)
    if anchor_on:
        apx1 = a1 - 0.002
        if corner:
            # clear of the wing walkway strip (an east-end anchor's apron
            # plane coincided with it -- coplanar T-junction soup).
            apx1 = min(apx1, Wm - cd - 0.456)   # clear of BOTH the wing
            # strip plane and the lot slab's east plane
        _box(lot, (a0 + 0.002, -cd - 0.45, 0.0),
             (apx1, -1.602, 0.14), M_CONCRETE)         # anchor entry apron
    if corner:
        _box(lot, (Wm - cd - 0.45, -Wy, 0.012),
             (Wm - 0.002, -cd - 0.452, 0.14), M_CONCRETE)
    for xa in xarms:
        fx = xa['fx']
        if xa['face'] == 'e':
            _box(lot, (fx + 0.002, -Wy, 0.012),
                 (fx + cd + 0.45, -cd - 0.452, 0.14), M_CONCRETE)
        else:
            _box(lot, (fx - cd - 0.45, -Wy, 0.012),
                 (fx - 0.002, -cd - 0.452, 0.14), M_CONCRETE)
    # ---- parking: 0-3 rows, perpendicular / angled / parallel / minimal.
    # STRIPES ARE FACES OF THE LOT MESH (thin quad islands 2 mm above the
    # slab top, vertex group 'lot_lines', UV-mapped like everything else)
    # -- never separate objects.
    rows = p["parking_rows"]
    style = p["parking_style"]
    y_lot0 = -cd - 0.452
    y_lot1 = y_lot0
    zs2 = 0.052                       # stripe plane (slab top + 2 mm)

    def stripe(xa, ya, xb, yb, shear=0.0):
        # 0.10 m painted line from (xa, ya) to (xb, yb) footprint; shear
        # skews the far edge in +x (angled stalls).
        lot.tag = 'lot_lines'
        lot.quad((xa, ya, zs2), (xb, ya, zs2),
                 (xb + shear, yb, zs2), (xa + shear, yb, zs2), M_TRIM)

    def stall_row(hy, x0r, x1r, shear, depth):
        # the SLAB must contain every painted stripe: first/last stall
        # positions are solved from each stripe's full sheared footprint.
        pitch = 2.7 if abs(shear) < 0.1 else 2.9
        m0, m1 = x0r + 0.35, x1r - 0.35
        sx_first = m0 + 0.05 - min(shear, 0.0)
        sx_last = m1 - 0.05 - max(shear, 0.0)
        n2 = int((sx_last - sx_first) / pitch)
        if n2 < 1:
            return
        for si in range(n2 + 1):
            sx = sx_first + si * pitch
            stripe(sx - 0.05, hy - 0.3, sx + 0.05, hy - depth, shear)
        if abs(shear) < 0.1:
            lot.tag = 'lot'
            for si in range(n2):
                sx = sx_first + si * pitch + pitch / 2.0
                _box(lot, (sx - 0.85, hy - 0.75, 0.040),
                     (sx + 0.85, hy - 0.60, 0.16), M_CONCRETE)  # wheel stop,
                #      seated 10 mm INTO the slab (a 1 mm hover z-fights)

    has_lot = rows > 0 or style in ('parallel', 'minimal')
    if has_lot:
        lot.tag = 'lot'
        lot_x1 = (Wm - cd - 0.452) if corner else Wm
        if style == 'minimal':
            # almost no parking: a short 4-stall pad by the entry.
            y_lot1 = y_lot0 - 10.5
            _box(lot, (0.0, y_lot1, 0.0), (min(14.0, lot_x1),
                 y_lot0 - 0.002, 0.05), M_CONCRETE)
            for si in range(5):
                sx = 0.6 + si * 2.7
                stripe(sx - 0.05, y_lot0 - 0.3, sx + 0.05, y_lot0 - 5.0)
        elif style == 'parallel':
            # parallel-only: one lane of curb stalls, tick lines between.
            y_lot1 = y_lot0 - 2.4 - 6.0
            _box(lot, (0.0, y_lot1, 0.0), (lot_x1, y_lot0 - 0.002, 0.05),
                 M_CONCRETE)
            n2 = int((lot_x1 - 1.0) / 6.5)
            for si in range(n2 + 1):
                sx = 0.5 + si * 6.5
                # ticks stop 20 mm short of the edge line (overlapping
                # coplanar faces put tick verts on its edges).
                stripe(sx - 0.05, y_lot0 - 0.2, sx + 0.05, y_lot0 - 2.28)
            stripe(0.5 - 0.05, y_lot0 - 2.3,
                   0.5 + n2 * 6.5 + 0.05, y_lot0 - 2.4)
        else:
            shear = {'perpendicular': 0.0, 'angled_60': 2.9,
                     'angled_45': 5.0}[style]
            depth = 5.0
            aisle = 6.5
            y_lot1 = y_lot0 - rows * depth - aisle * ((rows + 1) // 2)
            _box(lot, (0.0, y_lot1, 0.0), (lot_x1, y_lot0 - 0.002, 0.05),
                 M_CONCRETE)
            heads = [y_lot0 - 0.002]
            if rows >= 2:
                # facing pair across the first aisle.
                heads.append(y_lot0 - depth - aisle - depth + 0.002)
            if rows >= 3:
                heads.append(y_lot0 - 2 * depth - 2 * aisle + 0.002)
            for ri2, hy in enumerate(heads):
                sh = shear if ri2 % 2 == 0 else -shear
                stall_row(hy, 0.0, lot_x1, sh, depth)
    if docks_on:
        lot.tag = 'lot'
        _box(lot, (0.0, D + 0.002, 0.0), (We, D + 6.0, 0.05), M_CONCRETE)
        for (_mn, dk9, inset) in dock_m:
            if dk9 is None:
                continue
            (j0, j1) = dk9
            byf = (D - 0.6) if inset else D
            for bx in (j0 + 0.25, j1 - 0.45):
                _box(lot, (bx, byf + 0.002, Z_DSILL - 0.32),
                     (bx + 0.2, byf + 0.12, Z_DSILL - 0.06), M_METAL)
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
        asv = (axc - 2.0, axc - 1.1)
        zla = sorted({0.0, Z_BLK, Z_SRV, 3.0, zA_sf, zA_fas, zA_ceil,
                      zA_roof, zA_par} |
                     ({Z_DSILL, Z_DHEAD} if docks_on else set()))
        apk = ((a0 + a1) / 2.0 - 2.4, (a0 + a1) / 2.0 + 2.4)
        adk = (axc + 0.5, axc + 0.5 + 3.6)
        axl = sorted({ax0, ax1, ax0 + t, ax1 - t, aop0, aop1, ad0, ad1,
                      asv[0], asv[1], apk[0], apk[1],
                      axc - 0.09, axc + 0.09} |
                     ({adk[0], adk[1]} if docks_on else set()))
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
                    if zc0 < Z_DR - 1e-6:    # 2.6 m grocery double door
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
            if docks_on and adk[0] - 1e-6 <= u0 < adk[1] - 1e-6 and \
                    Z_DSILL - 1e-6 <= zc0 < Z_DHEAD - 1e-6:
                return 'window_dock'
            return 'wall'

        def a_plain(u0, zc0):
            del u0, zc0
            return 'wall'

        ash.tag = 'storefront'
        _wa = _Wall(ash, (0, AF, 0), (1, 0, 0), axl, zla, (0, -1, 0),
                    M_STUCCO, thickness=thick, inner_zmax=zA_ceil)
        _wa.inner_u0, _wa.inner_u1 = ax0 + wt, ax1 - wt
        _wa.fill(a_front, frame=0.06, mat_frame=M_TRIM, mat_pane=M_GLASS,
                 leaf=leaf_glass)
        ash.tag = 'facade_back'
        _wb = _Wall(ash, (0, D, 0), (1, 0, 0), axl, zla, (0, 1, 0),
                    M_STUCCO, thickness=thick, inner_zmax=zA_ceil)
        _wb.inner_u0, _wb.inner_u1 = ax0 + wt, ax1 - wt
        _wb.fill(a_rear, frame=0.06, mat_frame=M_TRIM, leaf=leaf_slab)
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
            e = -0.02      # embed the slab edges INTO the wall thickness
            ash.tag = 'slabs'
            _box(ash, (ax0 + wt + e, AF + wt + e, -0.004),
                 (ax1 - wt - e, D - wt - e, 0.12), M_CONCRETE)
            _box(ash, (ax0 + wt + e, AF + wt + e, zA_roof - 0.12),
                 (ax1 - wt - e, D - wt - e, zA_roof - 0.002), M_CONCRETE)
        # rooftop HVAC: groceries almost always carry a big RTU package.
        ash.tag = 'roof'
        hx0 = max(ax0 + 0.6, axc - 1.4)
        hy0 = D - 4.4
        _box(ash, (hx0 - 0.1, hy0 - 0.1, zA_roof + 0.002),
             (hx0 + 2.5, hy0 + 1.8, zA_roof + 0.16), M_CONCRETE)   # curb
        _box(ash, (hx0, hy0, zA_roof + 0.14),
             (hx0 + 2.4, hy0 + 1.7, zA_roof + 1.15), M_METAL)      # RTU
        _box(ash, (hx0 + 0.3, hy0 + 0.35, zA_roof + 1.13),
             (hx0 + 1.1, hy0 + 1.15, zA_roof + 1.34), M_METAL)     # fan hood
        _box(ash, (hx0 + 2.38, hy0 + 0.45, zA_roof + 0.30),
             (hx0 + min(3.1, (ax1 - hx0) - 0.4), hy0 + 1.25,
              zA_roof + 0.85), M_METAL)                            # duct run
        if docks_on:
            # raised loading platform against the rear + rubber bumpers.
            ash.tag = 'walkway'
            _box(ash, (adk[0] - 0.4, D + 0.002, 0.0),
                 (adk[1] + 0.4, D + 1.9, Z_DSILL - 0.02), M_CONCRETE)
            for bx in (adk[0] + 0.35, adk[1] - 0.55):
                _box(ash, (bx, D + 0.004, Z_DSILL - 0.32),
                     (bx + 0.2, D + 0.12, Z_DSILL - 0.06), M_METAL)
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

    blade_ok = not office and not has_canopy
    if p["sign_band"] and not dead:
        for i, (_b0, _b1, o0, o1) in enumerate(bays_m):
            _store_sign(sign, rng, emit_front, o0, o1, Z_SF + 0.05,
                        Z_FAS - 0.05, smats, dead, blade_ok=blade_ok)
        for i, (_b0, _b1, o0, o1) in enumerate(bays_w):
            _store_sign(sign, rng, emit_wing, Wy - o1, Wy - o0,
                        Z_SF + 0.05, Z_FAS - 0.05, smats, dead,
                        blade_ok=blade_ok)
    for xa in xarms:
        if not p["sign_band"] or dead:
            break
        fx = xa['fx']
        for i, (_b0, _b1, o0, o1) in enumerate(xa['bays']):
            if xa['face'] == 'e':
                _store_sign(sign, rng,
                            lambda u, pd, z: (fx + 0.002 + pd, -Wy + u, z),
                            o0, o1, Z_SF + 0.05, Z_FAS - 0.05, smats, dead,
                            blade_ok=blade_ok)
            else:
                _store_sign(sign, rng,
                            lambda u, pd, z: (fx - 0.002 - pd, -u, z),
                            Wy - o1, Wy - o0, Z_SF + 0.05, Z_FAS - 0.05,
                            smats, dead, blade_ok=blade_ok)
    if anchor_on and not dead:
        _store_sign(sign, rng,
                    lambda u, pd, z: (u, AF - 0.002 - pd, z),
                    a0 + 0.6, a1 - 0.6, zA_sf + 0.06, zA_fas - 0.06,
                    smats, dead, blade_ok=False)
    # security-bar grilles over every barred opening.
    sign.tag = 'story'
    grille_sets = [(emit_front, bays_m, doors_m, fates_m, bw_m, False)]
    if corner:
        grille_sets.append((emit_wing, bays_w, doors_w, fates_w, bw_w,
                            True))
    for xa in xarms:
        fx2 = xa['fx']
        if xa['face'] == 'e':
            em3 = (lambda fx3: lambda u, pd, z:
                   (fx3 + 0.002 + pd, -Wy + u, z))(fx2)
            grille_sets.append((em3, xa['bays'], xa['doors'], xa['fates'],
                                xa['bw'], False))
        else:
            em3 = (lambda fx3: lambda u, pd, z:
                   (fx3 - 0.002 - pd, -u, z))(fx2)
            grille_sets.append((em3, xa['bays'], xa['doors'], xa['fates'],
                                xa['bw'], True))
    for (em3, bys3, drs3, fts3, bws3, mir3) in grille_sets:
        for i3, (_b0, _b1, o0b, o1b) in enumerate(bys3):
            if fts3[i3] != 'barred':
                continue
            spans3 = [(drs3[i3][0], drs3[i3][1], 0.0, Z_SRV)]
            for (w0b, w1b) in bws3.get(i3, []):
                spans3.append((w0b, w1b, 0.9, Z_SRV))
            for (ja, jb, za3, zb3) in spans3:
                if mir3:
                    ja, jb = Wy - jb, Wy - ja
                _grille(sign, em3, ja, jb, za3, zb3)
    if p["roof_sign"]:
        rsx = bays_m[min(n - 1, max(0, n // 2))][0] + tw / 2.0
        for px in (rsx - 1.6, rsx + 1.5):
            _box(sign, (px, 1.0, z_roof + 0.002),
                 (px + 0.10, 1.10, z_roof + 2.15), M_METAL)
        _box(sign, (rsx - 1.65, 1.02, z_roof + 0.9),
             (rsx + 1.65, 1.08, z_roof + 2.05),
             M_METAL if dead else M_SIGN_B)
    if p["pole_sign"] and has_lot:
        # pole-sign panel ARRANGEMENTS, seeded (user-specified set):
        # 'ladder'   same-size panels flanking BOTH sides of the pole;
        # 'stack'    varying sizes, vertically aligned (single or twin
        #            pole) -- the classic directory;
        # 'pinwheel' one big frame subdivided CLOCKWISE into an unevenly
        #            sized panel grid (strip cut from top -> right ->
        #            bottom -> left in turn, last panel fills the core).
        # ONE PANEL PER SHOP (it is the mall directory); twin-pole panels
        # always span both poles. No lot = no pole sign.
        sign.tag = 'pole_sign'
        n_shops = (len(bays_m) + len(bays_w) +
                   sum(len(xa['bays']) for xa in xarms) +
                   (1 if anchor_on else 0))
        ph = p["pole_height"]
        # the pole (and its widest panel, half-width up to 1.65) must
        # CLEAR the store: west-arm canopy/walkway in C/E layouts, and
        # any rain cover / balcony / awning projection off the front.
        px = 1.6
        if any(xa['ox1'] <= 0.0 for xa in xarms):
            px = max(px, cd + 0.45 + 2.2)
        py = min(y_lot1 + 1.2, -cd - 2.6)
        # the pinwheel frame holds at most ~12 legible panels; a bigger
        # roster picks a linear style instead. ONE PANEL PER SHOP is the
        # rule, so a big directory that cannot fit on the requested pole
        # GROWS the pole rather than dropping panels below grade (the
        # mega-strip directory pole is a real LA silhouette anyway).
        style = rng.choice(['ladder', 'stack', 'pinwheel']
                           if n_shops <= 12 else ['ladder', 'stack'])
        if style == 'ladder':
            # lv_h floors at 0.34 (0.19 m panels): band = 62% of ph.
            ph = max(ph, ((n_shops + 1) // 2) * 0.34 / 0.62)
        elif style == 'stack':
            # 0.16 m strips + 0.08 gaps must fit the band below the
            # header (avail = 70% of ph minus the 1.55 header).
            ph = max(ph, (0.24 * n_shops + 1.55) / 0.70)
        pm = (lambda k: M_METAL) if dead else (lambda k: smats[k % 3])
        if style == 'ladder':
            _box(sign, (px - 0.14, py - 0.14, 0.0),
                 (px + 0.14, py + 0.14, ph), M_METAL)
            n_lv = (n_shops + 1) // 2
            lv_h = min(0.66, (ph * 0.62) / max(n_lv, 1))
            zc = ph - 0.35
            k = 0
            for _lv in range(n_lv):
                for sd in (-1.0, 1.0):
                    if k >= n_shops:
                        break
                    _box(sign, (px + sd * 0.18 + min(sd, 0) * 1.15,
                                py - 0.08, zc - lv_h + 0.10),
                         (px + sd * 0.18 + max(sd, 0) * 1.15,
                          py + 0.08, zc - 0.05), pm(k))
                    k += 1
                zc -= lv_h
        elif style == 'stack':
            twin = rng.random() < 0.5
            if twin:
                for tx in (px - 1.1, px + 1.0):
                    _box(sign, (tx, py - 0.12, 0.0),
                         (tx + 0.24, py + 0.12, ph), M_METAL)
                # panels must SPAN the poles (outer edges at +-1.24).
                hw_lo, hw_rand = 1.30, 0.35
            else:
                _box(sign, (px - 0.15, py - 0.15, 0.0),
                     (px + 0.15, py + 0.15, ph), M_METAL)
                hw_lo, hw_rand = 0.7, 0.6
            _box(sign, (px - 1.45, py - 0.14, ph - 1.35),
                 (px + 1.45, py + 0.14, ph - 0.2), pm(0))
            avail = (ph - 1.55) - ph * 0.30
            hh = max(0.16, min(0.40, avail / max(n_shops, 1) - 0.08))
            zc = ph - 1.55
            for k in range(n_shops):
                hw = hw_lo + rng.random() * hw_rand
                _box(sign, (px - hw, py - 0.08, zc - hh),
                     (px + hw, py + 0.08, zc - 0.04), pm(k + 1))
                zc -= hh + 0.08
        else:                         # pinwheel
            for tx in (px - 1.35, px + 1.11):
                _box(sign, (tx, py - 0.12, 0.0), (tx + 0.24, py + 0.12, ph),
                     M_METAL)
            gx0, gx1 = px - 1.45, px + 1.45
            gz0 = max(ph * 0.25, ph - 1.8 - 0.42 * n_shops)
            gz1 = ph - 0.15
            k = 0
            sides = ('top', 'right', 'bottom', 'left') * 4
            for side in sides:
                if k >= n_shops - 1 or (gx1 - gx0) < 0.5 or \
                        (gz1 - gz0) < 0.42:
                    break
                fr = 0.30 + rng.random() * 0.16
                if side == 'top':
                    cz = gz1 - (gz1 - gz0) * fr
                    _box(sign, (gx0, py - 0.09, cz + 0.02),
                         (gx1, py + 0.09, gz1), pm(k))
                    gz1 = cz
                elif side == 'right':
                    cx = gx1 - (gx1 - gx0) * fr
                    _box(sign, (cx + 0.02, py - 0.09, gz0),
                         (gx1, py + 0.09, gz1), pm(k))
                    gx1 = cx
                elif side == 'bottom':
                    cz = gz0 + (gz1 - gz0) * fr
                    _box(sign, (gx0, py - 0.09, gz0),
                         (gx1, py + 0.09, cz - 0.02), pm(k))
                    gz0 = cz
                else:
                    cx = gx0 + (gx1 - gx0) * fr
                    _box(sign, (gx0, py - 0.09, gz0),
                         (cx - 0.02, py + 0.09, gz1), pm(k))
                    gx0 = cx
                k += 1
            _box(sign, (gx0, py - 0.09, gz0), (gx1, py + 0.09, gz1),
                 pm(k))               # the core panel fills the remainder
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
    # no two-story shops / story options selected -> the shell is empty;
    # an empty mesh object breaks the scene exporter ("No mesh object").
    story_ob = story.to_object("LA_MiniMall_Story", mats) \
        if len(story.bm.faces) else None

    # ---- interior mode: slabs, demising walls, back corridor, hatch --------
    interior_obs = []
    if interior_on:
        e = 0.001
        slabs = _Shell()
        slabs.tag = 'slabs'
        _box(slabs, (wx + wt + e, wt + e, 0.0),
             (Wm - (0.0 if corner else wt) - e, D - wt - e, 0.12),
             M_CONCRETE)
        if corner:
            _box(slabs, (Wm + e, -Wy + wt + e, 0.0),
                 (We - wt - e, D - wt - e, 0.12), M_CONCRETE)
        _box(slabs, (wx + wt + e, wt + e, z_roof - 0.12),
             (Wm - (0.0 if corner else wt) - e, D - wt - e, z_roof - 0.002),
             M_CONCRETE)
        if corner:
            _box(slabs, (Wm + e, -Wy + wt + e, z_roof - 0.12),
                 (We - wt - e, D - wt - e, z_roof - 0.002), M_CONCRETE)
        cor_y = D - 1.6
        if office:
            # office floor slab with a STAIRWELL opening at the east end
            # of the corridor (the internal stair rises through it -- a
            # solid slab put the flight through the ceiling)
            sx0 = Wm - (0.0 if corner else wt) - e
            hole_x0 = sx0 - 4.85
            _box(slabs, (wx + wt + e, wt + e, Z_FAS - 0.30),
                 (hole_x0 - 0.001, D - wt - e, Z_FAS), M_CONCRETE)
            _box(slabs, (hole_x0 + 0.001, wt + e, Z_FAS - 0.30),
                 (sx0, cor_y + 0.05, Z_FAS), M_CONCRETE)
            if corner:
                # east wing office floor
                _box(slabs, (Wm + e, -Wy + wt + e, Z_FAS - 0.30),
                     (We - wt - e, D - wt - e, Z_FAS), M_CONCRETE)
        for xa in xarms:
            # mid arms: the floor JOINS the run slab at the mouth (the
            # old -0.05 stop left a 0.2 m corner gap strip in every slab)
            y1s = (D - wt) if xa['ox1'] <= 0.0 else (wt - 0.001)
            _box(slabs, (xa['ox0'] + wt + e, -Wy + wt + e, 0.0),
                 (xa['ox1'] - wt - e, y1s - e, 0.12), M_CONCRETE)
            _box(slabs, (xa['ox0'] + wt + e, -Wy + wt + e, z_roof - 0.12),
                 (xa['ox1'] - wt - e, y1s - e, z_roof - 0.002), M_CONCRETE)
            if office:
                # the arms are part of the two-storey mass: without their
                # office floor the storey windows hung over a void
                _box(slabs, (xa['ox0'] + wt + e, -Wy + wt + e,
                             Z_FAS - 0.30),
                     (xa['ox1'] - wt - e, y1s - e, Z_FAS), M_CONCRETE)
        interior_obs.append(slabs.to_object("LA_MiniMall_Slabs", mats))

        walls = _Shell(recalc=True)
        walls.tag = 'demising'
        zt2 = z_roof - 0.122
        zt_gnd = (Z_FAS - 0.302) if office else zt2   # stop at the office
        # floor slab -- running through it coplanar-overlapped the office
        # demising above.
        for (b0, _b1, _o0, _o1) in bays_m[1:]:
            _wall_solid(walls, 'y', b0 - 0.05, wt + 0.002, cor_y - 0.052,
                        0.12, zt_gnd, 0.10, None, M_STUCCO)
            if office:
                # floor-2 demising stops at the corridor line -- the
                # office HALLWAY connects the units (mirrors the ground
                # corridor; full-depth walls sealed each unit off).
                _wall_solid(walls, 'y', b0 - 0.05, wt + 0.002,
                            cor_y - 0.052, Z_FAS + 0.002, zt2, 0.10,
                            None, M_STUCCO)
        if corner:
            _wall_solid(walls, 'y', Wm + 0.05, wt + 0.002, D - wt - 0.002,
                        0.12, zt_gnd, 0.10, None, M_STUCCO)
            for (b0, _b1, _o0, _o1) in bays_w[1:]:
                _wall_solid(walls, 'x', b0 - Wy - 0.05, Wm + 0.152,
                            We - wt - 0.002, 0.12, zt_gnd, 0.10, None,
                            M_STUCCO)
        for xa in xarms:
            for (b0, _b1, _o0, _o1) in xa['bays'][1:]:
                _wall_solid(walls, 'x', b0 - Wy - 0.05, xa['ox0'] + wt +
                            0.002, xa['ox1'] - wt - 0.002, 0.12, zt_gnd,
                            0.10, None, M_STUCCO)
        # back corridor wall, one doored segment per tenant (both
        # storeys when there's an office floor).
        walls.tag = 'corridor'
        for i, (b0, b1, _o0, _o1) in enumerate(bays_m):
            a2 = b0 + (0.052 if i else wt + 0.002)
            b2 = b1 - (0.052 if i < n - 1 else wt + 0.002)
            dxc = (b0 + b1) / 2.0
            _wall_solid(walls, 'x', cor_y, a2, b2, 0.12, zt_gnd, 0.10,
                        (dxc - 0.45, dxc + 0.45, 0.12 + 2.05), M_STUCCO)
            if office:
                _wall_solid(walls, 'x', cor_y, a2, b2, Z_FAS + 0.002,
                            zt2, 0.10,
                            (dxc - 0.45, dxc + 0.45, Z_FAS + 0.002 + 2.05),
                            M_STUCCO)
        if office:
            # internal stair: ground corridor -> office corridor, rising
            # eastward inside the corridor through the stairwell opening
            walls.tag = 'steps'
            n_st = 14
            rise_i = (Z_FAS - 0.12) / n_st
            run_i = 0.27
            fx1 = Wm - (0.0 if corner else wt) - 0.202
            fx0 = fx1 - n_st * run_i
            sy0, sy1 = cor_y + 0.152, D - wt - 0.102
            for i9 in range(n_st):
                _box(walls, (fx0 + i9 * run_i + 0.001, sy0,
                             0.12 + i9 * rise_i),
                     (fx0 + (i9 + 1) * run_i - 0.001, sy1,
                      0.12 + (i9 + 1) * rise_i), M_CONCRETE)
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
           story_ob] + interior_obs + sf_extra_obs
    out = [ob for ob in out if ob is not None]
    for ob in out:
        ob["ferrum_lightmap_res"] = 0 if ob in (sign_ob, story_ob) else 128
    return out


SPEC = [
    params.MODE_PARAM,
    dict(name="tenants", type='INT', default=5, min=2, max=20),
    dict(name="storefront_detail", type='BOOL', default=True,
         desc="Storefront-bay dressing on open tenant bays"),
    dict(name="two_story_shops", type='FLOAT', default=0.25, min=0.0,
         max=0.6, desc="Fraction of open bays converted to two-story shops "
                       "(office strip only): real floor slab, demising "
                       "walls, internal stair; balcony run BREAKS there"),
    dict(name="storefront_style", type='ENUM', default='mixed',
         items=('mixed', 'checker', 'tile', 'stucco', 'panel'),
         desc="Bulkhead style (mixed = per-bay variety)"),
    dict(name="tenant_width", type='FLOAT', default=5.5, min=4.0, max=8.0,
         unit='LENGTH', desc="Bay width per tenant"),
    dict(name="depth", type='FLOAT', default=12.0, min=8.0, max=16.0,
         unit='LENGTH'),
    dict(name="canopy_depth", type='FLOAT', default=2.5, min=0.0, max=3.5,
         unit='LENGTH', desc="Below 0.5 = NO canopy (awnings become the "
              "storefront cover)"),
    dict(name="sign_band", type='BOOL', default=True,
         desc="Per-tenant mismatched sign panels on the fascia"),
    dict(name="parking_rows", type='INT', default=1, min=0, max=3,
         desc="Striped lot rows"),
    dict(name="parking_style", type='ENUM', default='perpendicular',
         items=('perpendicular', 'angled_60', 'angled_45', 'parallel',
                'minimal'),
         desc="Stall pattern; parallel = curb lane only, minimal = a "
              "4-stall pad (almost no parking)"),
    dict(name="pole_sign", type='BOOL', default=True),
    dict(name="pole_height", type='FLOAT', default=9.0, min=5.0, max=15.0,
         unit='LENGTH'),
    dict(name="shutters", type='FLOAT', default=0.3, min=0.0, max=1.0,
         desc="Fraction of front roll-up shutters down"),
    dict(name="bars", type='FLOAT', default=0.25, min=0.0, max=1.0,
         desc="Fraction of tenants with narrow barred openings instead "
              "of plate glass"),
    dict(name="awning_fraction", type='FLOAT', default=0.5, min=0.0,
         max=1.0, desc="Fraction of storefronts with a fabric awning "
              "(barrel or flat, seeded depth)"),
    dict(name="high_bulkhead", type='FLOAT', default=0.4, min=0.0, max=1.0,
         desc="Fraction of storefronts on a 0.9 m masonry knee wall "
              "(glazing and roll-ups stop there instead of grade)"),
    dict(name="door_ajar", type='FLOAT', default=0.15, min=0.0, max=1.0,
         desc="Seeded fraction of door leafs standing ajar (glass "
              "storefront pairs, rear man-doors) -- dead-mall tie-in"),
    dict(name="loading_docks", type='BOOL', default=True,
         desc="Heavy sectional dock doors on the rear at truck-sill "
              "height (some inset into recessed wells), platform at the "
              "anchor, bumpers + apron"),
    dict(name="peak_fraction", type='FLOAT', default=0.25, min=0.0, max=1.0,
         desc="Fraction of bays with a peaked parapet pediment"),
    dict(name="balconies", type='ENUM', default='run',
         items=('run', 'projecting', 'recessed', 'mixed'),
         desc="Office-storey balcony treatment per bay"),
    # -- monotony breakers --
    dict(name="anchor", type='BOOL', default=True,
         desc="One double-width anchor tenant: taller, proud of the run, "
              "prominent roof + signage (auto-off with office_strip)"),
    dict(name="layout", type='ENUM', default='bar',
         items=('bar', 'L', 'C', 'E', 'angled'),
         desc="Footprint: straight bar, L (east wing), C '[' (wings both "
              "ends), tall-E (wings both ends + a middle prong), angled "
              "(diagonal west wall on a boulevard-cut lot)"),
    dict(name="arcade", type='BOOL', default=False,
         desc="Stucco arcade piers + hanging fascia instead of posts"),
    dict(name="roof_sign", type='BOOL', default=False,
         desc="Rooftop frame sign instead of / beside the pole sign"),
    dict(name="roof_band", type='ENUM', default='none',
         items=('none', 'angled', 'curved'),
         desc="Forward-leaning or barrel-curved roof band over the "
              "walkway (suppresses parapet peaks)"),
    dict(name="corner_door", type='BOOL', default=False,
         desc="Mitred 45-degree corner doorway at the west street corner "
              "(bar/L layouts)"),
    dict(name="stair_style", type='ENUM', default='straight',
         items=('straight', 'switchback', 'parapet', 'curb'),
         desc="Office access stair: steel straight / steel switchback / "
              "stucco parapet / stucco curb"),
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
