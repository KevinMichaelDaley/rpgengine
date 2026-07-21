"""Family A -- residential massing. A1: Dingbat Apartment (rpg-qdr5).

TOPOLOGY PLAN (the ticket carries the full ASCII diagrams; summary):

The building shell is ONE welded bmesh built on a GLOBAL LINE GRID: every
horizontal Z-level runs around all four walls, and every window jamb owns an
X (or Y) line, so wall grids weld at corners with plain 4-valence verts and
openings are grid CELLS -- never booleans.

  z-levels   0 grade | 0.9 sill1 | 2.1 head1 | 2.45 soffit | 2.75 slab top
             ... per extra floor: sill/head/slab ... | roof plane | parapet top
  front      fascia + upper floors + parapet only; ground = CARPORT VOID
             (boundary loop rings it; square posts are separate closed shells)
  soffit     horizontal grid at soffit level, x-lines shared with the facade,
             meets the recessed ground wall (unit doors) at shared verts
  windows    per-cell: 4 inset ring quads -> 4 jamb (return) quads -> 1 pane
             quad; every corner is 4-valence
  roof       parapet cap ring (1 quad wide) -> inner drop -> inset roof plane,
             all built from the same clipped line families so edges weld
  stair      switchback of separate closed shells (step boxes, stringers,
             landing, square rail posts + quad-strip handrail) -- no sawtooth
             end caps (those would be L-shaped ngons)

Validation: la.topology.validate() must report 100% quads, 0 ngons/tris,
0 non-manifold, 0 doubles, 0 T-junctions on every emitted mesh.
"""
import bmesh
import bpy
from mathutils import Vector

from .. import params
from .. import topology

# ---------------------------------------------------------------------------
# Welded-grid machinery
# ---------------------------------------------------------------------------

_WELD = 1.0e-4


#: UV texel density: one UV tile per this many metres (rule 4).
_UV_SCALE = 1.0 / 2.0


class _Shell:
    """A bmesh under construction with a welded-vert cache.

    Rule 4: on export every face gets dominant-axis planar UVs at uniform
    texel density -- welded rectilinear wall grids therefore unwrap as
    CONTINUOUS strips, with natural seams at 90-degree corners.
    Rule 5: quads carry a ``tag``; tags become vertex groups on the object.
    """

    def __init__(self):
        self.bm = bmesh.new()
        self._cache = {}
        self.tag = None           # current subpart tag (rule 5)
        self._face_tags = {}      # bmesh face -> tag string

    def vert(self, co):
        key = (round(co[0] / _WELD), round(co[1] / _WELD), round(co[2] / _WELD))
        v = self._cache.get(key)
        if v is None:
            v = self.bm.verts.new(co)
            self._cache[key] = v
        return v

    def quad(self, a, b, c, d, mat=0, tag=None):
        """Quad from 4 coords, CCW as seen from the face normal side."""
        try:
            f = self.bm.faces.new((self.vert(a), self.vert(b),
                                   self.vert(c), self.vert(d)))
        except ValueError:      # exact duplicate face (shared cell edge) -- skip
            return None
        f.material_index = mat
        t = tag if tag is not None else self.tag
        if t:
            self._face_tags[f] = t
        return f

    def _write_uvs(self):
        uv = self.bm.loops.layers.uv.new("UVMap")
        for f in self.bm.faces:
            n = f.normal
            ax = max(range(3), key=lambda i: abs(n[i]))   # dominant axis
            ua, va = ((1, 2), (0, 2), (0, 1))[ax]
            # mirror one axis where the normal is negative so texture reads
            # unflipped from the OUTSIDE on every wall.
            s_u = -1.0 if n[ax] < 0 and ax != 2 else 1.0
            for loop in f.loops:
                co = loop.vert.co
                loop[uv].uv = (s_u * co[ua] * _UV_SCALE, co[va] * _UV_SCALE)

    def to_object(self, name, materials):
        self.bm.normal_update()      # BEFORE UVs: projection needs real normals
        self._write_uvs()
        me = bpy.data.meshes.new(name)
        # Indices FIRST: fresh bmesh verts are -1 until index_update, and a
        # -1 fed to vertex_groups.add is a hard bpy crash (hit at floors=3;
        # allocation-order dependent).
        self.bm.verts.index_update()
        tag_verts = {}
        for f, t in self._face_tags.items():
            tag_verts.setdefault(t, set()).update(v.index for v in f.verts)
        self.bm.to_mesh(me)
        self.bm.free()
        for m in materials:
            me.materials.append(m)
        ob = bpy.data.objects.new(name, me)
        for t, idxs in sorted(tag_verts.items()):
            vg = ob.vertex_groups.new(name=t)
            vg.add(sorted(idxs), 1.0, 'REPLACE')
        return ob


