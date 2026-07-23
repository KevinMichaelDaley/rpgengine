"""Phase-1 ASSEMBLY: storefront bay (rpg-a1ep) -- full variant kit.

A ground-floor (optionally two-story) commercial storefront bay composing
the Phase-0 elements. Ships two ways:

  emit_storefront_bay(...)   dressing emitter into an EXISTING wall frame
                             (the mini-mall dresses its open tenant bays);
                             wall plane y=yw facing -y, bay span x0..x1.
  la_storefront_bay          standalone tool (Add > Dystopian LA > Assembly)
                             with backing wall, facade treatment, signage,
                             roll-ups, awnings and an optional second story.

Parameter axes (all in the F9 redo panel):
  bulkhead_style  checker | tile | stucco | panel
  glazing         mullioned | plate  (plate = big butt-glazed sheets)
  rollup          none | closed | half | door  (door = roll-up service door)
  facade          stucco | ribbed | banded     (upper-wall treatment)
  sign            fascia | wrap | none   (wrap = tall band + side returns)
  blade / awning  Phase-0 blade sign + canvas/corrugated awning add-ons
  stories         1 | 2  (floor band, punched factory windows, cornice)

Quality bar as elements.py: 100% quads, welded islands, >= 5 mm embeds,
no coincident planes, no T-junctions; every dressing coordinate offset OFF
the host wall grid planes.
"""
from .. import params
from ..geom import (
    _MATS, _Shell, _box, _material,
    M_CONCRETE, M_GLASS, M_GYPSUM, M_METAL, M_SHUTTER, M_SIGN_A, M_SIGN_B,
    M_SIGN_C, M_STUCCO, M_TRIM,
)
from . import elements as el


# ---------------------------------------------------------------------------
# bulkhead styles
# ---------------------------------------------------------------------------
def _tile_checker(sh, x0, x1, z0, z1, tm, y_face, mat_a, mat_b):
    """Checkerboard tile field: shallow closed boxes, 2 mm grout, alternating
    colour AND proudness (neighbours never share a plane)."""
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


def _tile_field(sh, x0, x1, z0, z1, tm, y_face, mat):
    """Uniform running-bond tile field: single colour, rows offset half a
    module, all tiles at one proudness (grout gaps keep faces disjoint)."""
    rows = max(1, int(round((z1 - z0) / tm)))
    th = (z1 - z0) / rows
    for j in range(rows):
        off = (tm * 0.5) if (j % 2) else 0.0
        xa = x0
        first = tm - off if off else tm
        while xa < x1 - 0.004:
            xb = min(xa + (first if xa == x0 else tm), x1)
            first = tm
            _box(sh, (xa + 0.002, y_face - 0.012, z0 + j * th + 0.002),
                 (xb - 0.002, y_face + 0.012, z0 + (j + 1) * th - 0.002),
                 mat)
            xa = xb


def _bulkhead(sh, spans, yw, bh, style, tile):
    """Bulkhead band under the glazing, per style. Every style ships the
    backing box + metal sill channel; the face treatment varies."""
    for (sa, sb) in spans:
        _box(sh, (sa, yw - 0.028, 0.0), (sb, yw + 0.03, bh), M_TRIM)
        if style == 'checker':
            _tile_checker(sh, sa + 0.006, sb - 0.006, 0.05, bh - 0.02,
                          tile, yw - 0.028, M_SIGN_B, M_TRIM)
        elif style == 'tile':
            _tile_field(sh, sa + 0.006, sb - 0.006, 0.05, bh - 0.02,
                        tile, yw - 0.028, M_SIGN_B)
        elif style == 'panel':
            n = max(1, int(round((sb - sa) / 1.1)))
            pw2 = (sb - sa) / n
            for k in range(n):
                _box(sh, (sa + k * pw2 + 0.006, yw - 0.042,
                          0.05), (sa + (k + 1) * pw2 - 0.006, yw - 0.020,
                          bh - 0.03), M_CONCRETE)
        # 'stucco': plain backing + a scored reveal line.
        elif style == 'stucco':
            _box(sh, (sa + 0.004, yw - 0.036, bh * 0.45),
                 (sb - 0.004, yw - 0.022, bh * 0.45 + 0.02), M_TRIM)
        _box(sh, (sa - 0.004, yw - 0.040, bh - 0.005),
             (sb + 0.004, yw + 0.03, bh + 0.045), M_METAL)       # sill


