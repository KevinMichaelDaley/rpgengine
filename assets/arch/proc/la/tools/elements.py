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
    M_ASPHALT, M_CONCRETE, M_GLASS, M_METAL, M_PLYWOOD, M_SHUTTER,
    M_SIGN_A, M_SIGN_B, M_STUCCO, M_TRIM,
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


def _plan_bar(sh, pts, z, w, h, mat, tag=None):
    """Rectangular-section bar following a PLAN polyline at constant height
    (centreline z): one ring per point, offset along the mitre bisector
    (scaled 1/cos(half-angle)) so corners are true mitres -- ONE welded
    member, no overshooting butt joints. Collinear points subdivide for
    even density. All faces planar (horizontal tops/bottoms, vertical
    sides through two plan points)."""
    import math as _m
    n = len(pts)
    if n < 2:
        return
    rings = []
    for i in range(n):
        (px, py) = pts[i]
        if i == 0:
            dx, dy = pts[1][0] - px, pts[1][1] - py
            L = _m.hypot(dx, dy)
            nx, ny = -dy / L, dx / L
            ox, oy = nx * w * 0.5, ny * w * 0.5
        elif i == n - 1:
            dx, dy = px - pts[i - 1][0], py - pts[i - 1][1]
            L = _m.hypot(dx, dy)
            nx, ny = -dy / L, dx / L
            ox, oy = nx * w * 0.5, ny * w * 0.5
        else:
            d0x, d0y = px - pts[i - 1][0], py - pts[i - 1][1]
            d1x, d1y = pts[i + 1][0] - px, pts[i + 1][1] - py
            L0, L1 = _m.hypot(d0x, d0y), _m.hypot(d1x, d1y)
            n0x, n0y = -d0y / L0, d0x / L0
            n1x, n1y = -d1y / L1, d1x / L1
            bx_, by_ = n0x + n1x, n0y + n1y
            bl = _m.hypot(bx_, by_)
            if bl < 1e-6:                          # U-turn: fall back
                ox, oy = n0x * w * 0.5, n0y * w * 0.5
            else:
                scale = (w * 0.5) / max(0.2, (bx_ * n0x + by_ * n0y) / bl)
                ox, oy = bx_ / bl * scale, by_ / bl * scale
        rings.append(((px + ox, py + oy, z + h * 0.5),
                      (px - ox, py - oy, z + h * 0.5),
                      (px - ox, py - oy, z - h * 0.5),
                      (px + ox, py + oy, z - h * 0.5)))
    (a0, b0, c0, d0) = rings[0]
    sh.quad(a0, b0, c0, d0, mat, tag)              # start cap
    for k in range(n - 1):
        (a1, b1, c1, d1) = rings[k]
        (a2, b2, c2, d2) = rings[k + 1]
        sh.quad(a1, a2, b2, b1, mat, tag)          # top
        sh.quad(d1, c1, c2, d2, mat, tag)          # bottom
        sh.quad(b1, b2, c2, c1, mat, tag)          # inner side
        sh.quad(a1, d1, d2, a2, mat, tag)          # outer side
    (an, bn, cn, dn) = rings[-1]
    sh.quad(an, dn, cn, bn, mat, tag)              # end cap


def emit_railing_path(sh, pts, z0=0.0, height=1.0, spacing=0.125,
                      size=0.018, style='plain', post_every=1.6,
                      tag='loggia'):
    """Picket railing along a PLAN polyline: the top/bottom rails are ONE
    mitred _plan_bar each (corners merged, nothing overshoots), pickets at
    even stations per segment, posts at every corner + spaced along runs."""
    import math as _m
    keep = sh.tag
    sh.tag = tag
    rail_h = 0.045
    z_bot = z0 + 0.10
    z_top = z0 + height - rail_h * 0.5
    # subdivided station list (verts at every picket for even density).
    stations = [pts[0]]
    seg_info = []
    for i in range(len(pts) - 1):
        (ax, ay), (bx, by) = pts[i], pts[i + 1]
        L = _m.hypot(bx - ax, by - ay)
        n_p = max(2, int(round(L / spacing)))
        seg_info.append((ax, ay, bx, by, L, n_p))
        for k in range(1, n_p + 1):
            stations.append((ax + (bx - ax) * k / n_p,
                             ay + (by - ay) * k / n_p))
    _plan_bar(sh, stations, z_top, 0.045, rail_h, M_METAL)
    _plan_bar(sh, stations, z_bot, 0.045, rail_h, M_METAL)
    if style == 'mid_rail':
        _plan_bar(sh, stations, (z_top + z_bot) * 0.5, 0.032, 0.032,
                  M_METAL)
    zp0 = z_bot - rail_h * 0.5 + 0.012
    zp1 = z_top + rail_h * 0.5 - 0.012
    for (ax, ay, bx, by, L, n_p) in seg_info:      # pickets per segment
        for k in range(1, n_p):
            px = ax + (bx - ax) * k / n_p
            py = ay + (by - ay) * k / n_p
            _box(sh, (px - size * 0.5, py - size * 0.5, zp0),
                 (px + size * 0.5, py + size * 0.5, zp1), M_METAL)
            if style == 'collars':
                c = size * 1.9
                zc = (zp0 + zp1) * 0.5
                _box(sh, (px - c * 0.5, py - c * 0.5, zc - c * 0.6),
                     (px + c * 0.5, py + c * 0.5, zc + c * 0.6), M_METAL)
    pw = 0.05
    for (px, py) in pts:                           # corner + end posts
        _box(sh, (px - pw * 0.5, py - pw * 0.5, z0),
             (px + pw * 0.5, py + pw * 0.5, z0 + height + 0.025), M_METAL)
    for (ax, ay, bx, by, L, n_p) in seg_info:      # intermediate posts
        n_po = int(L / post_every)
        for k in range(1, n_po + 1):
            t = k * post_every / L
            if t > 0.94:
                continue
            px, py = ax + (bx - ax) * t, ay + (by - ay) * t
            _box(sh, (px - pw * 0.5, py - pw * 0.5, z0),
                 (px + pw * 0.5, py + pw * 0.5, z0 + height + 0.025),
                 M_METAL)
    sh.tag = keep


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


