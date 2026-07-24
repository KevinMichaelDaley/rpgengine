"""Shared welded-grid geometry machinery for every LA generator (rpg-2lyk).

Extracted from the A1 dingbat (the pattern-setter) so all tools build on the
same primitives and the same quality bar:

  _Shell       one welded bmesh under construction; vert cache at 0.1 mm,
               dominant-axis planar UVs at uniform texel density on export
               (rule 4), face tags -> vertex groups (rule 5).
  _Wall        one planar wall grid (origin + u direction + global z lines).
               Openings are grid CELLS, never booleans; runs absorb in BOTH
               axes (foreign grid lines crossing a span re-merge); THICK
               walls carry outer+inner faces with cut-through reveals.
  _box / _sheared_box / _wall_solid / _wall_L
               closed-solid primitives (planar quads only).
  _material / _MATS / M_*
               the shared material-slot order for every emitted mesh.
  _window_lines
               centred jamb lines for equal bays with per-bay widths.

Validation contract: la.topology.validate() reports 100% quads, 0 ngons /
tris, 0 non-manifold, 0 doubles, 0 T-junctions on every emitted mesh
(documented exemptions only, e.g. typography fills).
"""
import bmesh
import bpy
from mathutils import Vector

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

    def __init__(self, recalc=False):
        self.bm = bmesh.new()
        self._cache = {}
        self.tag = None           # current subpart tag (rule 5)
        self._face_tags = {}      # bmesh face -> tag string
        self.recalc = recalc      # closed-solid shells: recalc normals

    def vert(self, co):
        # Quantized weld cache with BOUNDARY-SAFE lookup: two emitters of
        # the "same" vert can differ by up to ~2e-6 (float32 storage,
        # different arithmetic paths), and a bay pitch like 4.1825 puts
        # every bay line exactly on a bucket edge -- the pair then rounds
        # into DIFFERENT buckets and leaves an unwelded double no matter
        # how tight a "near the edge" heuristic is. So on a cache miss,
        # probe all 26 neighbouring buckets and reuse any stored vert
        # within 1.2x the audit epsilon (intentional separations are
        # >= 2 mm, twenty buckets away -- never probed).
        kx = round(co[0] / _WELD)
        ky = round(co[1] / _WELD)
        kz = round(co[2] / _WELD)
        v = self._cache.get((kx, ky, kz))
        if v is not None:
            return v
        tol = _WELD * 1.2
        for dx in (0, -1, 1):
            for dy in (0, -1, 1):
                for dz in (0, -1, 1):
                    if dx == 0 and dy == 0 and dz == 0:
                        continue
                    v = self._cache.get((kx + dx, ky + dy, kz + dz))
                    if v is not None and \
                            abs(v.co[0] - co[0]) < tol and \
                            abs(v.co[1] - co[1]) < tol and \
                            abs(v.co[2] - co[2]) < tol:
                        return v
        v = self.bm.verts.new(co)
        self._cache[(kx, ky, kz)] = v
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
        if self.recalc:
            # all-closed-solid shells: make face normals consistently outward
            # regardless of authoring winding (open strips must NOT do this).
            bmesh.ops.recalc_face_normals(self.bm, faces=self.bm.faces)
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


def _hexa(shell, base, ev, ew, v0, v1, w0, w1, z0, z1, mat=0):
    """Closed 6-quad box spanned by two horizontal unit Vectors ``ev``/``ew``
    and global z: corner = base + ev*v + ew*w + (0,0,z).  Winding adapts to
    the pair's handedness so normals always face out.  The oriented sibling
    of _box -- door leafs (hinged at any ajar angle) and boxes on angled
    walls use it."""
    def pt(v, w, z):
        p = base + ev * v + ew * w
        return (p.x, p.y, p.z + z)
    flip = (ev.x * ew.y - ev.y * ew.x) < 0.0

    def q(a, b, c, d):
        if flip:
            shell.quad(a, d, c, b, mat)
        else:
            shell.quad(a, b, c, d, mat)
    q(pt(v0, w0, z0), pt(v0, w1, z0), pt(v1, w1, z0), pt(v1, w0, z0))  # bottom
    q(pt(v0, w0, z1), pt(v1, w0, z1), pt(v1, w1, z1), pt(v0, w1, z1))  # top
    q(pt(v0, w0, z0), pt(v1, w0, z0), pt(v1, w0, z1), pt(v0, w0, z1))  # -w
    q(pt(v0, w1, z0), pt(v0, w1, z1), pt(v1, w1, z1), pt(v1, w1, z0))  # +w
    q(pt(v0, w0, z0), pt(v0, w0, z1), pt(v0, w1, z1), pt(v0, w1, z0))  # -v
    q(pt(v1, w0, z0), pt(v1, w1, z0), pt(v1, w1, z1), pt(v1, w0, z1))  # +v