# ---------------------------------------------------------------------------
# roll-up curtain (adapted from the Phase-0 shutter, as an emitter)
# ---------------------------------------------------------------------------
def emit_rollup(sh, x0, x1, yw, z_head, open_frac=0.0, housing=True,
                z0=0.02):
    """Roll-up curtain over [x0,x1] on wall plane y=yw: guide channels,
    optional housing, welded accordion slat ribbon down to the opening
    line, bottom bar. Proud of the wall (never coplanar with it)."""
    keep = sh.tag
    sh.tag = 'shutters'
    gw = 0.075
    _box(sh, (x0 - gw, yw - 0.075, z0 - 0.02), (x0 + 0.006, yw + 0.035,
         z_head), M_METAL)
    _box(sh, (x1 - 0.006, yw - 0.075, z0 - 0.02), (x1 + gw, yw + 0.035,
         z_head), M_METAL)
    zh = z_head
    if housing:
        _box(sh, (x0 - gw - 0.02, yw - 0.16, z_head - 0.015),
             (x1 + gw + 0.02, yw + 0.045, z_head + 0.29), M_METAL)
        zh = z_head - 0.015
    z_bot = z0 + 0.01 + open_frac * (z_head - z0 - 0.5)
    xa, xb = x0 + 0.006 - 0.012, x1 - 0.006 + 0.012   # embedded into guides
    pitch, hook = 0.10, 0.025
    yF, yB = yw - 0.052, yw - 0.038
    z = zh
    while z - pitch >= z_bot:
        zf = z - (pitch - hook)
        sh.quad((xa, yF, z), (xb, yF, z), (xb, yB, zf), (xa, yB, zf),
                M_SHUTTER)
        sh.quad((xa, yB, zf), (xb, yB, zf), (xb, yF, zf - hook),
                (xa, yF, zf - hook), M_SHUTTER)
        z = zf - hook
    _box(sh, (xa - 0.004, yF - 0.033, z - 0.09),
         (xb + 0.004, yF + 0.043, z + 0.005), M_METAL)           # bottom bar
    sh.tag = keep


