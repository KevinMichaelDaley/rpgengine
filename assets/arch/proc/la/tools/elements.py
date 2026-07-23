"""Phase-0 reusable architectural ELEMENTS (kit of parts).

Small parametric generators the assemblies/typologies compose (see
ARCH_GEN_BUILD_ORDER.md). Each is a standalone registered tool AND an
importable build function so larger generators can call them directly.

Tickets: rpg-giun (wrought-iron railing kit), rpg-yhkd (straight-run stucco
stair + rail), rpg-bit5 (multi-pane steel-sash factory window), rpg-9s27
(Beaux-Arts modillion cornice).

Quality bar (rpg-2lyk): 100% quads, welded islands, no T-junctions, no
coincident planes (separate solids INTERPENETRATE, embedded >= 5 mm, never
sharing a face plane), even poly distribution (long members are welded tube
strips ringed at their natural stations -- pickets, mullions, modillion
bays -- never chains of butted boxes).

ASCII plans:

  railing (x-run)          stair (y-run, ascends +y)      window (xz plane)
   post picket... post       cheek/rail                     +--+--+--+ head
    ||  | | | |  ||           ____/|                        |##|##|##|
    ##==============##  top  /    ||  treads                +--+--+--+
    ||  | | | |  ||    rail /_____||_                       |##|~~|##|  ~~ = hopper
    ##==============##  bot                                 +--+--+--+ sill
                                                            mullion grid

  cornice profile (y toward viewer = projection, swept along x):
      __cap
     |  corona
     |__
      |mm|   <- modillion brackets under the soffit, even spacing
      _|
     _| bed steps (+ dentils)
     wall
"""
import math

from .. import params
from ..geom import (
    _MATS, _Shell, _box, _material, _sheared_box,
    M_CONCRETE, M_GLASS, M_METAL, M_SHUTTER, M_SIGN_A, M_SIGN_B, M_STUCCO,
    M_TRIM,
)


# ---------------------------------------------------------------------------
# shared helpers
# ---------------------------------------------------------------------------
def _bar(shell, stations, w, h, mat, tag=None, axis='x', center=0.0):
    """A welded rectangular-section TUBE along a polyline of (u, z_center)
    stations (u runs along @p axis; the section is w wide across the run,
    h tall, its across-axis centreline at @p center). One quad ring per
    station keeps poly distribution even and mitres pitch breaks by
    construction; only the two ends are capped -- ONE manifold island,
    never butted boxes."""
    if len(stations) < 2:
        return
    rings = []
    for (u, zc) in stations:
        c0, c1 = center - w * 0.5, center + w * 0.5
        zl, zh = zc - h * 0.5, zc + h * 0.5
        if axis == 'x':
            rings.append(((u, c0, zh), (u, c1, zh), (u, c1, zl), (u, c0, zl)))
        else:                                                  # along y
            rings.append(((c0, u, zh), (c1, u, zh), (c1, u, zl), (c0, u, zl)))
    (a0, b0, c0_, d0) = rings[0]
    if axis == 'x':
        shell.quad(a0, b0, c0_, d0, mat, tag)                  # start cap -u
    else:
        shell.quad(a0, d0, c0_, b0, mat, tag)
    for k in range(len(rings) - 1):
        (a1, b1, c1, d1) = rings[k]
        (a2, b2, c2, d2) = rings[k + 1]
        if axis == 'x':
            shell.quad(a1, a2, b2, b1, mat, tag)               # top
            shell.quad(d1, c1, c2, d2, mat, tag)               # bottom
            shell.quad(b1, b2, c2, c1, mat, tag)               # +across
            shell.quad(a1, d1, d2, a2, mat, tag)               # -across
        else:
            shell.quad(a1, b1, b2, a2, mat, tag)
            shell.quad(d1, d2, c2, c1, mat, tag)
            shell.quad(b1, c1, c2, b2, mat, tag)
            shell.quad(a1, a2, d2, d1, mat, tag)
    (an, bn, cn, dn) = rings[-1]
    if axis == 'x':
        shell.quad(an, dn, cn, bn, mat, tag)                   # end cap +u
    else:
        shell.quad(an, bn, cn, dn, mat, tag)


def _vbar(shell, x, y, stations, s, mat, tag=None):
    """Vertical square-bar tube at plan (x, y); stations are (z, y_off)
    (y_off shifts the section centre, 0 for plumb bars). Welded rings, end
    caps, outward winding."""
    rings = []
    for (z, yo) in stations:
        yc = y + yo
        rings.append(((x - s * 0.5, yc - s * 0.5, z),
                      (x + s * 0.5, yc - s * 0.5, z),
                      (x + s * 0.5, yc + s * 0.5, z),
                      (x - s * 0.5, yc + s * 0.5, z)))
    (a0, b0, c0, d0) = rings[0]
    shell.quad(a0, d0, c0, b0, mat, tag)                       # bottom cap
    for k in range(len(rings) - 1):
        (a1, b1, c1, d1) = rings[k]
        (a2, b2, c2, d2) = rings[k + 1]
        shell.quad(a1, b1, b2, a2, mat, tag)                   # -y face
        shell.quad(b1, c1, c2, b2, mat, tag)                   # +x face
        shell.quad(c1, d1, d2, c2, mat, tag)                   # +y face
        shell.quad(d1, a1, a2, d2, mat, tag)                   # -x face
    (an, bn, cn, dn) = rings[-1]
    shell.quad(an, bn, cn, dn, mat, tag)                       # top cap