def _wall_solid(shell, axis, at, a0, a1, zlo, zhi, t=0.09, door=None, mat=0):
    """ONE manifold all-quad wall solid along @p axis ('x' or 'y'), spanning
    @p a0..a1 at cross position @p at, thickness @p t. @p door = (g0, g1, dh)
    cuts a doorway INTO the topology: the faces subdivide on the door lines
    (5 quads per side, split end caps, jamb quads, header soffit) -- one
    closed mesh, nothing overlapping, nothing to z-fight."""
    if a1 - a0 < 0.05:
        return

    def P(u, w, z):
        return (u, at + w, z) if axis == 'x' else (at + w, u, z)

    q = shell.quad
    if door is None:
        q(P(a0, 0, zlo), P(a1, 0, zlo), P(a1, 0, zhi), P(a0, 0, zhi), mat)
        q(P(a0, t, zlo), P(a0, t, zhi), P(a1, t, zhi), P(a1, t, zlo), mat)
        q(P(a0, 0, zhi), P(a1, 0, zhi), P(a1, t, zhi), P(a0, t, zhi), mat)
        q(P(a0, 0, zlo), P(a0, t, zlo), P(a1, t, zlo), P(a1, 0, zlo), mat)
        q(P(a0, 0, zlo), P(a0, 0, zhi), P(a0, t, zhi), P(a0, t, zlo), mat)
        q(P(a1, 0, zlo), P(a1, t, zlo), P(a1, t, zhi), P(a1, 0, zhi), mat)
        return
    g0, g1, dh = door
    g0 = max(a0 + 0.05, g0)
    g1 = min(a1 - 0.05, g1)
    dh = min(dh, zhi - 0.08)
    for w, flip in ((0.0, False), (t, True)):
        # 5 face quads: legs (2 rows each) + header cell; door cell omitted.
        cells = [(a0, g0, zlo, dh), (a0, g0, dh, zhi),
                 (g1, a1, zlo, dh), (g1, a1, dh, zhi),
                 (g0, g1, dh, zhi)]
        for (u0, u1, z0, z1) in cells:
            pts = [P(u0, w, z0), P(u1, w, z0), P(u1, w, z1), P(u0, w, z1)]
            if flip:
                pts.reverse()
            q(*pts, mat)
    # top cap: 3 segments (verts at g0/g1 exist on the face top rows).
    for (u0, u1) in ((a0, g0), (g0, g1), (g1, a1)):
        q(P(u0, 0, zhi), P(u1, 0, zhi), P(u1, t, zhi), P(u0, t, zhi), mat)
    # leg undersides.
    for (u0, u1) in ((a0, g0), (g1, a1)):
        q(P(u0, 0, zlo), P(u0, t, zlo), P(u1, t, zlo), P(u1, 0, zlo), mat)
    # end caps, split at the door head line (the faces have that vert).
    for u, flip in ((a0, False), (a1, True)):
        for (z0, z1) in ((zlo, dh), (dh, zhi)):
            pts = [P(u, 0, z0), P(u, 0, z1), P(u, t, z1), P(u, t, z0)]
            if flip:
                pts.reverse()
            q(*pts, mat)
    # jambs + header soffit lining the doorway.
    for u, flip in ((g0, True), (g1, False)):
        pts = [P(u, 0, zlo), P(u, 0, dh), P(u, t, dh), P(u, t, zlo)]
        if flip:
            pts.reverse()
        q(*pts, mat)
    q(P(g0, 0, dh), P(g1, 0, dh), P(g1, t, dh), P(g0, t, dh), mat)