# ---------------------------------------------------------------------------
# the bay dressing emitter
# ---------------------------------------------------------------------------
def emit_storefront_bay(sh, x0, x1, yw, door, head, bulkhead=0.62,
                        tile=0.19, mullion_pitch=0.95, transom=2.2,
                        piers=(True, True), glass=False,
                        bulkhead_style='checker', glazing='mullioned',
                        door_dressing=True, glaze_dressing=True):
    """Dress one storefront bay on the wall plane y=yw (faces -y). See the
    module docstring for the parameter axes. glazing='plate' emits bars only
    at the span ends (big butt-glazed sheets, faint 4 mm joint bars)."""
    keep = sh.tag
    sh.tag = 'storefront'
    pw = 0.18

    for side, on in (('L', piers[0]), ('R', piers[1])):
        if not on:
            continue
        px0 = (x0 - pw - 0.004) if side == 'L' else (x1 + 0.004)
        _box(sh, (px0, yw - 0.034, 0.0), (px0 + pw, yw + 0.03, head + 0.02),
             M_TRIM)
        if bulkhead_style == 'checker':
            _tile_checker(sh, px0 + 0.006, px0 + pw - 0.006, 0.03,
                          head - 0.01, tile, yw - 0.034, M_SIGN_B, M_TRIM)
        elif bulkhead_style == 'tile':
            _tile_field(sh, px0 + 0.006, px0 + pw - 0.006, 0.03,
                        head - 0.01, tile, yw - 0.034, M_SIGN_B)

    spans = [(x0 + 0.01, x1 - 0.01)]
    if door is not None:
        (d0, d1) = door
        spans = [(x0 + 0.01, d0 - 0.01), (d1 + 0.01, x1 - 0.01)]
        spans = [(a, b) for (a, b) in spans if b - a > 0.15]
    _bulkhead(sh, spans, yw, bulkhead, bulkhead_style, tile)

    if not glaze_dressing:                          # roll-up REPLACES the
        _box(sh, (x0 - 0.006, yw - 0.042, head - 0.055),   # glazing: head
             (x1 + 0.006, yw + 0.03, head + 0.015), M_METAL)  # channel only
        sh.tag = keep
        return
    for (sa, sb) in spans:                          # glazing frame
        if glazing == 'plate':
            for mx in (sa + 0.02, sb - 0.02):       # end bars only
                _box(sh, (mx - 0.024, yw - 0.034, bulkhead + 0.02),
                     (mx + 0.024, yw + 0.09, head - 0.02), M_METAL)
            n = max(1, int(round((sb - sa) / 1.9)))
            for k in range(1, n):                   # faint butt joints
                mx = sa + (sb - sa) * k / n
                _box(sh, (mx - 0.004, yw - 0.018, bulkhead + 0.03),
                     (mx + 0.004, yw + 0.05, head - 0.03), M_METAL)
        else:
            n = max(1, int(round((sb - sa) / mullion_pitch)))
            for k in range(n + 1):
                mx = sa + (sb - sa) * k / n
                _box(sh, (mx - 0.026, yw - 0.036, bulkhead + 0.02),
                     (mx + 0.026, yw + 0.09, head - 0.02), M_METAL)
    if glazing != 'plate':                          # transom bar
        _box(sh, (x0 + 0.005, yw - 0.036, transom - 0.028),
             (x1 - 0.005, yw + 0.09, transom + 0.028), M_METAL)
    _box(sh, (x0 - 0.006, yw - 0.042, head - 0.055),
         (x1 + 0.006, yw + 0.03, head + 0.015), M_METAL)         # head

    if door is not None and door_dressing:
        (d0, d1) = door
        for fx in (d0 + 0.004, d1 - 0.059):
            _box(sh, (fx, yw - 0.032, 0.0), (fx + 0.055, yw + 0.084,
                 transom - 0.03), M_METAL)
        _box(sh, (d0 + 0.044, yw - 0.030, transom - 0.085),
             (d1 - 0.044, yw + 0.077, transom - 0.032), M_METAL)
        _box(sh, (d0 + 0.045, yw - 0.026, 0.98),
             (d1 - 0.045, yw + 0.012, 1.06), M_METAL)
        _box(sh, (d0 + 0.045, yw - 0.024, 0.01),
             (d1 - 0.045, yw + 0.014, 0.30), M_METAL)
        if glass:
            sh.quad((d0 + 0.05, yw + 0.018, 0.32),
                    (d1 - 0.05, yw + 0.018, 0.32),
                    (d1 - 0.05, yw + 0.018, transom - 0.09),
                    (d0 + 0.05, yw + 0.018, transom - 0.09), M_GLASS)

    if glass:                                       # display + transom glass
        for (sa, sb) in spans:
            gz1 = head - 0.04
            if glazing == 'plate':
                sh.quad((sa + 0.03, yw + 0.018, bulkhead + 0.02),
                        (sb - 0.03, yw + 0.018, bulkhead + 0.02),
                        (sb - 0.03, yw + 0.018, gz1),
                        (sa + 0.03, yw + 0.018, gz1), M_GLASS)
                continue
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
                        (gb, yw + 0.018, gz1), (ga, yw + 0.018, gz1),
                        M_GLASS)
        if door is not None and door_dressing and glazing != 'plate':
            (d0, d1) = door
            sh.quad((d0 + 0.02, yw + 0.018, transom + 0.02),
                    (d1 - 0.02, yw + 0.018, transom + 0.02),
                    (d1 - 0.02, yw + 0.018, head - 0.04),
                    (d0 + 0.02, yw + 0.018, head - 0.04), M_GLASS)
    sh.tag = keep