def _wedge_box(shell, x0, x1, y0, y1, z_bot, zt0, zt1, mat, tag=None):
    """Closed solid: FLAT bottom at z_bot, top plane sloping zt0 (at y0) ->
    zt1 (at y1). End faces are planar trapezoids, sides planar (constant x):
    6 true quads -- the stair-mass / cheek-wall primitive."""
    q = shell.quad
    q((x0, y0, z_bot), (x0, y1, z_bot), (x1, y1, z_bot), (x1, y0, z_bot),
      mat, tag)                                                       # bottom
    q((x0, y0, zt0), (x1, y0, zt0), (x1, y1, zt1), (x0, y1, zt1),
      mat, tag)                                                       # top
    q((x0, y0, z_bot), (x0, y0, zt0), (x0, y1, zt1), (x0, y1, z_bot),
      mat, tag)                                                       # -x
    q((x1, y0, z_bot), (x1, y1, z_bot), (x1, y1, zt1), (x1, y0, zt0),
      mat, tag)                                                       # +x
    q((x0, y0, z_bot), (x1, y0, z_bot), (x1, y0, zt0), (x0, y0, zt0),
      mat, tag)                                                       # y0 end
    q((x0, y1, z_bot), (x0, y1, zt1), (x1, y1, zt1), (x1, y1, z_bot),
      mat, tag)                                                       # y1 end


# ---------------------------------------------------------------------------
# rpg-giun -- wrought-iron railing kit (0a: the most-reused element)
# ---------------------------------------------------------------------------
RAILING_SPEC = [
    dict(name="length", type='FLOAT', default=2.4, min=0.6, max=12.0,
         unit='LENGTH', desc="Run length along x"),
    dict(name="height", type='FLOAT', default=1.0, min=0.6, max=1.4,
         unit='LENGTH', desc="Top-rail height"),
    dict(name="picket_spacing", type='FLOAT', default=0.125, min=0.08,
         max=0.30, unit='LENGTH'),
    dict(name="picket_size", type='FLOAT', default=0.018, min=0.010,
         max=0.040, unit='LENGTH', desc="Square picket bar section"),
    dict(name="style", type='ENUM', default='collars',
         items=('plain', 'collars', 'mid_rail'),
         desc="collars = forged collar block mid-picket"),
    dict(name="post_every", type='FLOAT', default=1.6, min=0.8, max=4.0,
         unit='LENGTH', desc="Intermediate post spacing"),
]


def build_railing(p, rng):
    """Wrought-iron picket railing run along +x, base at z=0.

    Rails are single welded tubes ringed at every picket station (even
    density) and stop 20 mm INSIDE the end posts (never coplanar with a post
    face). Pickets/posts/collars are closed bars interpenetrating the rails
    by >= 8 mm -- no shared planes anywhere."""
    L, H = p["length"], p["height"]
    ps, s = p["picket_spacing"], p["picket_size"]
    rail_w, rail_h = 0.045, 0.045
    z_bot = 0.10                                   # bottom rail centreline
    z_top = H - rail_h * 0.5                       # top rail centreline
    sh = _Shell()
    sh.tag = 'loggia'

    n_pick = max(2, int(round(L / ps)))
    xs = [L * k / n_pick for k in range(n_pick + 1)]
    # rails end 20 mm inside the end posts (posts are ~50 mm wide at 0 / L).
    r_st = [(0.02, 0.0)] + [(x, 0.0) for x in xs[1:-1]] + [(L - 0.02, 0.0)]
    _bar(sh, [(u, z + z_top) for (u, z) in r_st], rail_w, rail_h, M_METAL)
    _bar(sh, [(u, z + z_bot) for (u, z) in r_st], rail_w, rail_h, M_METAL)
    if p["style"] == 'mid_rail':
        zm = (z_top + z_bot) * 0.5
        _bar(sh, [(u, z + zm) for (u, z) in r_st], rail_w * 0.7,
             rail_h * 0.7, M_METAL)

    n_posts = max(1, int(round(L / p["post_every"])))
    post_xs = {round(L * k / n_posts, 4) for k in range(n_posts + 1)}
    zp0 = z_bot - rail_h * 0.5 + 0.012             # embedded into both rails
    zp1 = z_top + rail_h * 0.5 - 0.012
    for x in xs[1:-1]:
        if round(x, 4) in post_xs:
            continue
        _box(sh, (x - s * 0.5, -s * 0.5, zp0), (x + s * 0.5, s * 0.5, zp1),
             M_METAL)
        if p["style"] == 'collars':
            c = s * 1.9
            zc = (zp0 + zp1) * 0.5
            _box(sh, (x - c * 0.5, -c * 0.5, zc - c * 0.6),
                 (x + c * 0.5, c * 0.5, zc + c * 0.6), M_METAL)

    pw = max(0.05, s * 2.6)                        # posts: ground -> +25 mm
    for k in range(n_posts + 1):
        x = min(max(L * k / n_posts, pw * 0.5), L - pw * 0.5)
        _box(sh, (x - pw * 0.5, -pw * 0.5, 0.0),
             (x + pw * 0.5, pw * 0.5, H + 0.025), M_METAL)
    return [sh.to_object("LA_Elem_Railing", [_material(n) for n in _MATS])]


# ---------------------------------------------------------------------------
# rpg-yhkd -- exterior straight-run stucco stair with pipe rail (0b)
# ---------------------------------------------------------------------------
STAIR_SPEC = [
    dict(name="width", type='FLOAT', default=1.1, min=0.8, max=2.0,
         unit='LENGTH'),
    dict(name="height", type='FLOAT', default=2.7, min=0.9, max=4.2,
         unit='LENGTH', desc="Total rise"),
    dict(name="cheek", type='ENUM', default='parapet',
         items=('parapet', 'curb'),
         desc="parapet = solid stucco side walls; curb = low curb + post rail"),
    dict(name="rail_height", type='FLOAT', default=0.9, min=0.8, max=1.1,
         unit='LENGTH'),
]