def _wall_L(shell, cx, cy, y_end, x_end, zlo, zhi, t=0.09, door=None, mat=0):
    """ONE merged L-wall: arm A along y (at x=cx, from the corner to y_end)
    + arm B along x (at y=cy, from the corner to x_end), PROPERLY BOXED at
    the corner -- outline-prism topology, every face subdivided so the
    corner block welds (separate boxes step/z-fight at a visible convex
    corner). @p door = (g0, g1, dh) cuts a framed doorway into arm B."""
    h = t / 2.0
    right = x_end > cx                      # arm B extends toward +x?
    up = y_end > cy
    ya, yb = (cy - h, cy + h)               # arm B face planes
    xa, xb = (cx - h, cx + h)               # arm A face planes
    # arm B x-range: far end .. THROUGH the corner to arm A's far plane.
    bx_far = x_end
    # concave (inner) face stops at the FIRST arm-A plane in the approach
    # direction; the convex (outer) face runs THROUGH to the far plane.
    bx_near_in = xb if right else xa
    bx_near_out = xa if right else xb
    ay_near = yb if up else ya              # arm A starts at arm B's inner
    ay_far = y_end
    q = shell.quad

    def face_x_cells(x_lo, x_hi):
        """Sorted x cell lines for arm B faces, incl. corner + door lines."""
        lines = {x_lo, x_hi, min(xa, xb), max(xa, xb)}
        if door is not None:
            lines.update((door[0], door[1]))
        return sorted(v for v in lines if min(x_lo, x_hi) - 1e-9 <= v
                      <= max(x_lo, x_hi) + 1e-9)

    def emit_face_y(yp, x_lo, x_hi, flip):
        xs = face_x_cells(x_lo, x_hi)
        for i in range(len(xs) - 1):
            x0c, x1c = xs[i], xs[i + 1]
            in_door = (door is not None and
                       x0c >= door[0] - 1e-9 and x1c <= door[1] + 1e-9)
            rows = ((zlo, door[2]), (door[2], zhi)) if door is not None                 else ((zlo, zhi),)
            for (r0, r1) in rows:
                if in_door and r1 <= door[2] + 1e-9:
                    continue                     # the doorway itself
                pts = [(x0c, yp, r0), (x1c, yp, r0),
                       (x1c, yp, r1), (x0c, yp, r1)]
                if flip:
                    pts.reverse()
                q(*pts, mat)

    # arm B faces (outer runs through the corner; inner stops at arm A inner).
    emit_face_y(ya, bx_far, bx_near_out, flip=False)
    emit_face_y(yb, bx_far, bx_near_in, flip=True)
    # arm A faces, split at the corner line so top quads weld.
    zrows = ((zlo, door[2]), (door[2], zhi)) if door is not None         else ((zlo, zhi),)
    for (xp, y0f, flip) in ((bx_near_out, ya, right), (bx_near_in, ay_near,
                                                       not right)):
        segs = ((ya, yb), (yb, ay_far)) if xp == bx_near_out and up             else ((y0f, ay_far),)
        for (s0, s1) in segs:
            # rows split at the door head so arm B's corner-column verts
            # weld here too (they landed mid-edge otherwise).
            for (r0, r1) in zrows:
                pts = [(xp, s0, r0), (xp, s1, r0), (xp, s1, r1), (xp, s0, r1)]
                if flip:
                    pts.reverse()
                q(*pts, mat)
    # top: arm B strip (to the corner block edge), corner block, arm A strip.
    bx_block = min(xa, xb) if not right else max(xa, xb)
    for (x0c, x1c, y0c, y1c) in (
            (min(bx_far, bx_block), max(bx_far, bx_block), ya, yb),
            (min(xa, xb), max(xa, xb), ya, yb),
            (min(xa, xb), max(xa, xb), ay_near, ay_far)):
        # split the arm B strip at door lines so its verts weld the faces.
        xs = face_x_cells(x0c, x1c) if y0c == ya and y1c == yb and             abs(x1c - x0c) > t + 1e-6 else [x0c, x1c]
        for i in range(len(xs) - 1):
            q((xs[i], y0c, zhi), (xs[i + 1], y0c, zhi),
              (xs[i + 1], y1c, zhi), (xs[i], y1c, zhi), mat)
        for i in range(len(xs) - 1):
            x0d, x1d = xs[i], xs[i + 1]
            if (door is not None and y0c == ya and y1c == yb and
                    x0d >= door[0] - 1e-9 and x1d <= door[1] + 1e-9):
                continue                          # no bottom under the door
            q((x0d, y0c, zlo), (x0d, y1c, zlo),
              (x1d, y1c, zlo), (x1d, y0c, zlo), mat)
    # end caps.
    rows = ((zlo, door[2]), (door[2], zhi)) if door is not None else ((zlo, zhi),)
    for (r0, r1) in rows:
        pts = [(bx_far, ya, r0), (bx_far, ya, r1),
               (bx_far, yb, r1), (bx_far, yb, r0)]
        if right:
            pts.reverse()
        q(*pts, mat)
    for (r0, r1) in zrows:
        pts = [(xa, ay_far, r0), (xb, ay_far, r0), (xb, ay_far, r1),
               (xa, ay_far, r1)]
        if not up:
            pts.reverse()
        q(*pts, mat)
    # door jambs + header soffit.
    if door is not None:
        g0, g1, dh = door
        for (gx, flip) in ((g0, False), (g1, True)):
            pts = [(gx, ya, zlo), (gx, yb, zlo), (gx, yb, dh), (gx, ya, dh)]
            if flip:
                pts.reverse()
            q(*pts, mat)
        q((g0, ya, dh), (g1, ya, dh), (g1, yb, dh), (g0, yb, dh), mat)


