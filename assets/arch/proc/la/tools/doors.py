"""LA gen B1.4: door leaf + frame emitters (rpg-20cn).

Real door meshes for the commercial kit -- storefront glass door leafs
(stile/rail aluminium frame, twin glass islands, exterior push bar), rear
service man-doors (painted hollow-metal slab, kick plate, hinge knuckles,
knob), and the hollow-metal jamb/head frame liner the leafs hang in.

Emitters work in WALL-FRAME coordinates (point = origin + u_dir * u
- outward * depth + z), so they compose with any ``_Wall`` regardless of
orientation -- the angled mini-mall arm included.  Leafs hinge on a
vertical axis and take an ``ajar`` angle in radians (0 = closed);
positive swings OUTWARD past the wall plane (commercial egress opens
out, and an out-swung leaf reads clearly from the street for the
dead-mall story).

Quality bar: every face is a planar quad from the shared ``_hexa``
primitive, geometry is tagged 'doors', keeps >= 2 mm off the host wall
planes when closed (never coplanar), and hardware embeds 10-20 mm into
its carrier instead of landing on faces.  The ``*_filler`` factories
return callbacks for ``_Wall.fill(leaf=...)`` -- they draw their rng
inside the deterministic fill order, so seeded rebuilds reproduce.
"""
import math

from mathutils import Vector

from ..geom import M_GLASS, M_METAL, M_TRIM, _hexa

# leaf proportions (metres) -- storefront aluminium stock.
_GLASS_T = 0.044          # glass-leaf thickness
_STILE = 0.10             # stile / top-rail width
_MID_RAIL = (0.94, 1.05)  # push-rail z band (leaf-local)
_KICK = 0.28              # bottom-rail (kick) height
_SLAB_T = 0.046           # man-door slab thickness
_CLR = 0.012              # jamb / meeting clearance


def _leaf_axes(u_dir, outward, hinge_sign, ajar):
    """Leaf frame at ``ajar`` radians about the vertical hinge axis:
    ``ev`` runs hinge -> latch, ``ew`` is the leaf's exterior normal.
    ajar=0 gives ev=u_dir*hinge_sign, ew=outward; positive ajar swings
    the latch edge outward."""
    eu = Vector(u_dir).normalized() * hinge_sign
    n = Vector(outward).normalized()
    c, s = math.cos(ajar), math.sin(ajar)
    return eu * c + n * s, n * c - eu * s


def _pane(sh, base, ev, ew, v0, v1, w, z0, z1, mat):
    """Single glazing island quad at leaf depth ``w``, facing +ew."""
    def pt(v, z):
        p = base + ev * v + ew * w
        return (p.x, p.y, p.z + z)
    if (ev.x * ew.y - ev.y * ew.x) < 0.0:
        sh.quad(pt(v0, z0), pt(v1, z0), pt(v1, z1), pt(v0, z1), mat)
    else:
        sh.quad(pt(v0, z0), pt(v0, z1), pt(v1, z1), pt(v1, z0), mat)