def emit_railing(sh, u0, u1, axis='x', cross=0.0, z0=0.0, height=1.0,
                 spacing=0.125, size=0.018, style='plain', post_every=1.6,
                 tag='loggia'):
    """Wrought-iron picket railing EMITTER: run along @p axis from u0..u1
    at across-axis line @p cross, base z0. Rails are single welded tubes
    ringed at every picket station and stop 20 mm inside the end posts;
    pickets/posts/collars interpenetrate the rails >= 8 mm -- no shared
    planes. The assemblies (balcony decks, walkways) call this directly."""
    keep = sh.tag
    sh.tag = tag
    L = u1 - u0
    rail_w, rail_h = 0.045, 0.045
    z_bot = z0 + 0.10
    z_top = z0 + height - rail_h * 0.5

    def bx(u, half_u, half_c, za, zb, mat):
        if axis == 'x':
            _box(sh, (u - half_u, cross - half_c, za),
                 (u + half_u, cross + half_c, zb), mat)
        else:
            _box(sh, (cross - half_c, u - half_u, za),
                 (cross + half_c, u + half_u, zb), mat)

    n_pick = max(2, int(round(L / spacing)))
    us = [u0 + L * k / n_pick for k in range(n_pick + 1)]
    r_st = [(u0 + 0.02, 0.0)] + [(u, 0.0) for u in us[1:-1]] +            [(u1 - 0.02, 0.0)]
    _bar(sh, [(u, z_top) for (u, _z) in r_st], rail_w, rail_h, M_METAL,
         axis=axis, center=cross)
    _bar(sh, [(u, z_bot) for (u, _z) in r_st], rail_w, rail_h, M_METAL,
         axis=axis, center=cross)
    if style == 'mid_rail':
        zm = (z_top + z_bot) * 0.5
        _bar(sh, [(u, zm) for (u, _z) in r_st], rail_w * 0.7, rail_h * 0.7,
             M_METAL, axis=axis, center=cross)

    n_posts = max(1, int(round(L / post_every)))
    post_us = {round(u0 + L * k / n_posts, 4) for k in range(n_posts + 1)}
    zp0 = z_bot - rail_h * 0.5 + 0.012
    zp1 = z_top + rail_h * 0.5 - 0.012
    for u in us[1:-1]:
        if round(u, 4) in post_us:
            continue
        bx(u, size * 0.5, size * 0.5, zp0, zp1, M_METAL)
        if style == 'collars':
            c = size * 1.9
            zc = (zp0 + zp1) * 0.5
            bx(u, c * 0.5, c * 0.5, zc - c * 0.6, zc + c * 0.6, M_METAL)
    pw = max(0.05, size * 2.6)
    for k in range(n_posts + 1):
        u = min(max(u0 + L * k / n_posts, u0 + pw * 0.5), u1 - pw * 0.5)
        bx(u, pw * 0.5, pw * 0.5, z0, z0 + height + 0.025, M_METAL)
    sh.tag = keep


def build_railing(p, rng):
    """Standalone railing tool: one emit_railing run along +x at origin."""
    sh = _Shell()
    emit_railing(sh, 0.0, p["length"], axis='x', cross=0.0, z0=0.0,
                 height=p["height"], spacing=p["picket_spacing"],
                 size=p["picket_size"], style=p["style"],
                 post_every=p["post_every"])
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
    dict(name="double_rail", type='BOOL', default=True,
         desc="Rail BOTH sides of the return (top) flight"),
]


def _steel_flight(sh, x0, x1, y0, y1, z0, z1, rail_h, rail_side,
                  ext_y=None, ext_end='base'):
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
    slope = (z1 - z0) / (y1 - y0)
    zline = lambda y: z0 + (y - y0) * slope        # noqa: E731
    ym = 0.5 * (y0 + y1)
    sides = ('lo', 'hi') if rail_side == 'both' else (rail_side,)
    ext_side = sides[-1]                           # outer side gets the stub
    for side in sides:
        cx = (x0 + st * 0.5) if side == 'lo' else (x1 - st * 0.5)
        stations = [(y1, zline(y1) + rail_h), (ym, zline(ym) + rail_h),
                    (y0, zline(y0) + rail_h)]
        if ext_y is not None and side == ext_side:  # level stub at the
            if ext_end == 'top':                    # landing-adjacent end
                stations.insert(0, (ext_y, z1 + rail_h))
            else:
                stations.append((ext_y, z0 + rail_h))
        _bar(sh, stations, 0.045, 0.045, M_METAL, axis='y', center=cx)
        n_po = max(2, int(round(abs(y1 - y0) / 1.1)))
        for k in range(n_po + 1):                   # raked-run posts
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
    # ascends back -y beside A. Raked rails stop at the pitch break with a
    # 100 mm level stub that buries inside the landing's mitred U rail.
    _steel_flight(sh, 0.0, w, 0.0, run1, 0.0, h1, rail_h, 'lo',
                  ext_y=run1 + 0.10, ext_end='top')
    _steel_flight(sh, xB0, xB0 + w, run1 + 0.02, run1 + 0.02 - run_t * n2,
                  h1, H, rail_h,
                  'both' if p.get("double_rail", True) else 'hi',
                  ext_y=run1 + 0.10, ext_end='base')

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

    # landing rail: ONE mitred U (side run -> far edge -> side run) via
    # _plan_bar, slightly larger section than the flight rails so their
    # level stubs bury inside it with no coplanar faces. Corner posts under
    # the mitres + a mid post on the far edge.
    ztop = h1 + rail_h
    cxA, cxB = 0.03, lx1 - 0.03                    # flight rail centrelines
    upath = [(cxA, run1 + 0.06), (cxA, ly1 - 0.024),
             (lx1 * 0.5, ly1 - 0.024), (cxB, ly1 - 0.024),
             (cxB, run1 + 0.06)]
    _plan_bar(sh, upath, ztop + 0.001, 0.049, 0.049, M_METAL)
    for (px, py) in ((cxA, ly1 - 0.055), (cxB, ly1 - 0.055),
                     (lx1 * 0.5, ly1 - 0.055)):
        _box(sh, (px - 0.02, py - 0.02, h1 + 0.02),
             (px + 0.02, py + 0.02, ztop - 0.014), M_METAL)
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
# rpg-oyu3 -- masonry arched doorway with keystone (0e)
# ---------------------------------------------------------------------------
DOORWAY_SPEC = [
    dict(name="width", type='FLOAT', default=1.3, min=0.9, max=2.6,
         unit='LENGTH', desc="Clear opening width"),
    dict(name="height", type='FLOAT', default=2.6, min=2.0, max=4.0,
         unit='LENGTH', desc="Total height to the arch crown"),
    dict(name="voussoirs", type='INT', default=9, min=5, max=15,
         desc="Arch stones (odd puts the keystone on centre)"),
    dict(name="keystone", type='BOOL', default=True),
    dict(name="fill", type='ENUM', default='door',
         items=('door', 'open', 'boarded'),
         desc="Opening below the spring line"),
    dict(name="tympanum", type='ENUM', default='fanlight',
         items=('fanlight', 'sunburst', 'louver', 'solid'),
         desc="Arch infill above the spring (ignored when boarded)"),
]