class _Wall:
    """One planar wall grid: origin + u direction (horizontal) + z up.

    ``cells(iu, iz)`` classifies each cell: 'wall', 'void', or
    ('window'|'door', recess_sign) -- openings get the inset-ring treatment.
    Face winding: CCW seen from ``outward``.
    """

    def __init__(self, shell, origin, u_dir, u_lines, z_lines, outward, mat=0,
                 thickness=0.0, inner_zmax=None, inner_mat=None):
        self.s = shell
        self.o = Vector(origin)
        self.u = Vector(u_dir).normalized()
        self.ul = list(u_lines)
        self.zl = list(z_lines)
        self.n = Vector(outward).normalized()
        self.mat = mat
        # THICK walls (interior mode): one mesh carries the outer face, the
        # inner face (at depth=thickness, faces reversed), and openings CUT
        # THROUGH -- the reveal is the wall's own cut faces, the door floor
        # runs through the thickness. No liner shell, no cavity.
        self.th = thickness
        self.inner_zmax = inner_zmax if inner_zmax is not None else 1e30
        self.inner_mat = self.mat if inner_mat is None else inner_mat
        # inner-face clip range along u: adjacent walls' inner faces must
        # stop at the inset corner (unclipped they run INTO each other's
        # thickness -- coplanar crossings, non-manifold).
        self.inner_u0 = -1e30
        self.inner_u1 = 1e30
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

    def _qi(self, a, b, c, d, mat=None):
        """Inner-face quad: reversed relative to _q (normal points inward)."""
        m = self.inner_mat if mat is None else mat
        if self.flip:
            self.s.quad(a, b, c, d, m)
        else:
            self.s.quad(a, d, c, b, m)

    def fill(self, classify, frame=0.07, recess=0.08,
             mat_frame=None, mat_pane=None, leaf=None):
        """``leaf``: optional callback ``(wall, u0, u1, z0, z1, depth)``
        invoked once per merged doorL opening AFTER its reveal is cut --
        the B1.4 door-leaf emitters hang real doors in the hole.  None
        (the default) keeps the opening bare (walk-through)."""
        for iz in range(len(self.zl) - 1):
            for iu in range(len(self.ul) - 1):
                kind = classify(self.ul[iu], self.zl[iz])
                if kind in ('void', 'doorU'):
                    continue        # doorU is consumed by its doorL below
                u0, u1 = self.ul[iu], self.ul[iu + 1]
                z0, z1 = self.zl[iz], self.zl[iz + 1]
                if kind == 'void_in':
                    # Outer face present, inner face void: the loggia's
                    # inner-skin hole is WIDER than the facade hole by the
                    # cheek thickness, so these strip cells keep their
                    # street-side skin only.
                    self._q(self._co(u0, z0), self._co(u1, z0),
                            self._co(u1, z1), self._co(u0, z1))
                    continue
                if kind == 'doorL':
                    # TALL walk-through door: the rough opening IS the full
                    # rect (head = the grid head line) -- every boundary on
                    # grid lines, no face rings. Thin walls get a `recess`-
                    # deep reveal; THICK walls cut clean through: jambs +
                    # header span the full thickness and the FLOOR strip
                    # carries the slab out to the exterior plane.
                    # absorb ALL contiguous doorL cells SIDEWAYS first: a
                    # foreign grid line landing inside the jambs (e.g. a
                    # window jamb crossing the door span) sliced every such
                    # door into a slender slot + a wall sliver.
                    if iu > 0 and classify(self.ul[iu - 1], z0) == 'doorL':
                        continue    # consumed by the run to its left
                    ju = iu + 1
                    while (ju + 1 < len(self.ul) and
                           classify(self.ul[ju], z0) == 'doorL'):
                        ju += 1
                    u1r = self.ul[ju]
                    # then absorb ALL contiguous doorU rows above (a global
                    # z-line insertion once silently shortened every door).
                    jz = iz + 1
                    while (jz + 1 < len(self.zl) and
                           classify(u0, self.zl[jz]) == 'doorU'):
                        jz += 1
                    z1t = self.zl[jz]
                    depth = self.th if self.th > 0.0 else recess
                    zs = [z0] + [zv for zv in self.zl
                                 if z0 + 1e-6 < zv < z1t - 1e-6] + [z1t]
                    us = [u0] + [uv for uv in self.ul
                                 if u0 + 1e-6 < uv < u1r - 1e-6] + [u1r]
                    keep = self.s.tag
                    self.s.tag = 'doors'
                    mfd = self.mat if mat_frame is None else mat_frame
                    for zi in range(len(zs) - 1):
                        za, zb = zs[zi], zs[zi + 1]
                        self._q(self._co(u0, za, 0.0), self._co(u0, za, depth),
                                self._co(u0, zb, depth), self._co(u0, zb, 0.0), mfd)
                        self._q(self._co(u1r, za, depth), self._co(u1r, za, 0.0),
                                self._co(u1r, zb, 0.0), self._co(u1r, zb, depth), mfd)
                    # header + floor strip SEGMENTED at interior u lines so
                    # the wall cells above/below always weld.
                    for ui2 in range(len(us) - 1):
                        ua2, ub2 = us[ui2], us[ui2 + 1]
                        self._q(self._co(ua2, z1t, depth), self._co(ub2, z1t, depth),
                                self._co(ub2, z1t, 0.0), self._co(ua2, z1t, 0.0), mfd)
                        if self.th > 0.0:
                            self._q(self._co(ua2, z0, 0.0), self._co(ua2, z0, depth),
                                    self._co(ub2, z0, depth), self._co(ub2, z0, 0.0),
                                    M_CONCRETE)   # through-floor strip
                    if leaf is not None:
                        leaf(self, u0, u1r, z0, z1t, depth)
                    self.s.tag = keep
                    continue
                if kind == 'wall':
                    self._q(self._co(u0, z0), self._co(u1, z0),
                            self._co(u1, z1), self._co(u0, z1))
                    if (self.th > 0.0 and z0 < self.inner_zmax - 1e-6 and
                            u0 >= self.inner_u0 - 1e-6 and
                            u1 <= self.inner_u1 + 1e-6):
                        keep2 = self.s.tag
                        self.s.tag = 'interior_walls'
                        self._qi(self._co(u0, z0, self.th),
                                 self._co(u1, z0, self.th),
                                 self._co(u1, z1, self.th),
                                 self._co(u0, z1, self.th))
                        self.s.tag = keep2
                    continue
                # opening cell (window / window_awning): may span SEVERAL
                # contiguous rows AND columns (global lines split cells in
                # both axes; matching only one cell made squat half-windows
                # vertically and 0.45 m sliver windows horizontally -- rear
                # door/bath jamb lines cross the front window spans).
                # Rings/jambs are SEGMENTED at every interior grid line so
                # neighbouring wall cells' verts always weld.
                if iz > 0 and classify(u0, self.zl[iz - 1]) == kind:
                    continue        # consumed by the run below
                if iu > 0 and classify(self.ul[iu - 1], z0) == kind:
                    continue        # consumed by the run to its left
                jz = iz + 1
                while (jz + 1 < len(self.zl) and
                       classify(u0, self.zl[jz]) == kind):
                    jz += 1
                z1w = self.zl[jz]
                ju = iu + 1
                while (ju + 1 < len(self.ul) and
                       classify(self.ul[ju], z0) == kind):
                    ju += 1
                u1w = self.ul[ju]
                fu0, fu1 = u0 + frame, u1w - frame
                fz0, fz1 = z0 + frame, z1w - frame
                mids = [zv for zv in self.zl if z0 + 1e-6 < zv < z1w - 1e-6]
                zs = [z0] + mids + [z1w]
                mids_u = [uv for uv in self.ul if u0 + 1e-6 < uv < u1w - 1e-6]
                us = [u0] + mids_u + [u1w]

                def zc(v):   # clamp an outer z to the inner ring range
                    return min(max(v, fz0), fz1)

                def ucf(v):  # clamp an outer u to the inner ring range
                    return min(max(v, fu0), fu1)

                mf = self.mat if mat_frame is None else mat_frame
                mp = self.mat if mat_pane is None else mat_pane
                keep = self.s.tag
                self.s.tag = 'windows'
                # bottom + top face bands, segmented at interior u lines.
                for ui2 in range(len(us) - 1):
                    ua, ub = us[ui2], us[ui2 + 1]
                    self._q(self._co(ua, z0), self._co(ub, z0),
                            self._co(ucf(ub), fz0), self._co(ucf(ua), fz0), mf)
                    self._q(self._co(ucf(ua), fz1), self._co(ucf(ub), fz1),
                            self._co(ub, z1w), self._co(ua, z1w), mf)
                # side face rings, segmented at interior z lines.
                for si2 in range(len(zs) - 1):
                    za, zb = zs[si2], zs[si2 + 1]
                    self._q(self._co(u0, za), self._co(fu0, zc(za)),
                            self._co(fu0, zc(zb)), self._co(u0, zb), mf)
                    self._q(self._co(fu1, zc(za)), self._co(u1w, za),
                            self._co(u1w, zb), self._co(fu1, zc(zb)), mf)
                # jamb returns: top/bottom strips segmented at interior u
                # lines, sides segmented at interior z lines.
                inu = sorted({fu0, fu1} | {ucf(v) for v in mids_u})
                for ui2 in range(len(inu) - 1):
                    ua, ub = inu[ui2], inu[ui2 + 1]
                    self._q(self._co(ua, fz0), self._co(ub, fz0),
                            self._co(ub, fz0, recess), self._co(ua, fz0, recess), mf)
                    self._q(self._co(ua, fz1, recess), self._co(ub, fz1, recess),
                            self._co(ub, fz1), self._co(ua, fz1), mf)
                inz = sorted({fz0, fz1} | {zc(v) for v in mids})
                for si2 in range(len(inz) - 1):
                    za, zb = inz[si2], inz[si2 + 1]
                    self._q(self._co(fu0, za), self._co(fu0, za, recess),
                            self._co(fu0, zb, recess), self._co(fu0, zb), mf)
                    self._q(self._co(fu1, za, recess), self._co(fu1, za),
                            self._co(fu1, zb), self._co(fu1, zb, recess), mf)
                # pane: 1 mm-inset ISLAND. window_awning (the dingbat bath
                # sash) hinges at the TOP and swings OUT past the wall plane.
                # window_shutter (storefront roll-up): FINE slat curtain --
                # the pitch targets ~0.10 m and SCALES to divide the opening
                # evenly (n = round(h/0.10)), read as alternating-depth
                # micro-corrugation. window_dock (loading dock): heavy
                # sectional door -- coarse ~0.45 m panels with protruding
                # rib strips between them (the taller greebles).
                e2 = 0.001
                if kind in ('window_shutter', 'window_dock'):
                    keep2s = self.s.tag
                    self.s.tag = 'shutters' if kind == 'window_shutter' \
                        else 'doors'
                    hgt = (fz1 - e2) - (fz0 + e2)
                    if kind == 'window_shutter':
                        nseg = max(4, int(round(hgt / 0.10)))
                        step = hgt / nseg
                        for si3 in range(nseg):
                            za3 = fz0 + e2 + step * si3
                            sd = recess * (0.5 if si3 % 2 == 0 else 0.42)
                            self._q(self._co(fu0 + e2, za3, sd),
                                    self._co(fu1 - e2, za3, sd),
                                    self._co(fu1 - e2, za3 + step, sd),
                                    self._co(fu0 + e2, za3 + step, sd),
                                    M_SHUTTER)
                    else:
                        rib = 0.05
                        nseg = max(2, int(round(hgt / 0.45)))
                        step = hgt / nseg
                        for si3 in range(nseg):
                            za3 = fz0 + e2 + step * si3
                            self._q(self._co(fu0 + e2, za3, recess * 0.55),
                                    self._co(fu1 - e2, za3, recess * 0.55),
                                    self._co(fu1 - e2, za3 + step - rib,
                                             recess * 0.55),
                                    self._co(fu0 + e2, za3 + step - rib,
                                             recess * 0.55), M_METAL)
                            self._q(self._co(fu0 + e2, za3 + step - rib,
                                             recess * 0.2),
                                    self._co(fu1 - e2, za3 + step - rib,
                                             recess * 0.2),
                                    self._co(fu1 - e2, za3 + step,
                                             recess * 0.2),
                                    self._co(fu0 + e2, za3 + step,
                                             recess * 0.2), M_METAL)
                        # B1.4 hardware (rpg-20cn): jamb roller tracks,
                        # hinge plates at every panel joint, a bottom lift
                        # handle -- boxes floating between the panel plane
                        # (recess*0.55) and the rib plane (recess*0.2), so
                        # nothing is ever coplanar with the curtain quads.
                        for ut in (fu0 + 0.006, fu1 - 0.050):
                            _hexa(self.s, self.o, self.u, self.n,
                                  ut, ut + 0.044,
                                  -recess * 0.46, -recess * 0.30,
                                  fz0 + 0.004, fz1 - 0.004, M_METAL)
                        span = (fu1 - 0.10) - (fu0 + 0.10)
                        for si4 in range(1, nseg):
                            zj = fz0 + e2 + step * si4 - rib * 0.5
                            for kp in range(3):
                                pa = fu0 + 0.10 + (span - 0.10) * kp / 2.0
                                _hexa(self.s, self.o, self.u, self.n,
                                      pa, pa + 0.10,
                                      -recess * 0.16, -recess * 0.08,
                                      zj - 0.045, zj + 0.045, M_METAL)
                        ucn = (fu0 + fu1) * 0.5
                        _hexa(self.s, self.o, self.u, self.n,
                              ucn - 0.12, ucn + 0.12,
                              -recess * 0.50, -recess * 0.30,
                              fz0 + 0.10, fz0 + 0.16, M_METAL)
                    self.s.tag = keep2s
                elif kind == 'window_awning':
                    swing = -0.22
                    pc = [self._co(fu0 + e2, fz0 + e2, swing),
                          self._co(fu1 - e2, fz0 + e2, swing),
                          self._co(fu1 - e2, fz1 - e2, recess),
                          self._co(fu0 + e2, fz1 - e2, recess)]
                else:
                    pc = [self._co(fu0 + e2, fz0 + e2, recess),
                          self._co(fu1 - e2, fz0 + e2, recess),
                          self._co(fu1 - e2, fz1 - e2, recess),
                          self._co(fu0 + e2, fz1 - e2, recess)]
                if kind not in ('window_shutter', 'window_dock'):
                    self._q(pc[0], pc[1], pc[2], pc[3], mp)
                if self.th > 0.0 and z0 < self.inner_zmax:
                    # THICK: inner face ring + reveal, same 2-axis segmentation.
                    for ui2 in range(len(us) - 1):
                        ua, ub = us[ui2], us[ui2 + 1]
                        self._q(self._co(ua, z0, self.th),
                                self._co(ucf(ua), fz0, self.th),
                                self._co(ucf(ub), fz0, self.th),
                                self._co(ub, z0, self.th), mf)
                        self._q(self._co(ucf(ua), fz1, self.th),
                                self._co(ua, z1w, self.th),
                                self._co(ub, z1w, self.th),
                                self._co(ucf(ub), fz1, self.th), mf)
                    for si2 in range(len(zs) - 1):
                        za, zb = zs[si2], zs[si2 + 1]
                        self._q(self._co(fu0, zc(za), self.th), self._co(u0, za, self.th),
                                self._co(u0, zb, self.th), self._co(fu0, zc(zb), self.th), mf)
                        self._q(self._co(u1w, za, self.th), self._co(fu1, zc(za), self.th),
                                self._co(fu1, zc(zb), self.th), self._co(u1w, zb, self.th), mf)
                    # reveal from the inner ring to the jamb line.
                    for ui2 in range(len(inu) - 1):
                        ua, ub = inu[ui2], inu[ui2 + 1]
                        self._q(self._co(ub, fz0, self.th), self._co(ua, fz0, self.th),
                                self._co(ua, fz0, recess), self._co(ub, fz0, recess), mf)
                        self._q(self._co(ua, fz1, self.th), self._co(ub, fz1, self.th),
                                self._co(ub, fz1, recess), self._co(ua, fz1, recess), mf)
                    for si2 in range(len(inz) - 1):
                        za, zb = inz[si2], inz[si2 + 1]
                        self._q(self._co(fu0, za, self.th), self._co(fu0, zb, self.th),
                                self._co(fu0, zb, recess), self._co(fu0, za, recess), mf)
                        self._q(self._co(fu1, zb, self.th), self._co(fu1, za, self.th),
                                self._co(fu1, za, recess), self._co(fu1, zb, recess), mf)
                self.s.tag = keep