def build_stucco_stair(p, rng):
    """Straight-run exterior stucco stair ascending +y from the origin.

    Concrete riser/tread ribbon (welded strip at a 6 mm reveal off the cheek
    faces, per the A1 stringer discipline) over a stucco centre mass; each
    side gets either a solid stucco PARAPET cheek wall with a sloped metal
    cap bar, or a low CURB with a post-and-bar pipe rail. The nose pitch
    line is z = y*H/run; every mass top runs parallel to it (planar wedge
    boxes), and separate solids interpenetrate >= 10 mm -- never coplanar."""
    w, H = p["width"], p["height"]
    n = max(3, int(round(H / 0.185)))
    rise = H / n
    run_t = 0.27
    run = run_t * n
    slope = H / run
    ct = 0.15                                       # cheek/curb thickness
    sh = _Shell()
    sh.tag = 'steps'
    rail_h = p["rail_height"]

    ix0, ix1 = ct + 0.006, w + ct - 0.006           # ribbon (6 mm reveal)
    # concrete riser/tread ribbon. Risers are SPLIT at the lip line
    # (zhi - 20 mm) so the side-lip verts weld onto a real riser vert
    # instead of landing mid-edge (T-junction).
    for k in range(n):
        ya, yb = run_t * k, run_t * (k + 1)
        zlo, zhi = rise * k, rise * (k + 1)
        zl2 = zhi - 0.02
        sh.quad((ix0, ya, zlo), (ix1, ya, zlo), (ix1, ya, zl2),
                (ix0, ya, zl2), M_CONCRETE)                        # riser lo
        sh.quad((ix0, ya, zl2), (ix1, ya, zl2), (ix1, ya, zhi),
                (ix0, ya, zhi), M_CONCRETE)                        # riser hi
        sh.quad((ix0, ya, zhi), (ix1, ya, zhi), (ix1, yb, zhi),
                (ix0, yb, zhi), M_CONCRETE)                        # tread
    # side lips close the ribbon edge (thin vertical strips down 20 mm).
    for xx, flip in ((ix0, False), (ix1, True)):
        for k in range(n):
            ya, yb = run_t * k, run_t * (k + 1)
            zhi = rise * (k + 1)
            a, b = (xx, ya, zhi - 0.02), (xx, yb, zhi - 0.02)
            t0, t1 = (xx, ya, zhi), (xx, yb, zhi)
            if flip:
                sh.quad(a, b, t1, t0, M_CONCRETE)
            else:
                sh.quad(a, t0, t1, b, M_CONCRETE)

    # stucco centre mass under the ribbon (starts after step 1; the first
    # riser already closes the front to the ground).
    _wedge_box(sh, ct + 0.012, w + ct - 0.012, run_t, run,
               0.0, rise - 0.01, H - 0.01, M_STUCCO)

    if p["cheek"] == 'parapet':
        # solid cheeks: top parallel to the pitch at rail height (minus the
        # 50 mm cap bar), plus a sloped metal cap embedded 20 mm.
        for cx0 in (0.0, w + ct):
            zt0 = rail_h - 0.05
            _wedge_box(sh, cx0, cx0 + ct, 0.0, run,
                       0.0, zt0, H + zt0, M_STUCCO)
            cx = cx0 + ct * 0.5
            # cap bar centreline: 5 mm above the wall top line -> the 50 mm
            # bar EMBEDS 20 mm into the stucco (never coplanar), overhanging
            # 20 mm past each end.
            zc = lambda y: slope * y + zt0 + 0.005  # noqa: E731
            st = [(-0.02, zc(-0.02)), (run * 0.5, zc(run * 0.5)),
                  (run + 0.02, zc(run + 0.02))]
            _bar(sh, st, ct + 0.010, 0.05, M_METAL, axis='y', center=cx)
    else:
        # low curbs + post rail: bar centreline at pitch + rail height.
        for cx0 in (0.0, w + ct):
            _wedge_box(sh, cx0, cx0 + ct, 0.0, run,
                       0.0, 0.10, H + 0.10, M_STUCCO)
            cx = cx0 + ct * 0.5
            st = [(-0.02, rail_h - slope * 0.02),
                  (run * 0.5, slope * run * 0.5 + rail_h),
                  (run + 0.02, H + rail_h + slope * 0.02)]
            _bar(sh, st, 0.045, 0.045, M_METAL, axis='y', center=cx)
            n_po = max(2, int(round(run / 1.2)))
            for k in range(n_po + 1):
                y = 0.04 + (run - 0.08) * k / n_po
                zc = slope * y
                _box(sh, (cx - 0.022, y - 0.022, zc + 0.10 - 0.030),
                     (cx + 0.022, y + 0.022, zc + rail_h + 0.0025),
                     M_METAL)
    return [sh.to_object("LA_Elem_Stair", [_material(n) for n in _MATS])]


# ---------------------------------------------------------------------------
# rpg-bit5 -- multi-pane steel-sash factory window (0d)
# ---------------------------------------------------------------------------
WINDOW_SPEC = [
    dict(name="width", type='FLOAT', default=1.8, min=0.6, max=4.5,
         unit='LENGTH'),
    dict(name="height", type='FLOAT', default=2.4, min=0.6, max=4.5,
         unit='LENGTH'),
    dict(name="cols", type='INT', default=4, min=1, max=10),
    dict(name="rows", type='INT', default=5, min=1, max=12),
    dict(name="pivot_open", type='INT', default=1, min=0, max=4,
         desc="Hopper panes tilted open"),
    dict(name="broken", type='FLOAT', default=0.0, min=0.0, max=1.0,
         desc="Fraction of panes missing"),
]