def _sheared_box(shell, x0, x1, y0, y1, z_at_y0, z_at_y1, depth, mat=0, tag=None):
    """A box sheared along Y: top/bottom follow the line z(y), thickness
    @p depth below it. Every face stays PLANAR (shear is linear), so this is
    6 quads -- the natural stair stringer / sloped-beam primitive."""
    a0, a1 = z_at_y0, z_at_y1
    b0, b1 = a0 - depth, a1 - depth
    q = shell.quad
    q((x0, y0, b0), (x1, y0, b0), (x1, y1, b1), (x0, y1, b1), mat, tag)  # bottom
    q((x0, y0, a0), (x0, y1, a1), (x1, y1, a1), (x1, y0, a0), mat, tag)  # top
    q((x0, y0, b0), (x0, y1, b1), (x0, y1, a1), (x0, y0, a0), mat, tag)  # -x side
    q((x1, y0, b0), (x1, y0, a0), (x1, y1, a1), (x1, y1, b1), mat, tag)  # +x side
    q((x0, y0, b0), (x0, y0, a0), (x1, y0, a0), (x1, y0, b0), mat, tag)  # y0 end
    q((x0, y1, b1), (x1, y1, b1), (x1, y1, a1), (x0, y1, a1), mat, tag)  # y1 end


def _box(shell, mn, mx, mat=0):
    """Closed axis-aligned 6-quad box (its own welded island)."""
    x0, y0, z0 = mn
    x1, y1, z1 = mx
    shell.quad((x0, y0, z0), (x0, y1, z0), (x1, y1, z0), (x1, y0, z0), mat)  # bottom (-z)
    shell.quad((x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1), mat)  # top (+z)
    shell.quad((x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1), mat)  # -y
    shell.quad((x1, y1, z0), (x0, y1, z0), (x0, y1, z1), (x1, y1, z1), mat)  # +y
    shell.quad((x0, y1, z0), (x0, y0, z0), (x0, y0, z1), (x0, y1, z1), mat)  # -x
    shell.quad((x1, y0, z0), (x1, y1, z0), (x1, y1, z1), (x1, y0, z1), mat)  # +x


class _Wall:
    """One planar wall grid: origin + u direction (horizontal) + z up.

    ``cells(iu, iz)`` classifies each cell: 'wall', 'void', or
    ('window'|'door', recess_sign) -- openings get the inset-ring treatment.
    Face winding: CCW seen from ``outward``.
    """

    def __init__(self, shell, origin, u_dir, u_lines, z_lines, outward, mat=0):
        self.s = shell
        self.o = Vector(origin)
        self.u = Vector(u_dir).normalized()
        self.ul = list(u_lines)
        self.zl = list(z_lines)
        self.n = Vector(outward).normalized()
        self.mat = mat
        # A wall's quad (u0,z0)-(u1,z1) must read CCW from `outward`:
        flip = self.u.cross(Vector((0, 0, 1))).dot(self.n) < 0
        self.flip = flip

    def _co(self, u, z, depth=0.0):
        p = self.o + self.u * u - self.n * depth
        return (p.x, p.y, p.z + z)

    def _q(self, a, b, c, d, mat=None):
        m = self.mat if mat is None else mat
        if self.flip:
            self.s.quad(a, d, c, b, m)
        else:
            self.s.quad(a, b, c, d, m)

    def fill(self, classify, frame=0.07, recess=0.08,
             mat_frame=None, mat_pane=None):
        for iz in range(len(self.zl) - 1):
            for iu in range(len(self.ul) - 1):
                kind = classify(self.ul[iu], self.zl[iz])
                if kind == 'void':
                    continue
                u0, u1 = self.ul[iu], self.ul[iu + 1]
                z0, z1 = self.zl[iz], self.zl[iz + 1]
                if kind == 'wall':
                    self._q(self._co(u0, z0), self._co(u1, z0),
                            self._co(u1, z1), self._co(u0, z1))
                    continue
                # opening cell: inset ring -> jamb return -> recessed pane
                fu0, fu1 = u0 + frame, u1 - frame
                fz0, fz1 = z0 + frame, z1 - frame
                oc = [self._co(u0, z0), self._co(u1, z0),
                      self._co(u1, z1), self._co(u0, z1)]
                ic = [self._co(fu0, fz0), self._co(fu1, fz0),
                      self._co(fu1, fz1), self._co(fu0, fz1)]
                rc = [self._co(fu0, fz0, recess), self._co(fu1, fz0, recess),
                      self._co(fu1, fz1, recess), self._co(fu0, fz1, recess)]
                mf = self.mat if mat_frame is None else mat_frame
                mp = self.mat if mat_pane is None else mat_pane
                # openings get their own subpart tag (rule 5)
                keep = self.s.tag
                self.s.tag = 'windows' if kind == 'window' else 'doors'
                for k in range(4):
                    a, b = k, (k + 1) % 4
                    self._q(oc[a], oc[b], ic[b], ic[a], mf)   # face ring
                    self._q(ic[a], ic[b], rc[b], rc[a], mf)   # jamb return
                self._q(rc[0], rc[1], rc[2], rc[3], mp)       # pane
                self.s.tag = keep