# ---------------------------------------------------------------------------
# The dingbat
# ---------------------------------------------------------------------------

#: PBR palette: name -> (base RGB, roughness, metallic). Values follow the
#: standard PBR reference charts (Substance/Quixel): dielectrics metallic=0;
#: galvanized steel/roll-up 1.0 at rough ~0.45-0.55; aged asphalt ~0.95;
#: architectural glass rough ~0.08; enamel sign cabinets ~0.25; road paint
#: semi-matte ~0.6; raw soil/sand fully rough.
_MAT_PBR = {
    "la_stucco":       ((0.74, 0.70, 0.62), 0.90, 0.0),
    "la_trim":         ((0.85, 0.83, 0.78), 0.55, 0.0),
    "la_glass":        ((0.22, 0.52, 0.58), 0.08, 0.0, 0.35),   # teal plate glass: chromatic transmission
    "la_concrete":     ((0.54, 0.54, 0.52), 0.85, 0.0),
    "la_metal":        ((0.62, 0.64, 0.66), 0.45, 1.0),
    "la_gypsum":       ((0.82, 0.80, 0.77), 0.95, 0.0),
    "la_plywood":      ((0.55, 0.42, 0.28), 0.80, 0.0),
    "la_resin":        ((0.80, 0.74, 0.62), 0.30, 0.0),
    "la_sign_a":       ((0.72, 0.14, 0.10), 0.25, 0.0),
    "la_sign_b":       ((0.10, 0.38, 0.52), 0.25, 0.0),
    "la_sign_c":       ((0.86, 0.68, 0.18), 0.25, 0.0),
    "la_shutter":      ((0.58, 0.60, 0.62), 0.55, 1.0),
    "la_asphalt":      ((0.060, 0.060, 0.065), 0.95, 0.0),
    "la_paint_white":  ((0.82, 0.82, 0.78), 0.60, 0.0),
    "la_paint_yellow": ((0.78, 0.58, 0.10), 0.60, 0.0),
    "la_patch":        ((0.035, 0.035, 0.040), 0.90, 0.0),
    "la_sand":         ((0.76, 0.66, 0.48), 1.00, 0.0),
    "la_scorch":       ((0.03, 0.03, 0.03), 0.95, 0.0),
    "la_soil":         ((0.30, 0.23, 0.16), 1.00, 0.0),
}