def build_factory_window(p, rng):
    """Steel-sash factory window in the xz plane (glass at y=0, faces -y).

    Mitred picture-frame perimeter (welded single island, trapezoid faces
    meeting on the 45-degree corner diagonals), square-bar mullion tubes
    (verticals plumb; horizontals cross THROUGH them as interpenetrating
    islands, both ringed at every crossing for even density, ends embedded
    30 mm into the frame), and a welded glass sheet with grid verts on the
    mullion centrelines (pane edges hide inside the bars). Hopper panes are
    re-emitted tilted about the cell midline; 'broken' panes leave holes
    whose edges hide inside the mullions."""
    W, H = p["width"], p["height"]
    cols, rows = p["cols"], p["rows"]
    fd, fw = 0.09, 0.07                            # frame depth (y), width
    m = 0.028                                      # mullion bar section
    sh = _Shell()
    sh.tag = 'windows'

    ox0, ox1, oz0, oz1 = 0.0, W, 0.0, H
    ix0, ix1, iz0, iz1 = fw, W - fw, fw, H - fw
    oc = [(ox0, oz0), (ox1, oz0), (ox1, oz1), (ox0, oz1)]
    ic = [(ix0, iz0), (ix1, iz0), (ix1, iz1), (ix0, iz1)]
    for y in (-fd * 0.5, fd * 0.5):                # front + back rings
        for k in range(4):
            (oxa, oza), (oxb, ozb) = oc[k], oc[(k + 1) % 4]
            (ixa, iza), (ixb, izb) = ic[k], ic[(k + 1) % 4]
            a, b = (oxa, y, oza), (oxb, y, ozb)
            c, d = (ixb, y, izb), (ixa, y, iza)
            if y < 0:
                sh.quad(a, b, c, d, M_METAL)
            else:
                sh.quad(a, d, c, b, M_METAL)
    for k in range(4):                             # outer + inner reveals
        (oxa, oza), (oxb, ozb) = oc[k], oc[(k + 1) % 4]
        (ixa, iza), (ixb, izb) = ic[k], ic[(k + 1) % 4]
        sh.quad((oxa, -fd * 0.5, oza), (oxa, fd * 0.5, oza),
                (oxb, fd * 0.5, ozb), (oxb, -fd * 0.5, ozb), M_METAL)
        sh.quad((ixa, -fd * 0.5, iza), (ixb, -fd * 0.5, izb),
                (ixb, fd * 0.5, izb), (ixa, fd * 0.5, iza), M_METAL)

    gx = [ix0 + (ix1 - ix0) * k / cols for k in range(cols + 1)]
    gz = [iz0 + (iz1 - iz0) * k / rows for k in range(rows + 1)]
    for x in gx[1:-1]:                             # plumb verticals
        st = [(iz0 - 0.03, 0.0)] + [(z, 0.0) for z in gz[1:-1]] + \
             [(iz1 + 0.03, 0.0)]
        _vbar(sh, x, 0.0, st, m, M_METAL)
    for z in gz[1:-1]:                             # horizontals, through
        st = [(ix0 - 0.03, z)] + [(x, z) for x in gx[1:-1]] + \
             [(ix1 + 0.03, z)]
        _bar(sh, st, m, m, M_METAL, axis='x')

    def _shatter_rect(sx0, sx1, sz0, sz1, shared):
        """Knife ONE jagged star-shaped hole out of rect [sx0,sx1]x[sz0,sz1]:
        the remaining glass is a ring of quads between the hole polygon and
        the rect boundary -- the shards ARE what's left, so they can never
        overlap. Boundary samples: the 4 corners + extra points per edge
        (none on a @p shared internal edge, so two sub-rects weld cleanly at
        corners only -- no T-junctions). Hole verts sit on the centre ray of
        their boundary sample at a jagged radius; occasional near-centre
        radii make long shard fangs."""
        pts = []

        def edge(p0, p1, name):
            pts.append(p0)
            n_extra = 0 if name == shared else rng.randint(1, 2)
            for t in sorted(rng.uniform(0.22, 0.78) for _ in range(n_extra)):
                pts.append((p0[0] + (p1[0] - p0[0]) * t,
                            p0[1] + (p1[1] - p0[1]) * t))
        edge((sx0, sz0), (sx1, sz0), 'bottom')
        edge((sx1, sz0), (sx1, sz1), 'right')
        edge((sx1, sz1), (sx0, sz1), 'top')
        edge((sx0, sz1), (sx0, sz0), 'left')
        ccx = (sx0 + sx1) * 0.5 + rng.uniform(-0.12, 0.12) * (sx1 - sx0)
        ccz = (sz0 + sz1) * 0.5 + rng.uniform(-0.12, 0.12) * (sz1 - sz0)
        hole = []
        for (bx_, bz_) in pts:
            r = rng.uniform(0.45, 0.80)
            if rng.random() < 0.30:
                r = max(0.08, r * 0.30)               # long jagged fang
            hole.append((ccx + (bx_ - ccx) * r, ccz + (bz_ - ccz) * r))
        nn = len(pts)
        for k in range(nn):
            (b0x, b0z), (b1x, b1z) = pts[k], pts[(k + 1) % nn]
            (h0x, h0z), (h1x, h1z) = hole[k], hole[(k + 1) % nn]
            sh.quad((b0x, 0.0, b0z), (b1x, 0.0, b1z),
                    (h1x, 0.0, h1z), (h0x, 0.0, h0z), M_GLASS)

    def shards(xa, xb, za, zb):
        """Shattered pane: inset 4 mm (ring verts stay off the shared glass-
        sheet edge lines, hidden inside the mullion/frame bars), then knife
        ONE jagged hole -- a pane this small breaks as a single hole, never
        two."""
        xa, xb, za, zb = xa + 0.004, xb - 0.004, za + 0.004, zb - 0.004
        if (xb - xa) < 0.12 or (zb - za) < 0.12:
            return                                     # sliver: swept clean
        _shatter_rect(xa, xb, za, zb, None)

    cells = [(i, j) for i in range(cols) for j in range(rows)]
    rng.shuffle(cells)
    n_broken = int(round(p["broken"] * len(cells)))
    broken = set(cells[:n_broken])
    pivots = set(cells[n_broken:n_broken + p["pivot_open"]])
    for i in range(cols):
        for j in range(rows):
            xa = ix0 - 0.02 if i == 0 else gx[i]
            xb = ix1 + 0.02 if i == cols - 1 else gx[i + 1]
            za = iz0 - 0.02 if j == 0 else gz[j]
            zb = iz1 + 0.02 if j == rows - 1 else gz[j + 1]
            if (i, j) in broken:
                # mostly-shattered: jagged residual shards cling to the
                # bars (1 in 4 panes is swept fully clean).
                if rng.random() >= 0.25:
                    shards(xa, xb, za, zb)
                continue
            if (i, j) in pivots:
                zm = (za + zb) * 0.5
                hz = (zb - za) * 0.5
                ang = math.radians(28.0)
                dy = math.sin(ang) * hz
                dz = (1.0 - math.cos(ang)) * hz
                sh.quad((xa, -dy, zb - dz), (xb, -dy, zb - dz),
                        (xb, dy, za + dz), (xa, dy, za + dz), M_GLASS)
            else:
                sh.quad((xa, 0.0, za), (xb, 0.0, za), (xb, 0.0, zb),
                        (xa, 0.0, zb), M_GLASS)
    return [sh.to_object("LA_Elem_FactoryWindow",
                         [_material(n) for n in _MATS])]