# ---------------------------------------------------------------------------
# The dingbat
# ---------------------------------------------------------------------------

def _material(name):
    m = bpy.data.materials.get(name)
    if m is None:
        m = bpy.data.materials.new(name)
    return m


#: material slot order for every dingbat mesh.
_MATS = ["la_stucco", "la_trim", "la_glass", "la_concrete", "la_metal",
         "la_gypsum", "la_plywood", "la_resin"]
(M_STUCCO, M_TRIM, M_GLASS, M_CONCRETE, M_METAL,
 M_GYPSUM, M_PLYWOOD, M_RESIN) = range(8)


def _window_lines(width, cols, margin, win_w):
    """X lines: [0, margin, j0a, j0b, ..., width-margin, width] with windows
    centred in equal bays between the margins."""
    lines = [0.0, margin]
    span = width - 2.0 * margin
    bay = span / cols
    jambs = []
    for c in range(cols):
        centre = margin + bay * (c + 0.5)
        jambs.append((centre - win_w / 2.0, centre + win_w / 2.0))
        lines.extend(jambs[-1])
    lines.extend([width - margin, width])
    return sorted(set(lines)), jambs


def build_dingbat(p, rng):
    """Build the dingbat per the module topology plan. Returns objects."""
    W, D = p["width"], p["depth"]
    floors = p["floors"]
    cols = p["window_cols"]
    cd = min(5.0, D * 0.45)          # carport depth
    margin = 1.0                     # end margins on the facade
    win_w = min(1.5, (W - 2 * margin) / cols * 0.6)
    frame = 0.07

    # ---- z levels ----------------------------------------------------------
    z_grade, z_sill1, z_head1, z_soffit = 0.0, 0.9, 2.1, 2.45
    slab, spandrel, win_h, head = 0.30, 0.90, 1.20, 0.30
    zl = [z_grade, z_sill1, z_head1, z_soffit]
    z = z_soffit
    upper_rows = []                  # (sill, head) per upper floor
    floor_bands = []                 # (slab_lo, slab_hi, band_top) per floor
    for _f in range(max(1, floors - 1)):
        slab_lo = z
        z += slab                    # slab/fascia band top
        zl.append(z)
        s = z + spandrel
        h = s + win_h
        upper_rows.append((s, h))
        zl.extend([s, h])
        z = h + head
        zl.append(z)
        floor_bands.append((slab_lo, slab_lo + slab, z))
    z_roof = z
    z_par = z + 0.4
    zl.extend([z_par])
    zl = sorted(set(zl))

    t = 0.15                         # parapet ring width -- ALSO a wall line:
    # the cap ring's outer-edge verts at x=t / W-t must exist in the wall top
    # edges or they land mid-edge (T-junction, caught by the auditor).
    xl, xjambs = _window_lines(W, cols, margin, win_w)
    xl = sorted(set(xl) | {t, W - t})
    yl = sorted({0.0, cd, D, t, D - t})

    shell = _Shell()

    has_carport = p["carport"]
    # ---- front wall (y=0, faces -Y). With a carport: fascia + upper floors
    # only (the ground is the void). Without one the building sits on grade:
    # full-height wall, ground window row, centre-bay entry door. ------------
    front_z = [v for v in zl if v >= z_soffit] if has_carport else zl

    def near(u0, vals):
        return any(abs(u0 - v) < 1e-6 for v in vals)

    jamb_x = [a for (a, _b) in xjambs]

    def front_classify(u0, zc0):
        for (sll, _hh) in upper_rows:
            if abs(zc0 - sll) < 1e-6 and near(u0, jamb_x):
                return 'window'
        if not has_carport:
            # grounded: every bay is a unit entry -- full-height door
            # (grade cell + sill-band cell stack into one opening).
            if zc0 < z_head1 - 1e-6 and near(u0, jamb_x):
                return 'door'
        return 'wall'

    shell.tag = 'facade_front'
    _Wall(shell, (0, 0, 0), (1, 0, 0), xl, front_z, (0, -1, 0),
          M_STUCCO).fill(front_classify, frame=frame,
                         mat_frame=M_TRIM, mat_pane=M_GLASS)

    # ---- back wall (y=D, faces +Y): full height, windows on all floors -----
    back_z = zl

    spandrel_tops = [hi for (_lo, hi, _bt) in floor_bands]

    def back_classify(u0, zc0):
        if abs(zc0 - z_sill1) < 1e-6 and near(u0, jamb_x):
            return 'window'
        # upper units: the walkway door -- spandrel cell + window cell stack
        # into a door with a transom light over it.
        if near(u0, jamb_x):
            for st in spandrel_tops:
                if abs(zc0 - st) < 1e-6:
                    return 'door'
            for (sll, _hh) in upper_rows:
                if abs(zc0 - sll) < 1e-6:
                    return 'door'
        return 'wall'

    shell.tag = 'facade_back'
    _Wall(shell, (0, D, 0), (1, 0, 0), xl, back_z, (0, 1, 0),
          M_STUCCO).fill(back_classify, frame=frame,
                         mat_frame=M_TRIM, mat_pane=M_GLASS)

    # ---- side walls (x=0 faces -X; x=W faces +X): plain full-height grids --
    def plain(u0, zc0):
        del u0, zc0
        return 'wall'

    shell.tag = 'facade_side'
    _Wall(shell, (0, 0, 0), (0, 1, 0), yl, zl, (-1, 0, 0), M_STUCCO).fill(plain)
    _Wall(shell, (W, 0, 0), (0, 1, 0), yl, zl, (1, 0, 0), M_STUCCO).fill(plain)
    shell.tag = 'parapet'

    # ---- carport liner: recessed ground wall + soffit. A SEPARATE SHELL --
    # welding its edges into the side/front wall faces would put 3 faces on
    # one edge (non-manifold, auditor-caught); a clean abutting shell is the
    # quality bar's sanctioned form.
    liner = _Shell() if has_carport else None
    ground_z = [v for v in zl if v <= z_soffit]

    def ground_classify(u0, zc0):
        # every unit bay is a door: these are the ground apartments' entries.
        if zc0 < z_head1 - 1e-6 and near(u0, jamb_x):
            return 'door'
        return 'wall'

    if has_carport:
        liner.tag = 'doors'
        _Wall(liner, (0, cd, 0), (1, 0, 0), xl, ground_z, (0, -1, 0),
              M_STUCCO).fill(ground_classify, frame=frame,
                             mat_frame=M_TRIM, mat_pane=M_TRIM)
        liner.tag = 'carport'
        soffit_y = [v for v in yl if v <= cd]
        for iy in range(len(soffit_y) - 1):
            for iu in range(len(xl) - 1):
                x0, x1 = xl[iu], xl[iu + 1]
                y0, y1 = soffit_y[iy], soffit_y[iy + 1]
                liner.quad((x0, y0, z_soffit), (x1, y0, z_soffit),
                           (x1, y1, z_soffit), (x0, y1, z_soffit), M_CONCRETE)

    # ---- parapet cap + inner drop + roof plane -----------------------------
    rx = sorted({0.0, t, W - t, W} | set(v for v in xl if t < v < W - t))
    ry = sorted({0.0, t, D - t, D} | set(v for v in yl if t < v < D - t))

    def ring_cell(v0, v1, lo, hi):
        return v1 <= lo + 1e-6 or v0 >= hi - 1e-6

    shell.tag = 'parapet'
    for iy in range(len(ry) - 1):
        for ix in range(len(rx) - 1):
            x0, x1 = rx[ix], rx[ix + 1]
            y0, y1 = ry[iy], ry[iy + 1]
            on_ring = ring_cell(x0, x1, t, W - t) or ring_cell(y0, y1, t, D - t)
            zc = z_par if on_ring else z_roof
            m = M_TRIM if on_ring else M_CONCRETE
            shell.quad((x0, y0, zc), (x0, y1, zc), (x1, y1, zc), (x1, y0, zc), m)
    # inner drop: perimeter of the inset rect, z_par -> z_roof
    inner_x = [v for v in rx if t - 1e-6 <= v <= W - t + 1e-6]
    inner_y = [v for v in ry if t - 1e-6 <= v <= D - t + 1e-6]
    for i in range(len(inner_x) - 1):
        x0, x1 = inner_x[i], inner_x[i + 1]
        shell.quad((x0, t, z_par), (x1, t, z_par),
                   (x1, t, z_roof), (x0, t, z_roof), M_TRIM)
        shell.quad((x1, D - t, z_par), (x0, D - t, z_par),
                   (x0, D - t, z_roof), (x1, D - t, z_roof), M_TRIM)
    for i in range(len(inner_y) - 1):
        y0, y1 = inner_y[i], inner_y[i + 1]
        shell.quad((t, y1, z_par), (t, y0, z_par),
                   (t, y0, z_roof), (t, y1, z_roof), M_TRIM)
        shell.quad((W - t, y0, z_par), (W - t, y1, z_par),
                   (W - t, y1, z_roof), (W - t, y0, z_roof), M_TRIM)

    mats = [_material(n) for n in _MATS]
    body = shell.to_object("LA_Dingbat_Body", mats)
    liner_ob = liner.to_object("LA_Dingbat_Carport", mats) if has_carport else None

    # ---- posts (separate closed shells) ------------------------------------
    post_ob = None
    if has_carport:
        posts = _Shell()
        posts.tag = 'carport'
        n_posts = max(2, p["carport_bays"] + 1)
        px = 0.14
        for i in range(n_posts):
            x = margin + (W - 2 * margin) * (i / (n_posts - 1))
            x = min(max(x, px), W - px)
            _box(posts, (x - px / 2, 0.30, 0.0),
                 (x + px / 2, 0.30 + px, z_soffit), M_METAL)
        post_ob = posts.to_object("LA_Dingbat_Posts", mats)

    # ---- rear stair tower: stringer-carried flights + posted landings (see
    # the ticket's rev-2 topology plan). Each flight: two SHEARED-BOX
    # stringers (planar 6-quad prisms) carrying a folded tread/riser strip,
    # underside closed by one sloped soffit quad -- the sawtooth profile that
    # would be an ngon end-cap terminates INSIDE the stringer faces, which is
    # what stringers are for. Landings are slab plates on four continuous
    # posts to grade. All separate clean shells. -----------------------------
    stair = _Shell()
    stair.tag = 'steps'
    tread, s_w, st = 0.26, 1.1, 0.09          # tread run, lane width, stringer
    side = p["stair_side"]
    sdir = -1.0 if side == 'left' else 1.0
    laneB_x0 = (-s_w - 0.05) if side == 'left' else (W + 0.05)
    laneA_x0 = laneB_x0 + sdir * (s_w + 0.06)
    wd = 1.25
    levels = [0.0] + [lo + 0.12 for (lo, _hi, _bt) in floor_bands]
    y_arr = D + wd * 0.45

    def flight(x0, y_from, y_to, z_from, z_to):
        """One flight between two levels along y (either direction)."""
        x1 = x0 + s_w
        n = max(3, int(round(abs(z_to - z_from) / 0.185)))
        rise = (z_to - z_from) / n
        run = (y_to - y_from) / n
        # stringers: parallel to the nosing line (z_from..z_to + cover).
        for sx0, sx1 in ((x0, x0 + st), (x1 - st, x1)):
            _sheared_box(stair, min(sx0, sx1), max(sx0, sx1),
                         y_from, y_to, z_from + 0.06, z_to + 0.06,
                         0.34, M_METAL, 'steps')
        # tread/riser strip between the stringers, inset 1 mm: its boundary
        # hides inside the joint but never lands on a stringer edge (the
        # auditor's 16 T-junctions per flight when coincident).
        ix0, ix1 = x0 + st + 0.001, x1 - st - 0.001
        for k2 in range(n):
            ya2, yb2 = y_from + run * k2, y_from + run * (k2 + 1)
            zlo2, zhi2 = z_from + rise * k2, z_from + rise * (k2 + 1)
            stair.quad((ix0, ya2, zlo2), (ix1, ya2, zlo2),
                       (ix1, ya2, zhi2), (ix0, ya2, zhi2), M_CONCRETE)  # riser
            stair.quad((ix0, ya2, zhi2), (ix1, ya2, zhi2),
                       (ix1, yb2, zhi2), (ix0, yb2, zhi2), M_CONCRETE)  # tread
        # soffit: one sloped quad closing the underside.
        stair.quad((ix0, y_from, z_from - 0.10), (ix1, y_from, z_from - 0.10),
                   (ix1, y_to, z_to - 0.10), (ix0, y_to, z_to - 0.10),
                   M_CONCRETE)

    land_y0 = None
    for i in range(len(levels) - 1):
        zb, zt = levels[i], levels[i + 1]
        zh = (zb + zt) / 2.0
        n_half = max(3, int(round((zh - zb) / 0.185)))
        run = tread * n_half
        # flight A (outer lane): away from the building, zb -> mid.
        flight(min(laneA_x0, laneA_x0 + s_w * 0) + 0.0, y_arr, y_arr + run, zb, zh)
        # half landing plate.
        lx0 = min(laneA_x0, laneB_x0)
        lx1 = max(laneA_x0, laneB_x0) + s_w
        land_y0 = y_arr + run + 0.003
        _box(stair, (lx0, land_y0, zh - 0.10), (lx1, land_y0 + s_w, zh),
             M_CONCRETE)
        # flight B (inner lane): back toward the building, mid -> walkway.
        flight(laneB_x0, land_y0 + s_w - 0.003, y_arr, zh, zt)

    # four continuous posts carry the stacked half-landings, grade -> top;
    # 20 mm proud of the slab corners (touching, no shared planes).
    stair.tag = 'columns'
    top_land = (levels[-2] + levels[-1]) / 2.0 if len(levels) > 1 else levels[-1]
    lx0 = min(laneA_x0, laneB_x0)
    lx1 = max(laneA_x0, laneB_x0) + s_w
    for (px2, py2) in ((lx0 - 0.10, land_y0 - 0.10), (lx1 + 0.02, land_y0 - 0.10),
                       (lx0 - 0.10, land_y0 + s_w + 0.02),
                       (lx1 + 0.02, land_y0 + s_w + 0.02)):
        _box(stair, (px2, py2, 0.0), (px2 + 0.08, py2 + 0.08, top_land - 0.10),
             M_METAL)
    # walkway-extension posts: carry the arrival edge over the lanes.
    wpx = lx0 - 0.10 if side == 'left' else lx1 + 0.02
    for wy in (D + 0.05, D + wd - 0.13):
        _box(stair, (wpx, wy, 0.0), (wpx + 0.08, wy + 0.08,
             floor_bands[-1][0] - 0.02), M_METAL)
    stair_ob = stair.to_object("LA_Dingbat_Stair", mats)

    # ---- INTERIOR MODE (rule 1): inner wall liners, slabs, partitions,
    # rear walkway -- everything structural, just-built, walkable. Each piece
    # is a clean separate shell; inner liners mirror the exterior openings as
    # VOID cells so the existing jamb returns read as reveals from inside. ---
    interior_obs = []
    if p["mode"] == 'interior':
        wt = 0.15                              # wall thickness (liner offset)
        inner = _Shell()
        inner.tag = 'interior_walls'

        def void_openings(classify):
            def cls(u0, zc0):
                k = classify(u0, zc0)
                return 'void' if k in ('window', 'door') else k
            return cls

        ix0, ix1 = wt, W - wt
        iy1 = D - wt
        in_xl = sorted({ix0, ix1} | {v for v in xl if ix0 < v < ix1})
        top_z = zl[-1] - 0.4                   # underside of roof structure
        in_zl = sorted({v for v in zl if v <= top_z} | {top_z})
        lo_zl = [v for v in in_zl if v <= z_soffit]
        hi_zl = [v for v in in_zl if v >= z_soffit]
        # cd+wt joins the full-depth y-lines: the ground side-liner tops out
        # there, and without a matching vert the upper segment's bottom edge
        # takes a T-junction (auditor-caught, x2 sides).
        in_yl_full = sorted({wt, iy1, cd + wt} | {v for v in yl if wt < v < iy1})
        in_yl_gnd = sorted({cd + wt, iy1} | {v for v in yl if cd + wt < v < iy1})
        # inner faces point INTO the rooms (normals reversed vs exterior).
        if has_carport:
            # ground: recessed behind the carport; upper: at the facade.
            _Wall(inner, (0, cd + wt, 0), (1, 0, 0), in_xl, lo_zl, (0, 1, 0),
                  M_GYPSUM).fill(void_openings(ground_classify))
            _Wall(inner, (0, wt, 0), (1, 0, 0), in_xl, hi_zl, (0, 1, 0),
                  M_GYPSUM).fill(void_openings(front_classify))
        else:
            _Wall(inner, (0, wt, 0), (1, 0, 0), in_xl, in_zl, (0, 1, 0),
                  M_GYPSUM).fill(void_openings(front_classify))
        _Wall(inner, (0, iy1, 0), (1, 0, 0), in_xl, in_zl, (0, -1, 0),
              M_GYPSUM).fill(void_openings(back_classify))
        for sx, snrm in ((ix0, (1, 0, 0)), (ix1, (-1, 0, 0))):
            if has_carport:
                _Wall(inner, (sx, 0, 0), (0, 1, 0), in_yl_gnd, lo_zl, snrm,
                      M_GYPSUM).fill(plain)
                _Wall(inner, (sx, 0, 0), (0, 1, 0), in_yl_full, hi_zl, snrm,
                      M_GYPSUM).fill(plain)
            else:
                _Wall(inner, (sx, 0, 0), (0, 1, 0), in_yl_full, in_zl, snrm,
                      M_GYPSUM).fill(plain)
        interior_obs.append(inner.to_object("LA_Dingbat_Interior", mats))

        # slabs: one closed plate per storey (top face = floor, underside =
        # ceiling below). Inset 1 mm from the liner so shells never share
        # planes. Ground slab only without a carport (else the soffit is it).
        slabs = _Shell()
        slabs.tag = 'slabs'
        e = 0.001
        gy0 = (cd + wt) if has_carport else wt
        slab_boxes = [(z_grade + (0.0 if has_carport else 0.0),
                       z_grade + 0.12, gy0)]
        slab_boxes += [(lo, hi, wt) for (lo, hi, _bt) in floor_bands]
        for (z_lo, z_hi, y0s) in slab_boxes:
            _box(slabs, (ix0 + e, y0s + e, z_lo), (ix1 - e, iy1 - e, z_hi),
                 M_CONCRETE)
        interior_obs.append(slabs.to_object("LA_Dingbat_Slabs", mats))

        # unit partitions: one wall between window bays per storey, spanning
        # liner to liner. Thin closed boxes; carried on both floors so they
        # read load-bearing. 2 mm shy of the liners (no shared planes).
        parts = _Shell()
        parts.tag = 'partitions'
        pt = 0.10
        # one storey per floor band: that band's slab top to the next band's
        # slab bottom (roof structure underside for the last).
        storeys = [(z_grade + 0.12, floor_bands[0][0],
                    ((cd + wt) if has_carport else wt) + 0.002)]
        for i2, (lo, hi, _bt) in enumerate(floor_bands):
            nxt = floor_bands[i2 + 1][0] if i2 + 1 < len(floor_bands) else top_z
            storeys.append((hi, nxt, wt + 0.002))
        for k in range(1, cols):
            xb = margin + (W - 2 * margin) * (k / cols)
            for (zlo, zhi, ys) in storeys:
                _box(parts, (xb - pt / 2, ys, zlo),
                     (xb + pt / 2, iy1 - 0.002, zhi - 0.001), M_GYPSUM)
        interior_obs.append(parts.to_object("LA_Dingbat_Partitions", mats))

        # rear walkway serving the upper units: slab + square posts.
        walk = _Shell()
        walk.tag = 'walkway'
        wd = 1.25
        s_w2 = 1.1
        wx0 = (-2 * s_w2 - 0.10) if p["stair_side"] == 'left' else 0.0
        wx1 = W if p["stair_side"] == 'left' else (W + 2 * s_w2 + 0.10)
        for wi, (lo, hi, _bt) in enumerate(floor_bands):
            _box(walk, (wx0, D + 0.003 + 0.002 * wi, lo),
                 (wx1 + 0.001 * wi, D + wd, lo + 0.12), M_CONCRETE)
        walk.tag = 'columns'
        for i in range(3):
            x = 0.2 + (W - 0.4) * (i / 2.0)
            _box(walk, (x - 0.06, D + wd - 0.14, 0.0),
                 (x + 0.06, D + wd - 0.02, floor_bands[-1][0]), M_METAL)
        interior_obs.append(walk.to_object("LA_Dingbat_Walkway", mats))

    # ---- STORY OPTIONS (rule 3, off by default; theme: abandonment /
    # regime / resistance) ---------------------------------------------------
    story = _Shell()
    story.tag = 'story'
    if p["all_broken"]:
        # shattered panes: 2 quad shards per window + plywood behind some.
        for (a2, b2) in xjambs:
            for (sll, hh) in upper_rows:
                cx, cz = (a2 + b2) / 2.0, (sll + hh) / 2.0
                for k in range(2):
                    ang = rng.uniform(-0.6, 0.6)
                    dx = 0.28 * (1 if k == 0 else -1)
                    story.quad((cx + dx - 0.18, 0.06, cz - 0.22 + ang * 0.1),
                               (cx + dx + 0.10, 0.06, cz - 0.30 - ang * 0.1),
                               (cx + dx + 0.16, 0.065, cz + 0.24 + ang * 0.1),
                               (cx + dx - 0.12, 0.065, cz + 0.30 - ang * 0.1),
                               M_GLASS)
                if rng.random() < 0.45:
                    _box(story, (a2 + 0.02, 0.10, sll + 0.02),
                         (b2 - 0.02, 0.14, hh - 0.02), M_PLYWOOD)
    if p["sealed_unit"] and cols >= 2:
        # one unit's openings swallowed in aberration resin: a bulged panel
        # (3x3 quad grid, centre pushed proud) over the bay, floor to head.
        k = rng.randrange(cols)
        a2, b2 = xjambs[k]
        x0, x1 = a2 - 0.25, b2 + 0.25
        zlo = z_soffit + slab - 0.1 if has_carport else z_grade
        zhi = upper_rows[0][1] + 0.25
        xs = [x0, x0 + (x1 - x0) / 3, x0 + 2 * (x1 - x0) / 3, x1]
        zs = [zlo, zlo + (zhi - zlo) / 3, zlo + 2 * (zhi - zlo) / 3, zhi]
        for iz2 in range(3):
            for ix2 in range(3):
                bulge = 0.22 if (ix2 == 1 and iz2 == 1) else 0.10
                def co(xx, zz, bb):
                    return (xx, -bb, zz)
                c00 = co(xs[ix2], zs[iz2], 0.10 if (ix2 in (0,) or iz2 in (0,)) else bulge)
                c10 = co(xs[ix2+1], zs[iz2], 0.10 if (ix2+1 in (3,) or iz2 in (0,)) else bulge)
                c11 = co(xs[ix2+1], zs[iz2+1], 0.10 if (ix2+1 in (3,) or iz2+1 in (3,)) else bulge)
                c01 = co(xs[ix2], zs[iz2+1], 0.10 if (ix2 in (0,) or iz2+1 in (3,)) else bulge)
                story.quad(c00, c10, c11, c01, M_RESIN)
    if p["rooftop_roost"]:
        # pigeon-loft signal post: shed + cage posts + perch, rear roof corner.
        rx0, ry0 = W - 3.2, D - 2.6
        _box(story, (rx0, ry0, z_roof + 0.001), (rx0 + 1.6, ry0 + 1.2, z_roof + 1.1),
             M_PLYWOOD)
        story.quad((rx0 - 0.05, ry0 - 0.05, z_roof + 1.35),
                   (rx0 + 1.65, ry0 - 0.05, z_roof + 1.25),
                   (rx0 + 1.65, ry0 + 1.25, z_roof + 1.25),
                   (rx0 - 0.05, ry0 + 1.25, z_roof + 1.35), M_METAL)
        for (px2, py2) in ((rx0 - 0.02, ry0 - 0.02), (rx0 + 1.58, ry0 - 0.02),
                           (rx0 - 0.02, ry0 + 1.18), (rx0 + 1.58, ry0 + 1.18)):
            _box(story, (px2, py2, z_roof + 0.001), (px2 + 0.05, py2 + 0.05,
                 z_roof + 1.3), M_METAL)
        _box(story, (rx0 + 2.2, ry0 + 0.4, z_roof + 0.001),
             (rx0 + 2.26, ry0 + 0.46, z_roof + 2.1), M_METAL)
        _box(story, (rx0 + 1.85, ry0 + 0.40, z_roof + 1.95),
             (rx0 + 2.65, ry0 + 0.46, z_roof + 2.01), M_METAL)
    story_ob = story.to_object("LA_Dingbat_Story", mats)         if story._face_tags or story.bm.faces else None

    # ---- awnings + AC units -------------------------------------------------
    extras = _Shell()
    extras.tag = 'awnings'
    if p["awnings"]:
        for (a, b) in xjambs:
            for (s, h) in upper_rows:
                zh = h + 0.05
                extras.quad((a - 0.1, 0.0, zh), (b + 0.1, 0.0, zh),
                            (b + 0.1, -0.45, zh - 0.28),
                            (a - 0.1, -0.45, zh - 0.28), M_METAL)
                extras.quad((b + 0.1, -0.45, zh - 0.28),
                            (a - 0.1, -0.45, zh - 0.28),
                            (a - 0.1, -0.45, zh - 0.40),
                            (b + 0.1, -0.45, zh - 0.40), M_METAL)
    extras.tag = 'ac_units'
    for (a, b) in xjambs:
        for (s, h) in upper_rows:
            if rng.random() < p["ac_units"]:
                cx = (a + b) / 2.0
                _box(extras, (cx - 0.30, -0.28, s + 0.02),
                     (cx + 0.30, 0.10, s + 0.42), M_METAL)
    extras_ob = extras.to_object("LA_Dingbat_Extras", mats)

    # ---- engine tags --------------------------------------------------------
    out = [body, liner_ob, post_ob, stair_ob, extras_ob, story_ob] + interior_obs
    out = [ob for ob in out if ob is not None]
    for ob in out:
        ob["ferrum_lightmap_res"] = 0 if ob in (extras_ob, story_ob) else 128
    return out