def _voussoir(sh, cx, cz, r0, r1, th0, th1, y0, y1, mat):
    """One radial arch-stone prism. Extrusion along y is parallel, so every
    face (front/back sectors, radial sides, inner/outer sweeps) spans two
    parallel lines -> all 6 faces are planar quads."""
    def pt(r, th):
        return (cx + r * math.cos(th), cz + r * math.sin(th))
    (xa0, za0), (xa1, za1) = pt(r0, th0), pt(r0, th1)
    (xb0, zb0), (xb1, zb1) = pt(r1, th0), pt(r1, th1)
    q = sh.quad
    q((xa0, y0, za0), (xa1, y0, za1), (xb1, y0, zb1), (xb0, y0, zb0), mat)
    q((xa0, y1, za0), (xb0, y1, zb0), (xb1, y1, zb1), (xa1, y1, za1), mat)
    q((xa0, y0, za0), (xb0, y0, zb0), (xb0, y1, zb0), (xa0, y1, za0), mat)
    q((xa1, y0, za1), (xa1, y1, za1), (xb1, y1, zb1), (xb1, y0, zb1), mat)
    q((xa0, y0, za0), (xa0, y1, za0), (xa1, y1, za1), (xa1, y0, za1), mat)
    q((xb0, y0, zb0), (xb1, y0, zb1), (xb1, y1, zb1), (xb0, y1, zb0), mat)


def _half_disc(sh, cx, cz, R, y, mat, M=8):
    """Filled half-disc (flat, at plane y) tiled ENTIRELY in quads: a 4x2
    inner grid over the half-width chord region plus an M-segment ring out
    to the arc -- the ring path has M+1 points matching the arc 1:1, so
    there is no degenerate fan and no ngons."""
    ir = R * 0.5
    xs_c = [cx - ir, cx - ir * 0.5, cx, cx + ir * 0.5, cx + ir]
    zs2 = cz + ir * 0.5
    q = sh.quad
    for k in range(4):
        q((xs_c[k], y, cz), (xs_c[k + 1], y, cz),
          (xs_c[k + 1], y, zs2), (xs_c[k], y, zs2), mat)
        q((xs_c[k], y, zs2), (xs_c[k + 1], y, zs2),
          (xs_c[k + 1], y, cz + ir), (xs_c[k], y, cz + ir), mat)
    path = [(cx + ir, cz), (cx + ir, zs2), (cx + ir, cz + ir),
            (cx + ir * 0.5, cz + ir), (cx, cz + ir),
            (cx - ir * 0.5, cz + ir), (cx - ir, cz + ir),
            (cx - ir, zs2), (cx - ir, cz)]
    for k in range(M):
        tha, thb = math.pi * k / M, math.pi * (k + 1) / M
        ax, az = cx + R * math.cos(tha), cz + R * math.sin(tha)
        bx, bz = cx + R * math.cos(thb), cz + R * math.sin(thb)
        (ix, iz), (jx, jz) = path[k], path[k + 1]
        q((ax, y, az), (bx, y, bz), (jx, y, jz), (ix, y, iz), mat)


def _plank(sh, cx_, cz_, ang, L, wdt, y0, y1, mat):
    """Oriented plank prism in the xz plane: centre, angle, length, width,
    extruded y0..y1. Rectangle cross-section swept along y -> 6 planar
    quads."""
    ux, uz = math.cos(ang), math.sin(ang)
    px, pz = -uz, ux
    h, g = L * 0.5, wdt * 0.5
    c = [(cx_ - ux * h - px * g, cz_ - uz * h - pz * g),
         (cx_ + ux * h - px * g, cz_ + uz * h - pz * g),
         (cx_ + ux * h + px * g, cz_ + uz * h + pz * g),
         (cx_ - ux * h + px * g, cz_ - uz * h + pz * g)]
    q = sh.quad
    q((c[0][0], y0, c[0][1]), (c[1][0], y0, c[1][1]),
      (c[2][0], y0, c[2][1]), (c[3][0], y0, c[3][1]), mat)   # front
    q((c[0][0], y1, c[0][1]), (c[3][0], y1, c[3][1]),
      (c[2][0], y1, c[2][1]), (c[1][0], y1, c[1][1]), mat)   # back
    for k in range(4):                             # sides
        (ax, az), (bx, bz) = c[k], c[(k + 1) % 4]
        q((ax, y0, az), (ax, y1, az), (bx, y1, bz), (bx, y0, bz), mat)