# ---------------------------------------------------------------------------
# rpg-9s27 -- Beaux-Arts modillion cornice (0f)
# ---------------------------------------------------------------------------
CORNICE_SPEC = [
    dict(name="length", type='FLOAT', default=6.0, min=1.0, max=30.0,
         unit='LENGTH'),
    dict(name="projection", type='FLOAT', default=0.45, min=0.2, max=0.9,
         unit='LENGTH'),
    dict(name="height", type='FLOAT', default=0.55, min=0.3, max=1.0,
         unit='LENGTH'),
    dict(name="modillion_spacing", type='FLOAT', default=0.55, min=0.3,
         max=1.2, unit='LENGTH'),
    dict(name="dentils", type='BOOL', default=True),
]


def build_cornice(p, rng):
    """Beaux-Arts modillion cornice along +x; wall plane y=0, projecting -y,
    crown top at z = height (mount with the top at the parapet line).

    The stepped profile (bed steps -> soffit -> corona -> cap) is swept with
    a station at every modillion bay (even density). End caps tile each
    z-slab of the stepped section as rectangles SPLIT at every profile
    depth, so cap edges align across steps (no T-junctions, no ngons).
    Modillion brackets + dentils are closed boxes embedded 10 mm through
    the shell faces into the hollow interior -- never coplanar."""
    L, P, H = p["length"], p["projection"], p["height"]
    ms = p["modillion_spacing"]
    sh = _Shell()
    sh.tag = 'parapet'

    b1, b2 = -0.30 * P, -0.55 * P                  # bed step depths
    z1, z2 = 0.16 * H, 0.30 * H
    z_sof = 0.52 * H                               # soffit underside
    z_cor = 0.88 * H                               # corona top
    yc = -P + 0.06                                 # cap setback plane
    prof = [(0.0, 0.0), (b1, 0.0), (b1, z1), (b2, z1), (b2, z_sof),
            (-P, z_sof), (-P, z_cor), (yc, z_cor), (yc, H), (0.0, H)]

    nb = max(1, int(round(L / ms)))
    xs = [L * k / nb for k in range(nb + 1)]
    y_splits = sorted({y for (y, _z) in prof})     # ascending (deepest first)
    for k in range(len(prof) - 1):                 # profile sweep, per bay
        (ya, za), (yb, zb) = prof[k], prof[k + 1]
        # HORIZONTAL segments are split at every profile depth crossing
        # their span, so the end-cap column verts (which sit at those
        # depths) weld onto real sweep verts instead of mid-edge.
        if abs(za - zb) < 1e-9:
            lo, hi = min(ya, yb), max(ya, yb)
            cuts = [y for y in y_splits if lo - 1e-9 <= y <= hi + 1e-9]
            spans = list(zip(cuts[:-1], cuts[1:]))
            if ya > yb:                            # keep authored direction
                spans = [(y1, y0) for (y0, y1) in reversed(spans)]
        else:
            spans = [(ya, yb)]
        for (sa, sb) in spans:
            for s in range(nb):
                xa, xb = xs[s], xs[s + 1]
                sh.quad((xa, sa, za), (xb, sa, za), (xb, sb, zb),
                        (xa, sb, zb), M_TRIM)
    for x, flip in ((0.0, False), (L, True)):
        for k in range(len(prof) - 1):
            (ya, za), (yb, zb) = prof[k], prof[k + 1]
            if abs(za - zb) < 1e-9 or abs(ya) < 1e-9:
                continue                           # horizontal / at-wall seg
            cols = [y for y in y_splits if y >= ya - 1e-9]
            for ci in range(len(cols) - 1):
                y0c, y1c = cols[ci], cols[ci + 1]
                a, b = (x, y0c, za), (x, y0c, zb)
                c, d = (x, y1c, zb), (x, y1c, za)
                if flip:
                    sh.quad(a, d, c, b, M_TRIM)
                else:
                    sh.quad(a, b, c, d, M_TRIM)

    # modillions: stepped double-block brackets embedded up through the
    # soffit face; even spacing = the sweep stations.
    for k in range(nb + 1):
        x = min(max(xs[k], 0.10), L - 0.10)
        _box(sh, (x - 0.07, -P + 0.05, z_sof - 0.15),
             (x + 0.07, b2 - 0.012, z_sof + 0.01), M_TRIM)
        _box(sh, (x - 0.05, -P + 0.028, z_sof - 0.21),
             (x + 0.05, -P + 0.16, z_sof - 0.14), M_TRIM)
    if p["dentils"]:
        step = ms / 3.0
        nd = max(1, int(round(L / step)))
        for k in range(nd + 1):
            x = min(max(L * k / nd, 0.05), L - 0.05)
            _box(sh, (x - 0.035, b1 - 0.045, z1 - 0.09),
                 (x + 0.035, b1 + 0.010, z1 + 0.010), M_TRIM)
    return [sh.to_object("LA_Elem_Cornice", [_material(n) for n in _MATS])]