SPEC = [
    params.MODE_PARAM,
    dict(name="width", type='FLOAT', default=14.0, min=8.0, max=22.0,
         unit='LENGTH', desc="Building width"),
    dict(name="depth", type='FLOAT', default=9.0, min=6.0, max=14.0,
         unit='LENGTH', desc="Building depth"),
    dict(name="floors", type='INT', default=2, min=1, max=3),
    dict(name="carport_bays", type='INT', default=4, min=0, max=6),
    dict(name="window_cols", type='INT', default=4, min=2, max=8),
    dict(name="awnings", type='BOOL', default=True),
    dict(name="ac_units", type='FLOAT', default=0.4, min=0.0, max=1.0,
         desc="Window AC density"),
    dict(name="stair_side", type='ENUM', default='left',
         items=('left', 'right')),
    dict(name="facade_style", type='ENUM', default='plain',
         items=('plain', 'starburst', 'mansard', 'tiki', 'script')),
    # monotony breaker (rule 2): no carport => the building sits on grade
    # with a ground window row + entry door.
    dict(name="carport", type='BOOL', default=True,
         desc="Tuck-under carport; off = building drops to grade"),
    # story options (rule 3) -- off by default, thematically coherent.
    dict(name="all_broken", type='BOOL', default=False,
         desc="Abandonment: every window shattered, many boarded"),
    dict(name="sealed_unit", type='BOOL', default=False,
         desc="Regime: one unit's openings swallowed in aberration resin"),
    dict(name="rooftop_roost", type='BOOL', default=False,
         desc="Resistance: pigeon-loft signal post on the roof"),
]

params.register_tool(idname="la_dingbat", label="Dingbat Apartment",
                     family="Residential", build=build_dingbat, spec=SPEC)


def smoke():
    """Build with defaults, audit every mesh, return summaries."""
    import random
    objs = build_dingbat(params.defaults("la_dingbat"), random.Random(0))
    out = {}
    for ob in objs:
        rep = topology.validate_object(ob)
        out[ob.name] = (topology.ok(rep), topology.summarize(rep))
    return objs, out