def emit_glass_leaf(sh, origin, u_dir, outward, u0, u1, z0, z1,
                    depth=0.055, ajar=0.0, hinge_right=False,
                    push_bar=True):
    """One storefront glass door leaf hanging in the rough opening
    [u0,u1] x [z0,z1]; the leaf's exterior face sits ``depth`` behind
    the outer wall plane.  Stile/rail frame + two glass islands + an
    exterior push bar (mounts embedded 12 mm into the stiles)."""
    keep = sh.tag
    sh.tag = 'doors'
    o = Vector(origin)
    eu = Vector(u_dir).normalized()
    n = Vector(outward).normalized()
    wd = (u1 - u0) - 2.0 * _CLR
    ht = (z1 - z0) - _CLR - 0.008
    hs = -1.0 if hinge_right else 1.0
    hu = (u1 - _CLR) if hinge_right else (u0 + _CLR)
    base = o + eu * hu - n * depth
    base = Vector((base.x, base.y, base.z + z0 + 0.008))
    ev, ew = _leaf_axes(u_dir, outward, hs, ajar)
    # frame members: leaf body spans w [-_GLASS_T, 0] (0 = exterior face).
    mz0, mz1 = _MID_RAIL
    _hexa(sh, base, ev, ew, 0.0, _STILE, -_GLASS_T, 0.0, 0.0, ht, M_METAL)
    _hexa(sh, base, ev, ew, wd - _STILE, wd, -_GLASS_T, 0.0, 0.0, ht,
          M_METAL)
    # rails embed 12 mm into the stiles and step 3 mm behind BOTH leaf
    # faces -- a flush rail would share the stiles' front/back planes
    # along the embed strip (coplanar-overlap z-fight).
    rv0, rv1 = _STILE - 0.012, wd - _STILE + 0.012
    rw0, rw1 = -_GLASS_T + 0.003, -0.003
    _hexa(sh, base, ev, ew, rv0, rv1, rw0, rw1,
          ht - _STILE, ht - 0.002, M_METAL)                # top rail
    _hexa(sh, base, ev, ew, rv0, rv1, rw0, rw1,
          mz0, mz1, M_METAL)                               # mid rail
    _hexa(sh, base, ev, ew, rv0, rv1, rw0, rw1,
          0.002, _KICK, M_METAL)                           # kick rail
    gv0, gv1 = _STILE + 0.001, wd - _STILE - 0.001
    _pane(sh, base, ev, ew, gv0, gv1, -_GLASS_T * 0.5,
          _KICK + 0.001, mz0 - 0.001, M_GLASS)             # lower island
    _pane(sh, base, ev, ew, gv0, gv1, -_GLASS_T * 0.5,
          mz1 + 0.001, ht - _STILE - 0.001, M_GLASS)       # upper island
    if push_bar and wd > 2.6 * _STILE:
        bz = (mz0 + mz1) * 0.5
        _hexa(sh, base, ev, ew, _STILE * 0.55 + 0.014,
              wd - _STILE * 0.55 - 0.014, 0.048, 0.082,
              bz - 0.017, bz + 0.017, M_METAL)
        for mv in (_STILE * 0.55, wd - _STILE * 0.55 - 0.048):
            _hexa(sh, base, ev, ew, mv, mv + 0.048, -0.012, 0.052,
                  bz - 0.021, bz + 0.021, M_METAL)         # mounts, embedded
    sh.tag = keep


def emit_man_door(sh, origin, u_dir, outward, u0, u1, z0, z1,
                  depth=0.055, ajar=0.0, hinge_right=False):
    """Rear service man-door: painted hollow-metal slab + kick plate
    (embedded 12 mm, proud 6 mm), knob with rose, and three hinge
    knuckles riding the hinge edge."""
    keep = sh.tag
    sh.tag = 'doors'
    o = Vector(origin)
    eu = Vector(u_dir).normalized()
    n = Vector(outward).normalized()
    wd = (u1 - u0) - 2.0 * _CLR
    ht = (z1 - z0) - _CLR - 0.008
    hs = -1.0 if hinge_right else 1.0
    hu = (u1 - _CLR) if hinge_right else (u0 + _CLR)
    base = o + eu * hu - n * depth
    base = Vector((base.x, base.y, base.z + z0 + 0.008))
    ev, ew = _leaf_axes(u_dir, outward, hs, ajar)
    _hexa(sh, base, ev, ew, 0.0, wd, -_SLAB_T, 0.0, 0.0, ht, M_TRIM)
    _hexa(sh, base, ev, ew, 0.015, wd - 0.015, -0.012, 0.006,
          0.012, 0.26, M_METAL)                            # kick plate
    _hexa(sh, base, ev, ew, wd - 0.105, wd - 0.055, -0.012, 0.046,
          0.985, 1.035, M_METAL)                           # knob shank+rose
    _hexa(sh, base, ev, ew, wd - 0.125, wd - 0.035, 0.042, 0.084,
          0.972, 1.048, M_METAL)                           # knob head, embedded
    for hz in (0.16, ht * 0.5, ht - 0.24):
        _hexa(sh, base, ev, ew, -0.014, 0.020, -0.058, -0.036,
              hz, hz + 0.11, M_METAL)                      # hinge knuckle
    sh.tag = keep