# ---------------------------------------------------------------------------
# rpg-mv16 -- exterior steel switchback stair (0b)
# ---------------------------------------------------------------------------
SWITCHBACK_SPEC = [
    dict(name="width", type='FLOAT', default=1.1, min=0.9, max=1.6,
         unit='LENGTH', desc="Flight width"),
    dict(name="height", type='FLOAT', default=3.0, min=2.0, max=6.0,
         unit='LENGTH', desc="Total rise"),
    dict(name="rail_height", type='FLOAT', default=0.95, min=0.8, max=1.1,
         unit='LENGTH'),
]


def _steel_flight(sh, x0, x1, y0, y1, z0, z1, rail_h, rail_side,
                  ext_y=None):
    """One steel flight from base (y0,z0) to top (y1,z1): channel stringers
    (sheared boxes on the nose pitch line), open-riser tread plates embedded
    10 mm into the stringers, and a post-and-bar rail on @p rail_side ('lo'
    = the x0 stringer, 'hi' = x1). When @p ext_y is given the rail bar
    CONTINUES horizontally to that y at BASE level +rail_h -- one welded
    polyline with a mitred pitch break, so there is no butt joint (butted
    bar caps were coplanar -> T-junctions)."""
    st, td = 0.06, 0.28                            # stringer web / depth
    n = max(2, int(round(abs(z1 - z0) / 0.185)))
    rise, run = (z1 - z0) / n, (y1 - y0) / n
    for (sx0, sx1) in ((x0, x0 + st), (x1 - st, x1)):
        _sheared_box(sh, sx0, sx1, y0, y1, z0 + 0.03, z1 + 0.03, td, M_METAL)
    for k in range(n):                             # open-riser tread plates
        ya = y0 + run * k
        zk = z0 + rise * (k + 1)
        lo, hi = (ya, ya + abs(run) * 0.92) if run > 0 else \
                 (ya - abs(run) * 0.92, ya)
        _box(sh, (x0 + st - 0.010, lo, zk - 0.045),
             (x1 - st + 0.010, hi, zk), M_METAL)
    if rail_side is None:
        return
    cx = (x0 + st * 0.5) if rail_side == 'lo' else (x1 - st * 0.5)
    slope = (z1 - z0) / (y1 - y0)
    zline = lambda y: z0 + (y - y0) * slope        # noqa: E731
    ym = 0.5 * (y0 + y1)
    stations = [(y1, zline(y1) + rail_h), (ym, zline(ym) + rail_h),
                (y0, zline(y0) + rail_h)]
    if ext_y is not None:                          # landing continuation
        stations.append((ext_y, z0 + rail_h))
    _bar(sh, stations, 0.045, 0.045, M_METAL, axis='y', center=cx)
    n_po = max(2, int(round(abs(y1 - y0) / 1.1)))
    for k in range(n_po + 1):                      # raked-run posts
        y = y0 + (y1 - y0) * (0.06 + 0.88 * k / n_po)
        _box(sh, (cx - 0.02, y - 0.02, zline(y) + 0.01),
             (cx + 0.02, y + 0.02, zline(y) + rail_h - 0.0025), M_METAL)
    return


def build_switchback_stair(p, rng):
    """Exterior steel switchback: flight A ascends +y onto the half-landing;
    flight B departs from the SAME landing edge beside it and returns -y --
    so the turn actually works (up A, 180 on the landing, up B). Channel
    stringers, open risers, continuous mitred outer rails that run onto the
    landing, far-edge landing rail. All members interpenetrate >= 10 mm,
    never sharing a plane."""
    w, H = p["width"], p["height"]
    rail_h = p["rail_height"]
    gap = 0.06
    n = max(6, int(round(H / 0.185)))
    n1 = (n + 1) // 2
    n2 = n - n1
    run_t = 0.27
    h1 = H * n1 / n
    run1 = run_t * n1
    land_d = 1.05
    xB0 = w + gap                                  # flight B x range
    lx0, lx1 = 0.0, xB0 + w
    ly0, ly1 = run1 - 0.02, run1 + land_d
    sh = _Shell()
    sh.tag = 'steps'

    # both flights join the landing at its NEAR edge (y ~ run1): A tops out
    # there ascending +y; B's base tucks 20 mm into the edge channel and
    # ascends back -y beside A. Their rails continue onto the landing sides.
    _steel_flight(sh, 0.0, w, 0.0, run1, 0.0, h1, rail_h, 'lo',
                  ext_y=ly1 - 0.02)
    _steel_flight(sh, xB0, xB0 + w, run1 + 0.02, run1 + 0.02 - run_t * n2,
                  h1, H, rail_h, 'hi', ext_y=ly1 - 0.02)

    # half-landing: plate + edge channels + 4 posts to ground.
    _box(sh, (lx0 + 0.01, ly0, h1 - 0.02), (lx1 - 0.01, ly1, h1 + 0.03),
         M_METAL)
    _box(sh, (lx0, ly0 - 0.008, h1 - 0.16), (lx1, ly0 + 0.05, h1 + 0.035),
         M_METAL)
    _box(sh, (lx0, ly1 - 0.05, h1 - 0.16), (lx1, ly1 + 0.008, h1 + 0.035),
         M_METAL)
    for (px, py) in ((lx0 + 0.06, ly1 - 0.08), (lx1 - 0.06, ly1 - 0.08),
                     (lx0 + 0.06, ly0 + 0.10), (lx1 - 0.06, ly0 + 0.10)):
        _box(sh, (px - 0.035, py - 0.035, 0.0), (px + 0.035, py + 0.035,
             h1 - 0.015), M_METAL)

    # far-edge landing rail: dropped 6 mm so the through-running side bars
    # cross it without any shared plane; posts stop inside the bars.
    ztop = h1 + rail_h
    _bar(sh, [(lx0 + 0.02, ztop - 0.006), (0.5 * (lx0 + lx1), ztop - 0.006),
              (lx1 - 0.02, ztop - 0.006)], 0.045, 0.045, M_METAL, axis='x',
         center=ly1 - 0.035)
    for (px, py) in ((lx0 + 0.03, ly1 - 0.10), (lx1 - 0.03, ly1 - 0.10),
                     (0.5 * (lx0 + lx1), ly1 - 0.035)):
        _box(sh, (px - 0.02, py - 0.02, h1 + 0.02),
             (px + 0.02, py + 0.02, ztop - 0.012), M_METAL)
    return [sh.to_object("LA_Elem_Switchback", [_material(n) for n in _MATS])]


