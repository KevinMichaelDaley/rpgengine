"""Phase-1 ASSEMBLY: tiled checkerboard storefront bay (rpg-a1ep).

The first kit assembly: a ground-floor commercial storefront bay -- ceramic
checkerboard tile piers + bulkhead, aluminium-framed display glazing with a
transom band, and a glazed entry door. Ships TWO ways:

  emit_storefront_bay(...)   emitter into an EXISTING shell/wall frame (the
                             mini-mall dresses its open tenant bays with it);
                             wall plane y=yw facing -y, bay span x0..x1.
  la_storefront_bay          standalone registered tool (family Assembly)
                             with its own backing wall + fascia + glass.

Checkerboard tiles are shallow closed boxes on a backing face, 2 mm grout
gaps, alternating colour AND proudness (adjacent tiles never share a plane).
Quality bar as elements.py: 100% quads, welded islands, interpenetration
>= 5 mm, no coincident planes, no T-junctions.
"""
from .. import params
from ..geom import (
    _MATS, _Shell, _box, _material,
    M_GLASS, M_METAL, M_SIGN_B, M_SIGN_C, M_TRIM,
)


def _tile_checker(sh, x0, x1, z0, z1, tm, y_face, mat_a, mat_b):
    """Checkerboard ceramic tile field over [x0,x1]x[z0,z1]: shallow closed
    tile boxes on the y_face plane, 2 mm grout gaps, alternating colour and
    proudness (10 vs 16 mm) so neighbours never share a plane."""
    cols = max(1, int(round((x1 - x0) / tm)))
    rows = max(1, int(round((z1 - z0) / tm)))
    tw, th = (x1 - x0) / cols, (z1 - z0) / rows
    for i in range(cols):
        for j in range(rows):
            proud = 0.016 if ((i + j) % 2) else 0.010
            mat = mat_a if ((i + j) % 2) else mat_b
            _box(sh, (x0 + i * tw + 0.002, y_face - proud,
                      z0 + j * th + 0.002),
                 (x0 + (i + 1) * tw - 0.002, y_face + 0.012,
                  z0 + (j + 1) * th - 0.002), mat)


