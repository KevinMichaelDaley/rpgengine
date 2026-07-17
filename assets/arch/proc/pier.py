"""Parametric wall-pier generator (ticket rpg-k04n, epic rpg-pm1c).

A *wall pier* is an engaged masonry pier: a rectangular shaft that projects from
a wall face (its back is flush against the wall, at Y=0) and stands on a wider
*plinth* -- a short base block that projects further than the shaft on the front
and both sides. It reads as the massive brick/stone pier of a Romanesque arcade
or blind-arcade wall, and shares the WALL's coursed masonry material (build via
``masonry_uv=True`` -> ``arch._masonry_course_uvs``).

Conventions (see README.md):
  * Z up (Blender). Local origin at the pier's back-centre on the floor:
    X = 0 is the pier centreline, Y = 0 is the wall face (the pier projects +Y),
    Z = 0 is the floor. So an instance drops straight onto a wall at (wx, wy, 0).
  * One watertight, quad-only surface. Every face is a quad off a shared vertex
    grid (cut lines at the shaft's +/-X faces, its front +Y face, and the plinth
    top Z), so the step from plinth to shaft resolves as clean rectangular quads
    -- no stacked shells, no n-gons.

Parameters (build_wall_pier):
  width            shaft width  (X)         -- the face you see from the room.
  depth            shaft projection (Y) from the wall face.
  height           total height (Z), floor to pier top.
  plinth_height    Z extent of the plinth base block.
  plinth_project   how far the plinth projects BEYOND the shaft, on the front
                   (+Y) and each side (+/-X). 0 => no plinth step (a plain box).
"""
import bmesh
import bpy


def _obj_from_bm(name, bm, collection):
    """Realise *bm* as a new mesh object in *collection* (or the active one)."""
    me = bpy.data.meshes.new(name)
    bm.normal_update()
    bm.to_mesh(me)
    bm.free()
    obj = bpy.data.objects.new(name, me)
    (collection or bpy.context.collection).objects.link(obj)
    return obj


def build_wall_pier(name="wall_pier", width=0.7, depth=0.5, height=3.2,
                    plinth_height=0.35, plinth_project=0.09, collection=None,
                    masonry_uv=False):
    """Build an engaged wall pier (flush back at Y=0) with a projecting plinth.
    Returns the created object. Quad-only + watertight."""
    sw = max(width, 1e-3)
    sd = max(depth, 1e-3)
    h2 = max(height, plinth_height + 1e-3)
    h1 = max(min(plinth_height, h2 - 1e-3), 0.0)
    pp = max(plinth_project, 0.0)
    pw = sw + 2.0 * pp          # plinth full width (X)
    pd = sd + pp                # plinth projection (Y)

    # Shared vertex grid: cut lines at the shaft's -/+X faces and front, the
    # plinth's -/+X faces and front, the back (Y=0), and the plinth-top Z. A
    # (x, y, z) -> BMVert cache auto-welds coincident verts so the shell stays
    # watertight and quad-only.
    bm = bmesh.new()
    cache = {}

    def v(x, y, z):
        key = (round(x, 6), round(y, 6), round(z, 6))
        vert = cache.get(key)
        if vert is None:
            vert = bm.verts.new((x, y, z))
            cache[key] = vert
        return vert

    def quad(a, b, c, d, mi=0):
        """mi = material slot: 0 shaft (wall masonry), 1 plinth (its own course)."""
        f = bm.faces.new((v(*a), v(*b), v(*c), v(*d)))
        f.material_index = mi

    xl_s, xr_s = -sw / 2.0, sw / 2.0        # shaft X faces
    xl_p, xr_p = -pw / 2.0, pw / 2.0        # plinth X faces
    has_plinth = pp > 1e-6 and h1 > 1e-6

    if not has_plinth:
        # Plain box shaft, back flush at Y=0.
        _box(quad, xl_s, xr_s, 0.0, sd, 0.0, h2)
        obj = _obj_from_bm(name, bm, collection)
        _post(obj, masonry_uv)
        return obj

    _build_stepped(quad, xl_s, xr_s, xl_p, xr_p, sd, pd, h1, h2)
    obj = _obj_from_bm(name, bm, collection)
    _post(obj, masonry_uv)
    return obj


def _box(quad, x0, x1, y0, y1, z0, z1):
    """Six-quad axis-aligned box (outward normals)."""
    quad((x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0))  # bottom -Z
    quad((x0, y0, z1), (x0, y1, z1), (x1, y1, z1), (x1, y0, z1))  # top +Z
    quad((x0, y0, z0), (x0, y0, z1), (x1, y0, z1), (x1, y0, z0))  # back -Y
    quad((x0, y1, z0), (x1, y1, z0), (x1, y1, z1), (x0, y1, z1))  # front +Y
    quad((x0, y0, z0), (x0, y1, z0), (x0, y1, z1), (x0, y0, z1))  # left -X
    quad((x1, y0, z0), (x1, y0, z1), (x1, y1, z1), (x1, y1, z0))  # right +X