# ---------------------------------------------------------------------------
# rpg-0ag1 -- rolling metal shutter shopfront (0e)
# ---------------------------------------------------------------------------
SHUTTER_SPEC = [
    dict(name="width", type='FLOAT', default=3.0, min=1.2, max=6.0,
         unit='LENGTH'),
    dict(name="height", type='FLOAT', default=2.8, min=2.0, max=4.5,
         unit='LENGTH'),
    dict(name="open", type='FLOAT', default=0.0, min=0.0, max=0.8,
         desc="0 = rolled fully down; 0.8 = mostly open"),
    dict(name="housing", type='BOOL', default=True,
         desc="Exposed roll housing box at the head"),
]


def build_shutter(p, rng):
    """Roll-down shutter in the xz plane (curtain near y=0, faces -y).

    Side guide channels, optional roll-housing box, and the CURTAIN: a single
    welded accordion ribbon -- per slat an angled face quad + an interlock
    step quad (planar: every quad spans two parallel horizontal lines), ends
    embedded 15 mm into the guides. Bottom bar closes the curtain edge."""
    W, H = p["width"], p["height"]
    sh = _Shell()
    sh.tag = 'shutters'
    gw, gd = 0.09, 0.10                            # guide channel w/d
    _box(sh, (0.0, -gd * 0.5, 0.0), (gw, gd * 0.5, H), M_METAL)
    _box(sh, (W - gw, -gd * 0.5, 0.0), (W, gd * 0.5, H), M_METAL)
    z_head = H
    if p["housing"]:
        _box(sh, (-0.03, -0.17, H - 0.02), (W + 0.03, 0.17, H + 0.32),
             M_METAL)
        z_head = H - 0.02
    # curtain: accordion ribbon from the head down to the opening line.
    z_bot = 0.02 + p["open"] * (H - 0.55)
    xa, xb = gw - 0.015, W - gw + 0.015            # embedded into guides
    pitch, face_dy, hook = 0.10, 0.014, 0.025
    z = z_head
    yF, yB = -0.02, -0.02 + face_dy                # slat face / hook planes
    while z - pitch >= z_bot:
        zf = z - (pitch - hook)
        sh.quad((xa, yF, z), (xb, yF, z), (xb, yB, zf), (xa, yB, zf),
                M_SHUTTER)                          # angled slat face
        sh.quad((xa, yB, zf), (xb, yB, zf), (xb, yF, zf - hook),
                (xa, yF, zf - hook), M_SHUTTER)     # interlock step
        z = zf - hook
    _box(sh, (xa - 0.005, yF - 0.035, z - 0.09),
         (xb + 0.005, yF + 0.045, z + 0.005), M_METAL)   # bottom bar
    return [sh.to_object("LA_Elem_Shutter", [_material(n) for n in _MATS])]


# ---------------------------------------------------------------------------
# rpg-v3y9 -- cantilevered corrugated shop awning (0g)
# ---------------------------------------------------------------------------
AWNING_SPEC = [
    dict(name="width", type='FLOAT', default=2.6, min=1.0, max=6.0,
         unit='LENGTH'),
    dict(name="depth", type='FLOAT', default=1.1, min=0.5, max=2.0,
         unit='LENGTH', desc="Projection from the wall"),
    dict(name="drop", type='FLOAT', default=0.35, min=0.1, max=0.8,
         unit='LENGTH', desc="Fall from wall to front edge"),
    dict(name="struts", type='INT', default=2, min=0, max=5,
         desc="Diagonal underside struts"),
]