def emit_storefront_bay(sh, x0, x1, yw, door, head, bulkhead=0.62,
                        tile=0.19, mullion_pitch=0.95, transom=2.2,
                        piers=(True, True), glass=False):
    """Dress one storefront bay on the wall plane y=yw (faces -y).

    @p door: (d0, d1) span of the entry within the bay, or None.
    @p head: storefront head height (glazing stops here).
    @p piers: (left, right) -- emit the checkerboard pier strip on that edge
    (the mini-mall emits left-only per bay so adjacent bays don't overlap).
    @p glass: emit the display/door glass too (standalone; the mini-mall
    already has glass in its wall grid). All add-ons are proud of the wall
    or embedded through it -- never coplanar with it."""
    keep = sh.tag
    sh.tag = 'storefront'
    pw = 0.18

    for side, on in (('L', piers[0]), ('R', piers[1])):
        if not on:
            continue
        px0 = (x0 - pw - 0.004) if side == 'L' else (x1 + 0.004)
        # backing strip (embedded 20 mm into the wall) + tile field.
        _box(sh, (px0, yw - 0.034, 0.0), (px0 + pw, yw + 0.03, head + 0.02),
             M_TRIM)
        _tile_checker(sh, px0 + 0.006, px0 + pw - 0.006, 0.03, head - 0.01,
                      tile, yw - 0.034, M_SIGN_B, M_TRIM)

    # bulkhead (kickplate zone) -- skips the door span.
    spans = [(x0 + 0.01, x1 - 0.01)]
    if door is not None:
        (d0, d1) = door
        spans = [(x0 + 0.01, d0 - 0.01), (d1 + 0.01, x1 - 0.01)]
        spans = [(a, b) for (a, b) in spans if b - a > 0.15]
    for (sa, sb) in spans:
        _box(sh, (sa, yw - 0.028, 0.0), (sb, yw + 0.03, bulkhead), M_TRIM)
        _tile_checker(sh, sa + 0.006, sb - 0.006, 0.05, bulkhead - 0.02,
                      tile, yw - 0.028, M_SIGN_B, M_TRIM)
        # sill channel capping the bulkhead.
        _box(sh, (sa - 0.004, yw - 0.040, bulkhead - 0.005),
             (sb + 0.004, yw + 0.03, bulkhead + 0.045), M_METAL)

    # aluminium glazing frame: verticals at the mullion pitch over the
    # glazing zone, a transom bar, and a head channel across the bay.
    for (sa, sb) in spans:
        n = max(1, int(round((sb - sa) / mullion_pitch)))
        for k in range(n + 1):
            mx = sa + (sb - sa) * k / n
            _box(sh, (mx - 0.026, yw - 0.036, bulkhead + 0.02),
                 (mx + 0.026, yw + 0.09, head - 0.02), M_METAL)
    _box(sh, (x0 + 0.005, yw - 0.036, transom - 0.028),
         (x1 - 0.005, yw + 0.09, transom + 0.028), M_METAL)   # transom bar
    _box(sh, (x0 - 0.006, yw - 0.042, head - 0.055),
         (x1 + 0.006, yw + 0.03, head + 0.015), M_METAL)      # head channel

    if door is not None:
        (d0, d1) = door
        # aluminium door frame + push bar + kick plate (+ glass leaf).
        for fx in (d0 + 0.004, d1 - 0.059):
            _box(sh, (fx, yw - 0.032, 0.0), (fx + 0.055, yw + 0.084,
                 transom - 0.03), M_METAL)
        _box(sh, (d0 + 0.044, yw - 0.030, transom - 0.085),
             (d1 - 0.044, yw + 0.077, transom - 0.032), M_METAL)  # header
        _box(sh, (d0 + 0.045, yw - 0.026, 0.98),
             (d1 - 0.045, yw + 0.012, 1.06), M_METAL)            # push bar
        _box(sh, (d0 + 0.045, yw - 0.024, 0.01),
             (d1 - 0.045, yw + 0.014, 0.30), M_METAL)            # kick plate
        if glass:
            sh.quad((d0 + 0.05, yw + 0.018, 0.32), (d1 - 0.05, yw + 0.018, 0.32),
                    (d1 - 0.05, yw + 0.018, transom - 0.09),
                    (d0 + 0.05, yw + 0.018, transom - 0.09), M_GLASS)

    if glass:                                       # display + transom glass
        for (sa, sb) in spans:
            n = max(1, int(round((sb - sa) / mullion_pitch)))
            for k in range(n):
                ga = sa + (sb - sa) * k / n
                gb = sa + (sb - sa) * (k + 1) / n
                sh.quad((ga, yw + 0.018, bulkhead + 0.02),
                        (gb, yw + 0.018, bulkhead + 0.02),
                        (gb, yw + 0.018, transom - 0.02),
                        (ga, yw + 0.018, transom - 0.02), M_GLASS)
                sh.quad((ga, yw + 0.018, transom + 0.02),
                        (gb, yw + 0.018, transom + 0.02),
                        (gb, yw + 0.018, head - 0.04),
                        (ga, yw + 0.018, head - 0.04), M_GLASS)
        if door is not None:
            (d0, d1) = door
            sh.quad((d0 + 0.02, yw + 0.018, transom + 0.02),
                    (d1 - 0.02, yw + 0.018, transom + 0.02),
                    (d1 - 0.02, yw + 0.018, head - 0.04),
                    (d0 + 0.02, yw + 0.018, head - 0.04), M_GLASS)
    sh.tag = keep


STOREFRONT_SPEC = [
    dict(name="width", type='FLOAT', default=4.2, min=2.4, max=8.0,
         unit='LENGTH', desc="Bay width between piers"),
    dict(name="head", type='FLOAT', default=3.0, min=2.6, max=4.0,
         unit='LENGTH', desc="Storefront head height"),
    dict(name="bulkhead", type='FLOAT', default=0.62, min=0.3, max=1.0,
         unit='LENGTH'),
    dict(name="tile", type='FLOAT', default=0.19, min=0.10, max=0.35,
         unit='LENGTH', desc="Checkerboard tile module"),
    dict(name="door_pos", type='ENUM', default='left',
         items=('left', 'center', 'right', 'none')),
]


def build_storefront_bay(p, rng):
    """Standalone storefront bay: backing wall + sign fascia + the full
    dressing with glass. Wall plane y=0 faces -y."""
    w, head = p["width"], p["head"]
    sh = _Shell()
    sh.tag = 'storefront'
    _box(sh, (-0.24, 0.024, -0.02), (w + 0.24, 0.20, head + 0.68), M_TRIM)
    sh.tag = 'signage'
    _box(sh, (-0.20, -0.012, head + 0.06), (w + 0.20, 0.03, head + 0.60),
         M_SIGN_C)                                  # fascia sign band
    dw = min(1.05, w * 0.35)
    door = None
    if p["door_pos"] == 'left':
        door = (0.10, 0.10 + dw)
    elif p["door_pos"] == 'right':
        door = (w - 0.10 - dw, w - 0.10)
    elif p["door_pos"] == 'center':
        door = (w * 0.5 - dw * 0.5, w * 0.5 + dw * 0.5)
    emit_storefront_bay(sh, 0.0, w, 0.0, door, head,
                        bulkhead=p["bulkhead"], tile=p["tile"], glass=True)
    return [sh.to_object("LA_Asm_Storefront", [_material(n) for n in _MATS])]


params.register_tool(idname="la_storefront_bay", label="Storefront Bay",
                     family="Assembly", build=build_storefront_bay,
                     spec=STOREFRONT_SPEC)