def _build_stepped(quad, xl_s, xr_s, xl_p, xr_p, sd, pd, h1, h2):
    """The full stepped shell. Every face is subdivided along the SHARED grid
    (x cuts at the shaft faces, y cut at the shaft front, z cut at the plinth
    top) so neighbouring faces share whole edges -- no T-junctions."""
    xs = [xl_p, xl_s, xr_s, xr_p]
    ys = [0.0, sd, pd]

    P = 1   # plinth material slot; shaft stays slot 0.

    # Plinth floor (bottom, -Z): full 3x2 grid.
    for ix in range(3):
        for iy in range(2):
            x0, x1 = xs[ix], xs[ix + 1]; y0, y1 = ys[iy], ys[iy + 1]
            quad((x0, y0, 0.0), (x0, y1, 0.0), (x1, y1, 0.0), (x1, y0, 0.0), P)

    # Plinth walls (z 0->h1), front + two sides, cut to match the ledge grid.
    for ix in range(3):                                   # front +Y, cut at shaft X
        x0, x1 = xs[ix], xs[ix + 1]
        quad((x0, pd, 0.0), (x0, pd, h1), (x1, pd, h1), (x1, pd, 0.0), P)
    for iy in range(2):                                   # left/right -/+X, cut at y=sd
        y0, y1 = ys[iy], ys[iy + 1]
        quad((xl_p, y0, 0.0), (xl_p, y1, 0.0), (xl_p, y1, h1), (xl_p, y0, h1), P)   # left -X
        quad((xr_p, y0, 0.0), (xr_p, y0, h1), (xr_p, y1, h1), (xr_p, y1, 0.0), P)   # right +X

    # Plinth-top ledge (z = h1): plinth rect minus the shaft footprint cell.
    for iy in range(2):
        for ix in range(3):
            if ix == 1 and iy == 0:      # shaft cell -- the shaft rises here.
                continue
            x0, x1 = xs[ix], xs[ix + 1]; y0, y1 = ys[iy], ys[iy + 1]
            quad((x0, y0, h1), (x1, y0, h1), (x1, y1, h1), (x0, y1, h1), P)  # +Z

    # Shaft walls (z h1->h2): front + two sides (back in the T-face).
    quad((xl_s, sd, h1), (xl_s, sd, h2), (xr_s, sd, h2), (xr_s, sd, h1))         # shaft front +Y
    quad((xl_s, 0.0, h1), (xl_s, sd, h1), (xl_s, sd, h2), (xl_s, 0.0, h2))       # shaft left -X
    quad((xr_s, 0.0, h1), (xr_s, 0.0, h2), (xr_s, sd, h2), (xr_s, sd, h1))       # shaft right +X
    # Shaft top (+Z).
    quad((xl_s, 0.0, h2), (xl_s, sd, h2), (xr_s, sd, h2), (xr_s, 0.0, h2))

    # Stepped back T-face (Y = 0), z 0->h2. Cut at shaft X lines and z=h1 so it
    # is 4 quads: full plinth width below h1 (plinth slot), shaft width above.
    quad((xl_p, 0.0, 0.0), (xl_p, 0.0, h1), (xl_s, 0.0, h1), (xl_s, 0.0, 0.0), P)   # lower-left
    quad((xl_s, 0.0, 0.0), (xl_s, 0.0, h1), (xr_s, 0.0, h1), (xr_s, 0.0, 0.0), P)   # lower-centre
    quad((xr_s, 0.0, 0.0), (xr_s, 0.0, h1), (xr_p, 0.0, h1), (xr_p, 0.0, 0.0), P)   # lower-right
    quad((xl_s, 0.0, h1), (xl_s, 0.0, h2), (xr_s, 0.0, h2), (xr_s, 0.0, h1))        # upper (shaft width)


def _post(obj, masonry_uv):
    """Recalculate normals + optional coursed masonry UVs (shared wall material)."""
    import bmesh as _bm
    me = obj.data
    b = _bm.new()
    b.from_mesh(me)
    _bm.ops.recalc_face_normals(b, faces=b.faces)
    b.to_mesh(me)
    b.free()
    if me.uv_layers.active is None:      # _masonry_course_uvs overwrites; needs a layer.
        me.uv_layers.new(name="UVMap")
    if masonry_uv:
        import arch
        arch._masonry_course_uvs(obj)