# ---------------------------------------------------------------------------
# standalone tool
# ---------------------------------------------------------------------------
STOREFRONT_SPEC = [
    dict(name="width", type='FLOAT', default=4.2, min=2.4, max=8.0,
         unit='LENGTH', desc="Bay width between piers"),
    dict(name="head", type='FLOAT', default=3.0, min=2.6, max=4.0,
         unit='LENGTH', desc="Storefront head height"),
    dict(name="bulkhead", type='FLOAT', default=0.62, min=0.3, max=1.0,
         unit='LENGTH'),
    dict(name="bulkhead_style", type='ENUM', default='checker',
         items=('checker', 'tile', 'stucco', 'panel')),
    dict(name="tile", type='FLOAT', default=0.19, min=0.10, max=0.35,
         unit='LENGTH', desc="Tile module"),
    dict(name="glazing", type='ENUM', default='mullioned',
         items=('mullioned', 'plate'),
         desc="plate = large butt-glazed sheets"),
    dict(name="door_pos", type='ENUM', default='left',
         items=('left', 'center', 'right', 'none')),
    dict(name="rollup", type='ENUM', default='none',
         items=('none', 'closed', 'half', 'door'),
         desc="Roll-up REPLACES the glazing (closed) / the door (door)"),
    dict(name="bars", type='BOOL', default=False,
         desc="Wrought-iron security bars over the display glass"),
    dict(name="facade", type='ENUM', default='stucco',
         items=('stucco', 'ribbed', 'banded'),
         desc="Upper wall treatment"),
    dict(name="sign", type='ENUM', default='fascia',
         items=('fascia', 'wrap', 'none'),
         desc="wrap = tall band wrapping the bay sides"),
    dict(name="blade", type='BOOL', default=False,
         desc="Add a projecting blade sign"),
    dict(name="awning", type='ENUM', default='none',
         items=('none', 'canvas', 'corrugated')),
    dict(name="stories", type='INT', default=1, min=1, max=2,
         desc="2 = shop + upper floor with punched windows and a cornice"),
]