def build_awning(p, rng):
    """Corrugated cantilever awning: wall plane y=0, mount top at z=0,
    sheet sloping down-out to (-depth, -drop). The sheet is a welded
    accordion of planar quads (verts alternate between two parallel sloped
    planes along x). Ledger at the wall, front lip channel, and sheared-box
    diagonal struts underneath."""
    W, D, drop = p["width"], p["depth"], p["drop"]
    sh = _Shell()
    sh.tag = 'awnings'
    _box(sh, (0.0, -0.045, -0.14), (W, 0.005, 0.0), M_METAL)      # ledger
    # sheet normal (in the yz plane, perpendicular to the slope).
    import math as _m
    sl = _m.sqrt(D * D + drop * drop)
    nY, nZ = -drop / sl, D / sl                    # unit normal (points up-out)
    amp, pitch = 0.022, 0.085
    nseg = max(2, int(round(W / pitch)))
    xs = [W * k / nseg for k in range(nseg + 1)]
    y0, z0 = -0.02, -0.03                          # start just off the wall
    y1, z1 = -D, -0.03 - drop
    for k in range(nseg):
        o0 = amp if (k % 2) else 0.0               # alternate planes
        o1 = amp if ((k + 1) % 2) else 0.0
        a = (xs[k], y0 + nY * o0, z0 + nZ * o0)
        b = (xs[k + 1], y0 + nY * o1, z0 + nZ * o1)
        c = (xs[k + 1], y1 + nY * o1, z1 + nZ * o1)
        d = (xs[k], y1 + nY * o0, z1 + nZ * o0)
        sh.quad(a, b, c, d, M_METAL)
    _box(sh, (-0.005, -D - 0.05, -0.10 - drop),
         (W + 0.005, -D + 0.06, -0.005 - drop), M_METAL)          # front lip
    ns = p["struts"]
    for k in range(ns):
        x = W * (k + 1) / (ns + 1)
        # diagonal strut: wall (lower) out to the front edge underside.
        _sheared_box(sh, x - 0.025, x + 0.025, -0.03, -D + 0.10,
                     -0.30 - drop * 0.4, -drop - 0.06, 0.05, M_METAL)
    return [sh.to_object("LA_Elem_Awning", [_material(n) for n in _MATS])]


# ---------------------------------------------------------------------------
# rpg-8o58 -- vertical projecting blade sign (0h)
# ---------------------------------------------------------------------------
BLADE_SPEC = [
    dict(name="height", type='FLOAT', default=2.6, min=1.0, max=6.0,
         unit='LENGTH', desc="Blade height"),
    dict(name="projection", type='FLOAT', default=0.9, min=0.4, max=1.6,
         unit='LENGTH', desc="Stand-off from the wall"),
    dict(name="panels", type='INT', default=4, min=1, max=8,
         desc="Stacked letter panels on the blade"),
    dict(name="top_z", type='FLOAT', default=4.5, min=2.0, max=12.0,
         unit='LENGTH', desc="Mount height of the blade top"),
]


def build_blade_sign(p, rng):
    """Projecting blade sign: wall plane x=0 (blade projects +x), sign faces
    +/-y. Wall plate + two bracket arms, the blade box, proud border trim
    strips on both faces, and stacked letter-panel strips (alternating sign
    palette) -- every add-on a closed box embedded >= 8 mm, never coplanar."""
    Hb, P = p["height"], p["projection"]
    z1 = p["top_z"]
    z0 = z1 - Hb
    bt = 0.12                                      # blade thickness
    sh = _Shell()
    sh.tag = 'signage'
    _box(sh, (0.0, -0.10, z0 + Hb * 0.15), (0.035, 0.10, z1 - Hb * 0.05),
         M_METAL)                                  # wall plate
    for zc in (z1 - Hb * 0.12, z0 + Hb * 0.22):    # bracket arms
        _bar(sh, [(0.02, zc), (P * 0.55, zc)], 0.05, 0.05, M_METAL,
             axis='x')
    _box(sh, (P * 0.45, -bt * 0.5, z0), (P, bt * 0.5, z1), M_SIGN_A)
    # border trim: proud strips around both faces (top/bottom/front/back).
    for ys, ye in ((-bt * 0.5 - 0.015, -bt * 0.5 + 0.008),
                   (bt * 0.5 - 0.008, bt * 0.5 + 0.015)):
        _box(sh, (P * 0.45 + 0.02, ys, z1 - 0.06), (P - 0.02, ye, z1 + 0.012),
             M_TRIM)
        _box(sh, (P * 0.45 + 0.02, ys, z0 - 0.012), (P - 0.02, ye, z0 + 0.06),
             M_TRIM)
        _box(sh, (P * 0.45 - 0.012, ys, z0 + 0.02), (P * 0.45 + 0.05, ye,
             z1 - 0.02), M_TRIM)
        _box(sh, (P - 0.05, ys, z0 + 0.02), (P + 0.012, ye, z1 - 0.02),
             M_TRIM)
    np_ = p["panels"]
    for k in range(np_):                           # stacked letter panels
        za = z0 + Hb * (k + 0.18) / np_
        zb = z0 + Hb * (k + 0.82) / np_
        for ys, ye in ((-bt * 0.5 - 0.010, -bt * 0.5 + 0.010),
                       (bt * 0.5 - 0.010, bt * 0.5 + 0.010)):
            _box(sh, (P * 0.45 + 0.09, ys, za), (P - 0.09, ye, zb), M_SIGN_B)
    return [sh.to_object("LA_Elem_BladeSign", [_material(n) for n in _MATS])]


# ---------------------------------------------------------------------------
# registration
# ---------------------------------------------------------------------------
params.register_tool(idname="la_railing", label="Railing Kit",
                     family="Elements", build=build_railing,
                     spec=RAILING_SPEC)
params.register_tool(idname="la_stucco_stair", label="Stucco Stair",
                     family="Elements", build=build_stucco_stair,
                     spec=STAIR_SPEC)
params.register_tool(idname="la_factory_window", label="Factory Window",
                     family="Elements", build=build_factory_window,
                     spec=WINDOW_SPEC)
params.register_tool(idname="la_cornice", label="Modillion Cornice",
                     family="Elements", build=build_cornice,
                     spec=CORNICE_SPEC)
params.register_tool(idname="la_switchback_stair", label="Switchback Stair",
                     family="Elements", build=build_switchback_stair,
                     spec=SWITCHBACK_SPEC)
params.register_tool(idname="la_shutter", label="Roll-Up Shutter",
                     family="Elements", build=build_shutter,
                     spec=SHUTTER_SPEC)
params.register_tool(idname="la_awning", label="Corrugated Awning",
                     family="Elements", build=build_awning,
                     spec=AWNING_SPEC)
params.register_tool(idname="la_blade_sign", label="Blade Sign",
                     family="Elements", build=build_blade_sign,
                     spec=BLADE_SPEC)