def _quoin_jamb(sh, x0, x1, zs, t, proud_a, proud_b, nb, mat):
    """ONE welded quoin jamb solid: the front face steps between two proud
    depths per course as a vertical accordion (front quads + horizontal step
    ledges). Side faces and end caps are SPLIT at the shallow depth line so
    every ledge corner welds onto a real vert (an unsplit side face carried
    them mid-edge -> T-junctions)."""
    zh = zs / nb
    q = sh.quad
    deep = min(proud_a, proud_b)                   # more proud (further -y)
    shal = max(proud_a, proud_b)
    deep_, shal_ = min(proud_a, proud_b), max(proud_a, proud_b)
    yc = [deep_ if ((nb - 1 - k) % 2 == 0) else shal_ for k in range(nb)]

    def side(xx, flip, ya, yb, z0, z1):
        a, b = (xx, ya, z0), (xx, yb, z0)
        c, d = (xx, yb, z1), (xx, ya, z1)
        if flip:
            q(a, b, c, d, mat)
        else:
            q(a, d, c, b, mat)

    for k in range(nb):
        z0, z1 = k * zh, (k + 1) * zh
        y = yc[k]
        q((x0, y, z0), (x1, y, z0), (x1, y, z1), (x0, y, z1), mat)  # front
        if k + 1 < nb:                             # step ledge to next course
            yn = yc[k + 1]
            lo, hi = (y, yn) if y < yn else (yn, y)
            a, b = (x0, lo, z1), (x1, lo, z1)
            c, d = (x1, hi, z1), (x0, hi, z1)
            if yn > y:
                q(a, b, c, d, mat)
            else:
                q(a, d, c, b, mat)
        for xx, flip in ((x0, False), (x1, True)):  # sides, split at shal
            if y < shal - 1e-9:
                side(xx, flip, y, shal, z0, z1)
            side(xx, flip, shal, t, z0, z1)
        q((x0, t, z0), (x1, t, z0), (x1, t, z1), (x0, t, z1), mat)  # back
    for (zc, first, top) in ((0.0, yc[0], False), (zs, yc[nb - 1], True)):
        spans = ([(first, shal)] if first < shal - 1e-9 else []) + \
                [(shal, t)]
        for (ya, yb) in spans:                     # end caps, same splits
            a, b = (x0, ya, zc), (x1, ya, zc)
            c, d = (x1, yb, zc), (x0, yb, zc)
            if top:
                q(a, b, c, d, mat)
            else:
                q(a, d, c, b, mat)