def build_storefront_bay(p, rng):
    """Standalone storefront bay assembly. The backing wall VOIDS the
    shopfront opening and a dark shop cavity sits behind it, so the inset
    glazing and frames read as cut INTO the wall, not applied on it.
    Composes the Phase-0 kit: roll-up curtains (which REPLACE glazing or
    door), factory windows + cornice (second story), blade sign, awnings,
    iron security bars."""
    w, head = p["width"], p["head"]
    two = p["stories"] >= 2
    f2h = 2.75
    top = head + 0.68 + (f2h if two else 0.0)
    ru = p["rollup"]
    sh = _Shell()
    sh.tag = 'storefront'
    # backing wall AROUND the opening (top band + side columns), never a
    # slab behind the glass.
    _box(sh, (-0.24, 0.024, head - 0.045), (w + 0.24, 0.20, top), M_TRIM)
    # side columns: inset planes + tops embedded 30 mm INTO the band (flush
    # shared planes put their corners on the band edges -> T-junctions).
    _box(sh, (-0.234, 0.030, -0.02), (-0.015, 0.194, head - 0.012), M_TRIM)
    _box(sh, (w + 0.015, 0.030, -0.02), (w + 0.234, 0.194, head - 0.012),
         M_TRIM)
    if ru != 'closed':
        # shop cavity: floor / ceiling / back / side reveals (dark), giving
        # the glazing real depth behind it.
        sh.tag = 'interior_walls'
        _box(sh, (-0.02, 0.015, -0.045), (w + 0.02, 0.92, 0.018),
             M_CONCRETE)                            # floor
        _box(sh, (-0.02, 0.015, head - 0.015), (w + 0.02, 0.92, head + 0.04),
             M_GYPSUM)                              # ceiling
        _box(sh, (-0.02, 0.86, -0.03), (w + 0.02, 0.96, head + 0.02),
             M_GYPSUM)                              # back wall
        _box(sh, (-0.048, 0.015, -0.03), (-0.012, 0.90, head + 0.015),
             M_GYPSUM)
        _box(sh, (w + 0.012, 0.015, -0.03), (w + 0.048, 0.90, head + 0.015),
             M_GYPSUM)
        sh.tag = 'storefront'

    # signage band over the shopfront.
    sh.tag = 'signage'
    if p["sign"] == 'wrap':
        _box(sh, (-0.30, -0.016, head + 0.04), (w + 0.30, 0.03, head + 0.66),
             M_SIGN_C)
        for bx in (-0.306, w + 0.306 - 0.05):
            _box(sh, (bx, -0.020, head + 0.02), (bx + 0.05, 0.44,
                 head + 0.68), M_SIGN_C)
    elif p["sign"] == 'fascia':
        _box(sh, (-0.20, -0.012, head + 0.06), (w + 0.20, 0.03, head + 0.60),
             M_SIGN_C)

    # facade treatment on the upper wall.
    sh.tag = 'facade_front'
    fz0, fz1 = head + 0.70, top - 0.05
    if fz1 - fz0 > 0.25 and p["facade"] == 'ribbed':
        n = max(2, int(round(w / 0.38)))
        for k in range(n + 1):
            rx = 0.06 + (w - 0.12) * k / n
            _box(sh, (rx - 0.025, -0.006, fz0 + 0.03),
                 (rx + 0.025, 0.03, fz1 - 0.03), M_STUCCO)
    elif fz1 - fz0 > 0.25 and p["facade"] == 'banded':
        nb = max(2, int((fz1 - fz0) / 0.5))
        for k in range(nb):
            bz = fz0 + (fz1 - fz0) * (k + 0.5) / nb
            _box(sh, (-0.26, -0.008, bz - 0.055), (w + 0.26, 0.03,
                 bz + 0.055), M_STUCCO)

    # ground floor dressing.
    dw = min(1.05, w * 0.35)
    door = None
    if p["door_pos"] == 'left':
        door = (0.10, 0.10 + dw)
    elif p["door_pos"] == 'right':
        door = (w - 0.10 - dw, w - 0.10)
    elif p["door_pos"] == 'center':
        door = (w * 0.5 - dw * 0.5, w * 0.5 + dw * 0.5)

    bh = p["bulkhead"]
    if ru == 'closed':
        # roll-up REPLACES the glazing entirely: bulkhead + piers + head
        # channel + the curtain over the whole opening down to the sill.
        emit_storefront_bay(sh, 0.0, w, 0.0, None, head, bulkhead=bh,
                            tile=p["tile"], glass=False,
                            bulkhead_style=p["bulkhead_style"],
                            glaze_dressing=False)
        emit_rollup(sh, 0.085, w - 0.085, -0.006, head - 0.085,
                    open_frac=0.0, housing=True, z0=bh + 0.03)
    elif ru == 'door' and door is not None:
        # service door: roll-up in the door slot, glazed spans either side.
        (d0, d1) = door
        emit_storefront_bay(sh, 0.0, w, 0.0, door, head, bulkhead=bh,
                            tile=p["tile"], glass=True,
                            bulkhead_style=p["bulkhead_style"],
                            glazing=p["glazing"], door_dressing=False)
        emit_rollup(sh, d0 + 0.07, d1 - 0.07, -0.012,
                    min(2.2, head - 0.35) + 0.42, open_frac=0.0,
                    housing=True, z0=0.02)
    else:
        emit_storefront_bay(sh, 0.0, w, 0.0, door, head, bulkhead=bh,
                            tile=p["tile"], glass=True,
                            bulkhead_style=p["bulkhead_style"],
                            glazing=p["glazing"])
        if ru == 'half':
            # raised curtain in front of the glazing, inside the opening.
            emit_rollup(sh, 0.085, w - 0.085, -0.062, head - 0.085,
                        open_frac=0.6, housing=True, z0=bh + 0.03)

    if p["bars"] and ru in ('none', 'half'):
        # wrought-iron security bars over the display glass: verticals at a
        # tight pitch + two horizontal flats, standing proud of the glazing.
        sh.tag = 'shutters'
        z0b, z1b = bh + 0.10, head - 0.14
        nbars = max(4, int(round(w / 0.145)))
        for k in range(nbars + 1):
            bx2 = 0.05 + (w - 0.10) * k / nbars
            _box(sh, (bx2 - 0.011, -0.095, z0b), (bx2 + 0.011, -0.072, z1b),
                 M_METAL)
        for zf in (z0b + (z1b - z0b) * 0.25, z0b + (z1b - z0b) * 0.75):
            _box(sh, (0.03, -0.102, zf - 0.02), (w - 0.03, -0.066,
                 zf + 0.02), M_METAL)
        sh.tag = 'storefront'

    out = [sh.to_object("LA_Asm_Storefront", [_material(n) for n in _MATS])]

    # second story: punched factory windows + cornice crown.
    if two:
        nw = max(2, int(round(w / 1.9)))
        wp = dict(width=min(1.5, w / nw - 0.5), height=1.55, cols=3, rows=3,
                  pivot_open=0, broken=0.0)
        for k in range(nw):
            wx = (w / nw) * (k + 0.5) - wp["width"] * 0.5
            for o in el.build_factory_window(wp, rng):
                o.location = (wx, -0.005, head + 1.05)
                out.append(o)
        cp = dict(length=w + 0.60, projection=0.32, height=0.42,
                  modillion_spacing=0.55, dentils=False)
        for o in el.build_cornice(cp, rng):
            o.location = (-0.30, 0.0, top - 0.42)
            out.append(o)

    if p["blade"]:
        bp = dict(height=min(2.2, head - 0.6), projection=0.85, panels=4,
                  top_z=head + (0.62 if not two else 1.6))
        for o in el.build_blade_sign(bp, rng):
            o.rotation_euler = (0.0, 0.0, -1.5707963)
            o.location = (-0.02, 0.35, 0.0)
            out.append(o)

    if p["awning"] != 'none':
        # roll-up present -> the awning mounts ABOVE the roll-up housing and
        # spans the WHOLE bay (proud of the sign band); otherwise it hangs
        # over the display span beside the door as usual.
        if ru != 'none':
            aw = w - 0.04
            ax, ay, az = 0.02, -0.035, head + 0.36
        else:
            aw = max(1.2, w - (dw + 0.4 if door else 0.3))
            ax = (door[1] + 0.15 if (door and p["door_pos"] == 'left')
                  else 0.15)
            ay, az = -0.02, head - 0.10
        if p["awning"] == 'canvas':
            ap = dict(width=aw, depth=1.0, drop=0.5, valance=0.26,
                      stripes=True)
            objs = el.build_canvas_awning(ap, rng)
        else:
            ap = dict(width=aw, depth=1.0, drop=0.4, struts=2)
            objs = el.build_awning(ap, rng)
        for o in objs:
            o.location = (ax, ay, az)
            out.append(o)
    return out


def transom_z(head):
    """Transom line used by the roll-up door head (kept in one place)."""
    return min(2.2, head - 0.35) + 0.55


params.register_tool(idname="la_storefront_bay", label="Storefront Bay",
                     family="Assembly", build=build_storefront_bay,
                     spec=STOREFRONT_SPEC)