def emit_door_frame(sh, origin, u_dir, outward, u0, u1, z0, z1, depth,
                    mat=M_METAL):
    """Hollow-metal jamb/head frame liner in the rough opening: posts +
    head embedded 20 mm through the reveal planes, plus door-stop ribs
    at mid-reveal.  Faces stop 8 mm shy of the outer wall plane (never
    coplanar with it)."""
    keep = sh.tag
    sh.tag = 'doors'
    o = Vector(origin)
    eu = Vector(u_dir).normalized()
    n = Vector(outward).normalized()
    d0, d1 = 0.008, max(depth - 0.008, 0.05)
    stop = min(depth * 0.5, 0.06)
    for ua, ub in ((u0 - 0.020, u0 + 0.040), (u1 - 0.040, u1 + 0.020)):
        _hexa(sh, o, eu, n, ua, ub, -d1, -d0, z0 + 0.002, z1 + 0.020, mat)
    _hexa(sh, o, eu, n, u0 + 0.020, u1 - 0.020, -d1 + 0.002, -d0 - 0.002,
          z1 - 0.022, z1 + 0.010, mat)                     # head, embedded
    for ua, ub in ((u0 + 0.024, u0 + 0.056), (u1 - 0.056, u1 - 0.024)):
        _hexa(sh, o, eu, n, ua, ub, -stop - 0.016, -stop + 0.016,
              z0 + 0.010, z1 - 0.028, mat)                 # stop ribs
    sh.tag = keep


def glass_leaf_filler(rng, ajar_frac, double_min=1.35, frame=True):
    """``_Wall.fill(leaf=...)`` callback: hollow-metal liner + glass
    leaf(s) in every doorL opening.  Spans past ``double_min`` metres get
    a leaf pair meeting on an astragal gap; ``ajar_frac`` of leafs stand
    seeded-open (dead-mall tie-in)."""
    def _ang():
        return rng.uniform(0.18, 1.05) if rng.random() < ajar_frac else 0.0

    def cb(wall, u0, u1, z0, z1, depth):
        sh = wall.s
        o, u, n = tuple(wall.o), tuple(wall.u), tuple(wall.n)
        if frame:
            emit_door_frame(sh, o, u, n, u0, u1, z0, z1, depth)
        d_leaf = min(max(depth - 0.026, 0.03), 0.066)
        if (u1 - u0) >= double_min:
            mid = (u0 + u1) * 0.5
            emit_glass_leaf(sh, o, u, n, u0, mid + 0.006, z0, z1,
                            depth=d_leaf, ajar=_ang(), hinge_right=False)
            emit_glass_leaf(sh, o, u, n, mid - 0.006, u1, z0, z1,
                            depth=d_leaf, ajar=_ang(), hinge_right=True)
        else:
            emit_glass_leaf(sh, o, u, n, u0, u1, z0, z1, depth=d_leaf,
                            ajar=_ang(), hinge_right=rng.random() < 0.5)
    return cb


def man_door_filler(rng, ajar_frac, frame=True):
    """``_Wall.fill(leaf=...)`` callback: liner + one service man-door
    slab per doorL opening, seeded ajar like the glass filler."""
    def cb(wall, u0, u1, z0, z1, depth):
        sh = wall.s
        o, u, n = tuple(wall.o), tuple(wall.u), tuple(wall.n)
        if frame:
            emit_door_frame(sh, o, u, n, u0, u1, z0, z1, depth)
        ang = rng.uniform(0.15, 0.85) if rng.random() < ajar_frac else 0.0
        emit_man_door(sh, o, u, n, u0, u1, z0, z1,
                      depth=min(max(depth - 0.026, 0.03), 0.066),
                      ajar=ang, hinge_right=rng.random() < 0.5)
    return cb