def _material(name):
    """Fetch-or-create a palette material, with its PBR values applied to
    the Principled BSDF (the engine exporter reads tint/roughness/metallic
    straight off it) and mirrored to the solid-viewport display."""
    m = bpy.data.materials.get(name)
    if m is None:
        m = bpy.data.materials.new(name)
    pbr = _MAT_PBR.get(name)
    if pbr is not None:
        (col, rough, metal) = pbr[:3]
        alpha = pbr[3] if len(pbr) > 3 else 1.0
        m.use_nodes = True
        bsdf = next((n for n in m.node_tree.nodes
                     if n.type == 'BSDF_PRINCIPLED'), None)
        if bsdf is not None:
            bsdf.inputs["Base Color"].default_value = (col[0], col[1],
                                                       col[2], 1.0)
            bsdf.inputs["Roughness"].default_value = rough
            bsdf.inputs["Metallic"].default_value = metal
            bsdf.inputs["Alpha"].default_value = alpha
        if alpha < 1.0:
            m.blend_method = 'BLEND'      # viewport reads as glass too
        m.diffuse_color = (col[0], col[1], col[2], alpha)
        m.roughness = rough
        m.metallic = metal
    return m


#: material slot order for every LA-generator mesh (shared indices).
#: APPEND-ONLY: existing generators bake these indices into face data.
_MATS = ["la_stucco", "la_trim", "la_glass", "la_concrete", "la_metal",
         "la_gypsum", "la_plywood", "la_resin",
         "la_sign_a", "la_sign_b", "la_sign_c", "la_shutter",
         "la_asphalt", "la_paint_white", "la_paint_yellow", "la_patch",
         "la_sand", "la_scorch", "la_soil"]
(M_STUCCO, M_TRIM, M_GLASS, M_CONCRETE, M_METAL,
 M_GYPSUM, M_PLYWOOD, M_RESIN,
 M_SIGN_A, M_SIGN_B, M_SIGN_C, M_SHUTTER) = range(12)
(M_ASPHALT, M_PAINT_W, M_PAINT_Y, M_PATCH,
 M_SAND, M_SCORCH, M_SOIL) = range(12, 19)


def _window_lines(width, cols, margin, widths):
    """X lines: [0, margin, j0a, j0b, ..., width-margin, width] with windows
    centred in equal bays between the margins. @p widths is PER BAY -- the
    fenestration is floor-plan dependent (living bays wide, bedroom bays
    narrow), not one uniform sash."""
    lines = [0.0, margin]
    span = width - 2.0 * margin
    bay = span / cols
    jambs = []
    for c in range(cols):
        centre = margin + bay * (c + 0.5)
        w2 = widths[c] / 2.0
        jambs.append((centre - w2, centre + w2))
        lines.extend(jambs[-1])
    lines.extend([width - margin, width])
    return sorted(set(lines)), jambs