def build_arched_doorway(p, rng):
    """Masonry arched doorway: opening x in [0,w] at wall plane y=0 (reveal
    projects +y, surround proud -y). The arch is ONE welded ring sweep
    (voussoir joints are texture, not geometry) over-swung 5 degrees past
    the spring line so its ends embed INSIDE the jamb tops; the keystone is
    a proud radial solid interpenetrating the ring; each jamb is a single
    welded quoin-stepped solid. Nothing floats, nothing shares a plane."""
    w, H = p["width"], p["height"]
    nv = p["voussoirs"]
    t = 0.30                                       # reveal depth
    R = w * 0.5
    zs = H - R                                     # spring line
    cx = w * 0.5
    sh = _Shell()
    sh.tag = 'doors'

    jw = 0.26
    nb = max(3, int(round(zs / 0.45)))
    _quoin_jamb(sh, -jw, -0.002, zs, t, -0.085, -0.045, nb, M_TRIM)
    _quoin_jamb(sh, w + 0.002, w + jw, zs, t, -0.085, -0.045, nb, M_TRIM)
    # threshold runs past the jamb faces (flush ends were coplanar).
    _box(sh, (-jw - 0.02, -0.06, -0.10), (w + jw + 0.02, t + 0.01, 0.02),
         M_CONCRETE)

    # arch ring: welded tube(s), ring station per voussoir (even density),
    # swung 5 deg past each spring so the caps hide inside the jambs. With a
    # keystone the ring is INTERRUPTED: two arcs whose upper ends tuck 12
    # mrad inside the keystone's radial faces (a continuous ring would run
    # straight through the stone).
    vd = jw - 0.045                                # ring depth < jamb width
    over = math.radians(2.0)
    if p["keystone"]:
        half = math.pi / max(nv, 5) * 0.75
        emb = 0.012
        n_half = max(2, nv // 2)
        _arc_tube(sh, cx, zs, R + 0.002, R + vd, -0.055, t, n_half, M_TRIM,
                  th_a=-over, th_b=math.pi * 0.5 - half + emb)
        _arc_tube(sh, cx, zs, R + 0.002, R + vd, -0.055, t, n_half, M_TRIM,
                  th_a=math.pi * 0.5 + half - emb, th_b=math.pi + over)
        _voussoir(sh, cx, zs, R - 0.01, R + vd + 0.09,
                  math.pi * 0.5 - half, math.pi * 0.5 + half,
                  -0.13, t - 0.01, M_TRIM)
    else:
        _arc_tube(sh, cx, zs, R + 0.002, R + vd, -0.055, t, nv, M_TRIM,
                  th_a=-over, th_b=math.pi + over)

    if p["fill"] == 'boarded':
        # boarding that would actually hold: thick near-horizontal boards
        # nailed across the opening with BOTH ends landing ON the surround
        # (in the arch zone the reach follows the ring, never past it),
        # irregular spacing/tilt, and 1-2 proud diagonal braces in front.
        def half_open(z):
            if z <= zs:
                return w * 0.5
            return math.sqrt(max(R * R - (z - zs) * (z - zs), 0.0))

        z_key = zs + R - 0.34                  # keystone territory starts
        def fit_plank(zc, ang, xoff, wdt, y0, y1, mat):
            L = (w + 0.34) / max(0.4, math.cos(ang))
            for _ in range(14):                    # shrink until both ends
                fits = True                        # land on the surround
                for sgn in (-1.0, 1.0):
                    ex = abs(xoff + sgn * math.cos(ang) * L * 0.5)
                    ez = zc + sgn * math.sin(ang) * L * 0.5
                    if ex > half_open(ez) + 0.17 or ez > zs + R - 0.10:
                        fits = False
                if fits:
                    break
                L *= 0.92
            if L > 0.5:
                # a board that reaches the keystone zone RESTS on the
                # keystone instead of intersecting it: displace it outward.
                if zc + abs(math.sin(ang)) * L * 0.5 > z_key and y0 > -0.13:
                    d = -0.134 - y0
                    y0, y1 = y0 + d, y1 + d
                _plank(sh, cx + xoff, zc, ang, L, wdt, y0, y1, mat)

        npl = max(5, int(round(H / 0.30)))
        zc = 0.20
        k = 0
        while zc < zs + R - 0.25:
            ang = math.radians(rng.uniform(2.0, 10.0)) *                 (1 if rng.random() < 0.5 else -1)
            fit_plank(zc + rng.uniform(-0.05, 0.05), ang,
                      rng.uniform(-0.05, 0.05), rng.uniform(0.20, 0.29),
                      -0.088 - 0.001 * (k % 3), -0.053 - 0.001 * (k % 3),
                      M_PLYWOOD)
            zc += rng.uniform(0.24, 0.42)          # irregular courses
            k += 1
        for sgn in ((1.0, -1.0) if w > 1.2 else (1.0,)):   # diagonal braces
            ang = sgn * math.radians(rng.uniform(24.0, 34.0))
            fit_plank(zs * rng.uniform(0.45, 0.6), ang,
                      rng.uniform(-0.06, 0.06), rng.uniform(0.22, 0.28),
                      -0.134, -0.098, M_PLYWOOD)
    elif p["fill"] == 'door':
        # paneled door leaf set mid-reveal, up to the spring line.
        yd = t * 0.55                              # leaf plane in the reveal
        zt = zs - 0.02
        _box(sh, (0.015, yd, 0.0), (w - 0.015, yd + 0.05, zt), M_PLYWOOD)
        stiles = [0.015, w - 0.015 - 0.09]
        if w > 1.6:
            stiles.append(w * 0.5 - 0.045)
        for sx in stiles:                          # proud stiles
            _box(sh, (sx, yd - 0.014, 0.008), (sx + 0.09, yd + 0.01,
                 zt - 0.008), M_PLYWOOD)
        for (za, zb) in ((0.0, 0.16), (0.92, 1.04), (zt - 0.13, zt)):
            _box(sh, (0.02, yd - 0.012, za + 0.006),
                 (w - 0.02, yd + 0.008, zb - 0.006), M_PLYWOOD)  # rails
        _box(sh, (0.01, yd - 0.006, zs - 0.045), (w - 0.01, yd + 0.056,
             zs - 0.012), M_PLYWOOD)               # transom bar over the leaf

    if p["fill"] != 'boarded':                     # tympanum templates
        yd = t * 0.55
        ty = p["tympanum"]
        if ty == 'fanlight':                       # radial glazing
            _half_disc(sh, cx, zs, R - 0.015, yd + 0.03, M_GLASS)
            for k in range(1, 4):
                th = math.pi * k / 4.0
                _voussoir(sh, cx, zs, 0.04, R - 0.02, th - 0.010,
                          th + 0.010, yd - 0.005, yd + 0.055, M_PLYWOOD)
            _box(sh, (cx - 0.06, yd - 0.005, zs - 0.015),
                 (cx + 0.06, yd + 0.055, zs + 0.06), M_PLYWOOD)
        elif ty == 'sunburst':                     # deco radiating fins
            _half_disc(sh, cx, zs, R - 0.012, yd + 0.045, M_STUCCO)
            nf = 9
            for k in range(nf):
                th = math.pi * (k + 0.5) / nf
                _voussoir(sh, cx, zs, 0.10, R - 0.045, th - 0.016,
                          th + 0.016, yd - 0.045, yd + 0.055, M_STUCCO)
            _voussoir(sh, cx, zs, 0.025, 0.13, 0.0, math.pi,
                      yd - 0.055, yd + 0.050, M_STUCCO)          # half hub
        elif ty == 'louver':                       # tilted vent blades
            zb2 = zs + 0.07
            while zb2 < zs + R - 0.10:
                # widest opening across the blade's z-range (its low edge),
                # +35 mm so the ends bury inside the reveal -- anchored,
                # never floating.
                hw = math.sqrt(max(R * R - (zb2 - 0.045 - zs) ** 2, 0.0))
                hw = min(hw + 0.035, R - 0.01)
                _sheared_box(sh, cx - hw, cx + hw, yd - 0.030, yd + 0.055,
                             zb2 - 0.045, zb2, 0.020, M_PLYWOOD)
                zb2 += 0.095
        else:                                      # 'solid' recessed infill
            _half_disc(sh, cx, zs, R - 0.012, yd + 0.05, M_STUCCO)
    return [sh.to_object("LA_Elem_Doorway", [_material(n) for n in _MATS])]


# ---------------------------------------------------------------------------
# rpg-pvk7 -- recessed streamline arched entry porch (0e)
# ---------------------------------------------------------------------------
PORCH_SPEC = [
    dict(name="width", type='FLOAT', default=1.6, min=1.1, max=2.6,
         unit='LENGTH', desc="Clear opening width"),
    dict(name="height", type='FLOAT', default=2.9, min=2.3, max=4.0,
         unit='LENGTH', desc="Total height to the arch crown"),
    dict(name="recess", type='FLOAT', default=1.1, min=0.5, max=2.0,
         unit='LENGTH', desc="Porch depth into the wall"),
    dict(name="bands", type='INT', default=3, min=1, max=5,
         desc="Concentric streamline trim bands"),
]


def _arc_tube(sh, cx, cz, r0, r1, y0, y1, n, mat, th_a=0.0, th_b=math.pi):
    """Half-annulus SOLID as one welded swept tube: a 4-vert (r x y) section
    ringed at every arc station, capped at both ends. Every face spans two
    parallel lines (extrusion along y / secant facets) -> planar quads; one
    manifold island (butted voussoir chunks shared radial faces -> non-
    manifold)."""
    rings = []
    for k in range(n + 1):
        th = th_a + (th_b - th_a) * k / n
        c, s = math.cos(th), math.sin(th)
        rings.append(((cx + r0 * c, y0, cz + r0 * s),
                      (cx + r1 * c, y0, cz + r1 * s),
                      (cx + r1 * c, y1, cz + r1 * s),
                      (cx + r0 * c, y1, cz + r0 * s)))
    (a0, b0, c0, d0) = rings[0]
    sh.quad(a0, b0, c0, d0, mat)                   # start cap
    for k in range(n):
        (a1, b1, c1, d1) = rings[k]
        (a2, b2, c2, d2) = rings[k + 1]
        sh.quad(a1, a2, b2, b1, mat)               # front (y0)
        sh.quad(d1, c1, c2, d2, mat)               # back (y1)
        sh.quad(b1, b2, c2, c1, mat)               # outer sweep
        sh.quad(a1, d1, d2, a2, mat)               # inner sweep
    (an, bn, cn, dn) = rings[-1]
    sh.quad(an, dn, cn, bn, mat)                   # end cap


def build_entry_porch(p, rng):
    """Streamline-moderne recessed entry: opening x in [0,w] at wall plane
    y=0, recess extends +y. Ships the recess interior (jamb reveals, barrel
    ceiling, back wall with door, floor + stoop) and the applied surround
    (concentric welded arc-tube bands stepping down in proudness, continued
    as stacked pilaster boxes below the spring line). The assembly provides
    the surrounding wall. Every abutting face grid is SPLIT at its
    neighbours' stations (reveal at door-head height, floor and header at
    the door jambs / disc chords) so no vert lands mid-edge."""
    w, H = p["width"], p["height"]
    dp = p["recess"]
    R = w * 0.5
    zs = H - R
    cx = w * 0.5
    sh = _Shell()
    sh.tag = 'doors'
    M = 8                                          # arc segments

    def arc(r, k, n=M):
        th = math.pi * k / n
        return (cx + r * math.cos(th), zs + r * math.sin(th))

    dw = min(1.0, w - 0.3)
    dh = min(2.05, zs - 0.15)      # door head must stay below the spring
    dx0 = cx - dw * 0.5
    q = sh.quad

    # barrel ceiling (faces INTO the porch).
    for k in range(M):
        (x0, z0), (x1, z1) = arc(R, k), arc(R, k + 1)
        q((x0, 0.0, z0), (x0, dp, z0), (x1, dp, z1), (x1, 0.0, z1), M_STUCCO)
    # jamb reveals, split at the door-head line (the back-wall flank corner
    # vert would otherwise sit mid-edge).
    for xx, flip in ((0.0, False), (w, True)):
        for (za, zb) in ((0.0, dh), (dh, zs)):
            a, b = (xx, 0.0, za), (xx, dp, za)
            c, d = (xx, dp, zb), (xx, 0.0, zb)
            if flip:
                q(a, b, c, d, M_STUCCO)
            else:
                q(a, d, c, b, M_STUCCO)
    # porch floor, split at the door jambs (back-wall flank bottom corners).
    for (xa, xb) in ((0.0, dx0), (dx0, dx0 + dw), (dx0 + dw, w)):
        q((xa, 0.0, 0.0), (xb, 0.0, 0.0), (xb, dp, 0.0), (xa, dp, 0.0),
          M_CONCRETE)

    # back wall at y=dp. The disc chord half-width DERIVES from the door
    # (ir = dw/2) so the door jambs, header columns, disc grid and ring path
    # all share one station set -- alignment by construction, no mid-edge
    # verts anywhere.
    ir = dw * 0.5
    xs_c = [cx - ir, cx - ir * 0.5, cx, cx + ir * 0.5, cx + ir]
    q((0.0, dp, 0.0), (dx0, dp, 0.0), (dx0, dp, dh), (0.0, dp, dh), M_STUCCO)
    q((dx0 + dw, dp, 0.0), (w, dp, 0.0), (w, dp, dh), (dx0 + dw, dp, dh),
      M_STUCCO)
    hdr_xs = sorted(set([0.0, dx0, dx0 + dw, w] + xs_c))   # header columns
    # (split at BOTH the disc chords and the door jambs, so the flank top
    # corners + disc verts all weld onto real header verts)
    for k in range(len(hdr_xs) - 1):
        q((hdr_xs[k], dp, dh), (hdr_xs[k + 1], dp, dh),
          (hdr_xs[k + 1], dp, zs), (hdr_xs[k], dp, zs), M_STUCCO)
    # arch disc: 4x2 inner grid + an 8-segment ring out to the arc. The ring
    # path has 9 points matching the 8 arc segments 1:1 (no degenerate fan).
    zs2 = zs + ir * 0.5
    for k in range(4):                             # inner grid, 2 rows
        q((xs_c[k], dp, zs), (xs_c[k + 1], dp, zs),
          (xs_c[k + 1], dp, zs2), (xs_c[k], dp, zs2), M_STUCCO)
        q((xs_c[k], dp, zs2), (xs_c[k + 1], dp, zs2),
          (xs_c[k + 1], dp, zs + ir), (xs_c[k], dp, zs + ir), M_STUCCO)
    path = [(cx + ir, zs), (cx + ir, zs2), (cx + ir, zs + ir),
            (cx + ir * 0.5, zs + ir), (cx, zs + ir), (cx - ir * 0.5, zs + ir),
            (cx - ir, zs + ir), (cx - ir, zs2), (cx - ir, zs)]
    for k in range(M):
        (ax, az), (bx, bz) = arc(R, k), arc(R, k + 1)
        (ix, iz), (jx, jz) = path[k], path[k + 1]
        # the k=0 / k=M-1 quads own the long z=zs chord edges (arc end ->
        # path corner); split them at the door-jamb station when it falls
        # inside, so the header-column verts weld instead of T-junctioning.
        split_x = None
        if k == 0 and ix < dx0 + dw < ax:
            split_x = dx0 + dw
        elif k == M - 1 and bx < dx0 < jx:
            split_x = dx0
        if split_x is None:
            q((ax, dp, az), (bx, dp, bz), (jx, dp, jz), (ix, dp, iz),
              M_STUCCO)
        else:
            lo, hi = (ix, ax) if k == 0 else (bx, jx)
            tt = (split_x - lo) / (hi - lo)
            if k == 0:
                mx_, mz_ = ix + (bx - ix) * tt, iz + (bz - iz) * tt
                q((split_x, dp, zs), (ax, dp, az), (bx, dp, bz),
                  (mx_, dp, mz_), M_STUCCO)
                q((ix, dp, iz), (split_x, dp, zs), (mx_, dp, mz_),
                  (jx, dp, jz), M_STUCCO)
            else:
                mx_, mz_ = ax + (jx - ax) * tt, az + (jz - az) * tt
                q((ax, dp, az), (bx, dp, bz), (split_x, dp, zs),
                  (mx_, dp, mz_), M_STUCCO)
                q((mx_, dp, mz_), (split_x, dp, zs), (jx, dp, jz),
                  (ix, dp, iz), M_STUCCO)
    _box(sh, (dx0 + 0.02, dp - 0.045, 0.01), (dx0 + dw - 0.02, dp - 0.005,
         dh - 0.02), M_PLYWOOD)                    # door leaf (proud)
    for sx in (dx0 + 0.03, dx0 + dw - 0.10):       # proud stiles
        _box(sh, (sx, dp - 0.058, 0.02), (sx + 0.07, dp - 0.04, dh - 0.03),
             M_PLYWOOD)
    for (za, zb) in ((0.02, 0.15), (0.95, 1.05), (dh - 0.14, dh - 0.03)):
        _box(sh, (dx0 + 0.035, dp - 0.056, za), (dx0 + dw - 0.035,
             dp - 0.042, zb), M_PLYWOOD)           # rails

    # streamline bands: welded arc tubes + stacked pilasters below (tops
    # tucked 20 mm past the spring so the tube caps hide inside them).
    bw = 0.12
    for i in range(p["bands"]):
        proud = 0.10 - i * 0.025
        r0 = R + 0.02 + i * bw
        r1 = r0 + bw - 0.015
        _arc_tube(sh, cx, zs, r0, r1, -proud, 0.02, M, M_STUCCO)
        xl0 = -(0.02 + (i + 1) * bw) + 0.015 - 0.012
        xl1 = -(0.02 + i * bw) + 0.012
        # nested pilasters overlap in x: STAGGER their back/top/bottom
        # planes per band so no two boxes (or the band tube) share a plane.
        yb = 0.031 + 0.004 * i
        zt2 = zs + 0.02 + 0.004 * i
        zb2 = -0.002 * (i + 1)
        for (x0, x1) in ((xl0, xl1), (w - xl1, w - xl0)):
            _box(sh, (x0, -proud - 0.010, zb2), (x1, yb, zt2), M_STUCCO)
    _box(sh, (-0.25, -0.42, -0.001), (w + 0.25, dp * 0.5, 0.14),
         M_CONCRETE)                               # stoop step
    return [sh.to_object("LA_Elem_Porch", [_material(n) for n in _MATS])]


# ---------------------------------------------------------------------------
# rpg-qlur -- flat tar roof: parapet + mechanical penthouses (0f)
# ---------------------------------------------------------------------------
ROOF_SPEC = [
    dict(name="width", type='FLOAT', default=8.0, min=3.0, max=20.0,
         unit='LENGTH'),
    dict(name="depth", type='FLOAT', default=6.0, min=3.0, max=16.0,
         unit='LENGTH'),
    dict(name="parapet", type='FLOAT', default=0.75, min=0.3, max=1.3,
         unit='LENGTH', desc="Parapet height above the roof plane"),
    dict(name="bulkhead", type='BOOL', default=True,
         desc="Stair bulkhead penthouse"),
    dict(name="ac_units", type='INT', default=2, min=0, max=6),
    dict(name="vents", type='INT', default=3, min=0, max=8),
]


def build_tar_roof(p, rng):
    """Flat tar roof cap for a W x D footprint (roof plane z=0): tar slab,
    MITRED parapet ring (welded tube, trapezoid corner faces -- no butted
    corner boxes), proud cap ring offset so no plane is shared, and seeded
    mechanical clutter (stair bulkhead wedge, AC units, vent stacks, a pipe)
    placed on a shuffled grid inside the parapet margin."""
    W, D = p["width"], p["depth"]
    ph = p["parapet"]
    t = 0.16                                       # parapet thickness
    sh = _Shell(recalc=True)
    sh.tag = 'parapet'
    _box(sh, (t + 0.005, t + 0.005, -0.12), (W - t - 0.005, D - t - 0.005,
         0.0), M_ASPHALT)                          # tar slab

    def ring(o0, o1, i0, i1, z0, z1, mat):
        """Mitred rectangular ring tube: outer rect o, inner rect i."""
        oc = [(o0[0], o0[1]), (o1[0], o0[1]), (o1[0], o1[1]), (o0[0], o1[1])]
        ic = [(i0[0], i0[1]), (i1[0], i0[1]), (i1[0], i1[1]), (i0[0], i1[1])]
        for z, up in ((z1, True), (z0, False)):    # top + bottom rings
            for k in range(4):
                (oxa, oya), (oxb, oyb) = oc[k], oc[(k + 1) % 4]
                (ixa, iya), (ixb, iyb) = ic[k], ic[(k + 1) % 4]
                a, b = (oxa, oya, z), (oxb, oyb, z)
                c, d = (ixb, iyb, z), (ixa, iya, z)
                if up:
                    sh.quad(a, b, c, d, mat)
                else:
                    sh.quad(a, d, c, b, mat)
        for k in range(4):                         # outer + inner walls
            (oxa, oya), (oxb, oyb) = oc[k], oc[(k + 1) % 4]
            (ixa, iya), (ixb, iyb) = ic[k], ic[(k + 1) % 4]
            sh.quad((oxa, oya, z0), (oxb, oyb, z0), (oxb, oyb, z1),
                    (oxa, oya, z1), mat)
            sh.quad((ixa, iya, z0), (ixa, iya, z1), (ixb, iyb, z1),
                    (ixb, iyb, z0), mat)

    ring((0.0, 0.0), (W, D), (t, t), (W - t, D - t), -0.06, ph, M_STUCCO)
    ring((-0.025, -0.025), (W + 0.025, D + 0.025),
         (t - 0.025, t - 0.025), (W - t + 0.025, D - t + 0.025),
         ph - 0.005, ph + 0.055, M_TRIM)           # proud cap

    # mechanical clutter on a shuffled placement grid.
    m = t + 0.45
    cols, rows = 3, 2
    slots = [(m + (W - 2 * m) * (i + 0.5) / cols,
              m + (D - 2 * m) * (j + 0.5) / rows)
             for i in range(cols) for j in range(rows)]
    rng.shuffle(slots)
    # the bulkhead DOOR (on the -y face) needs a clear egress path: nothing
    # may be placed in the rectangle in front of it.
    clear = None
    if p["bulkhead"] and slots:
        (bx, by) = slots.pop()
        bw2, bd2 = min(1.6, W * 0.3), min(1.2, D * 0.3)
        _wedge_box(sh, bx - bw2 * 0.5, bx + bw2 * 0.5, by - bd2 * 0.5,
                   by + bd2 * 0.5, -0.02, 2.15, 1.9, M_STUCCO)
        _box(sh, (bx - 0.35, by - bd2 * 0.5 - 0.035, 0.0),
             (bx + 0.35, by - bd2 * 0.5 + 0.01, 1.85), M_METAL)   # door
        clear = (bx - 0.9, bx + 0.9, by - bd2 * 0.5 - 1.6, by - bd2 * 0.5)
        slots = [(sx, sy) for (sx, sy) in slots
                 if not (clear[0] - 0.45 < sx < clear[1] + 0.45 and
                         clear[2] - 0.45 < sy < clear[3] + 0.45)]

    def blocked(px, py, r):
        return clear is not None and (clear[0] - r < px < clear[1] + r and
                                      clear[2] - r < py < clear[3] + r)

    for _ in range(p["ac_units"]):
        if not slots:
            break
        (ax, ay) = slots.pop()
        aw = rng.uniform(0.5, 0.85)
        _box(sh, (ax - aw * 0.5, ay - aw * 0.5, -0.02),
             (ax + aw * 0.5, ay + aw * 0.5, 0.55), M_METAL)
        _box(sh, (ax - aw * 0.35, ay - aw * 0.35, 0.53),
             (ax + aw * 0.35, ay + aw * 0.35, 0.60), M_SHUTTER)   # fan grille
    for _ in range(p["vents"]):
        vx = rng.uniform(m, W - m)
        vy = rng.uniform(m, D - m)
        for _try in range(8):                      # keep the egress clear
            if not blocked(vx, vy, 0.15):
                break
            vx, vy = rng.uniform(m, W - m), rng.uniform(m, D - m)
        if blocked(vx, vy, 0.15):
            continue
        vh = rng.uniform(0.35, 0.9)
        _box(sh, (vx - 0.07, vy - 0.07, -0.02), (vx + 0.07, vy + 0.07, vh),
             M_METAL)
        _box(sh, (vx - 0.11, vy - 0.11, vh - 0.005), (vx + 0.11, vy + 0.11,
             vh + 0.05), M_METAL)                  # rain cap
    px_, py_ = rng.uniform(m, W - m), rng.uniform(m, D - m)
    for _try in range(8):                          # keep the egress clear
        if not blocked(px_, py_, 0.1):
            break
        px_, py_ = rng.uniform(m, W - m), rng.uniform(m, D - m)
    if not blocked(px_, py_, 0.1):
        _vbar(sh, px_, py_, [(-0.02, 0.0), (1.3, 0.0)], 0.06, M_METAL)
    return [sh.to_object("LA_Elem_TarRoof", [_material(n) for n in _MATS])]


# ---------------------------------------------------------------------------
# rpg-imsr -- projecting canvas storefront awning (0g)
# ---------------------------------------------------------------------------
CANVAS_SPEC = [
    dict(name="width", type='FLOAT', default=3.0, min=1.2, max=6.0,
         unit='LENGTH'),
    dict(name="depth", type='FLOAT', default=1.0, min=0.5, max=1.8,
         unit='LENGTH', desc="Projection from the wall"),
    dict(name="drop", type='FLOAT', default=0.55, min=0.2, max=1.0,
         unit='LENGTH', desc="Fall from wall to front edge"),
    dict(name="valance", type='FLOAT', default=0.28, min=0.0, max=0.5,
         unit='LENGTH', desc="Hanging front skirt height"),
    dict(name="stripes", type='BOOL', default=True),
]


def build_canvas_awning(p, rng):
    """Striped canvas storefront awning: wall plane y=0 (mount top z=0),
    fabric sloping to (-depth, -drop). Fabric is one welded sheet segmented
    per stripe (even density, stripes alternate the sign palette); the
    valance hangs at the front edge with a scalloped bottom (two planar
    quads per stripe, mid-point dipped). Side gussets close the triangles;
    sheared side arms + a front tube bar carry it."""
    W, D, drop = p["width"], p["depth"], p["drop"]
    vh = p["valance"]
    sh = _Shell()
    sh.tag = 'awnings'
    ns = max(2, int(round(W / 0.42)))
    xs = [W * k / ns for k in range(ns + 1)]
    for k in range(ns):                            # sloped fabric, per stripe
        mat = (M_SIGN_A if (k % 2) else M_TRIM) if p["stripes"] else M_SIGN_A
        xm = (xs[k] + xs[k + 1]) * 0.5
        # two columns per stripe so the fabric's front-edge verts match the
        # valance scallop mid-verts (mid-vert on a long edge = T-junction).
        for (xa, xb) in ((xs[k], xm), (xm, xs[k + 1])):
            sh.quad((xa, 0.0, 0.0), (xb, 0.0, 0.0),
                    (xb, -D, -drop), (xa, -D, -drop), mat)
    if vh > 0.01:
        for k in range(ns):                        # scalloped valance
            mat = (M_SIGN_A if (k % 2) else M_TRIM) if p["stripes"] \
                else M_SIGN_A
            xm = (xs[k] + xs[k + 1]) * 0.5
            zt = -drop
            sh.quad((xs[k], -D, zt), (xm, -D, zt), (xm, -D, zt - vh),
                    (xs[k], -D, zt - vh * 0.72), mat)
            sh.quad((xm, -D, zt), (xs[k + 1], -D, zt),
                    (xs[k + 1], -D, zt - vh * 0.72), (xm, -D, zt - vh), mat)
    for xx, flip in ((0.012, False), (W - 0.012, True)):   # side gussets
        a, b = (xx, -0.005, -0.02), (xx, -D + 0.01, -drop - 0.01)
        c, d = (xx, -D + 0.01, -drop - vh * 0.5), (xx, -0.005, -0.55 - vh * 0.3)
        if flip:
            sh.quad(a, b, c, d, M_SIGN_A)
        else:
            sh.quad(a, d, c, b, M_SIGN_A)
    for (x0, x1) in ((0.0, 0.04), (W - 0.04, W)):  # sheared side arms
        _sheared_box(sh, x0, x1, -0.005, -D + 0.03, -0.035, -drop - 0.012,
                     0.05, M_METAL)
    _bar(sh, [(x, -drop - 0.012) for x in xs], 0.045, 0.045, M_METAL,
         axis='x', center=-D + 0.035)              # front tube, per-stripe rings
    return [sh.to_object("LA_Elem_CanvasAwning",
                         [_material(n) for n in _MATS])]


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
params.register_tool(idname="la_arched_doorway", label="Arched Doorway",
                     family="Elements", build=build_arched_doorway,
                     spec=DOORWAY_SPEC)
params.register_tool(idname="la_entry_porch", label="Streamline Entry Porch",
                     family="Elements", build=build_entry_porch,
                     spec=PORCH_SPEC)
params.register_tool(idname="la_tar_roof", label="Tar Roof + Parapet",
                     family="Elements", build=build_tar_roof,
                     spec=ROOF_SPEC)
params.register_tool(idname="la_canvas_awning", label="Canvas Awning",
                     family="Elements", build=build_canvas_awning,
                     spec=CANVAS_SPEC)
