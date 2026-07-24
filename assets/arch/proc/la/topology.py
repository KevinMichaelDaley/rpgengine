"""Production-topology validation (rpg-2lyk quality bar, doc section 0).

Every generator's smoke check runs ``validate()`` on its output and asserts
``ok(report)`` before the mesh is ever shown for wireframe sign-off. The
checks encode the non-negotiables:

  * all-quad (tri/ngon counts reported; quads_pct target 100)
  * no non-manifold edges, no zero-area faces, no doubled verts
  * no T-junctions: a vertex lying ON another edge's interior without being
    connected to it (the classic mid-edge landing) is reported with its
    location so the offending loop can be found in the wireframe.

All functions take a ``bmesh`` (from an object or built directly) so tools
can validate BEFORE committing to a mesh datablock.
"""
import bmesh
from mathutils import Vector, kdtree

#: Distance under which two verts count as doubled / a vert counts as ON an edge.
EPS_WELD = 1.0e-4
#: Faces with area under this are "zero-area" (degenerate).
EPS_AREA = 1.0e-8


def validate(bm, eps_weld=EPS_WELD, eps_area=EPS_AREA):
    """Audit @p bm and return a report dict (see keys below). Read-only.

    Report keys: n_verts, n_edges, n_faces, n_quads, n_tris, n_ngons,
    quads_pct, nonmanifold_edges, zero_area_faces, doubled_verts,
    t_junctions (list of (vert_index, edge_index, world_co tuple)).
    """
    # ensure_lookup_table enables bm.verts[i]; index_update assigns .index
    # (fresh bmesh elements carry -1 until then -- both are required).
    bm.verts.ensure_lookup_table(); bm.verts.index_update()
    bm.edges.ensure_lookup_table(); bm.edges.index_update()
    bm.faces.ensure_lookup_table(); bm.faces.index_update()

    n_quads = sum(1 for f in bm.faces if len(f.verts) == 4)
    n_tris = sum(1 for f in bm.faces if len(f.verts) == 3)
    n_ngons = len(bm.faces) - n_quads - n_tris

    nonmanifold = [e.index for e in bm.edges if not e.is_manifold and not e.is_boundary]
    zero_area = [f.index for f in bm.faces if f.calc_area() < eps_area]

    # Doubles: KD over verts; any pair closer than eps_weld.
    kd = kdtree.KDTree(len(bm.verts))
    for v in bm.verts:
        kd.insert(v.co, v.index)
    kd.balance()
    doubled = set()
    for v in bm.verts:
        for (_co, idx, dist) in kd.find_range(v.co, eps_weld):
            if idx != v.index:
                doubled.add(tuple(sorted((idx, v.index))))

    # T-junctions: for every edge, find verts within its bounding sphere and
    # test exact point-segment distance; a hit that is not an endpoint and not
    # topologically connected to the edge is a junction.
    t_junctions = []
    for e in bm.edges:
        a, b = e.verts[0].co, e.verts[1].co
        mid = (a + b) * 0.5
        radius = (a - b).length * 0.5 + eps_weld
        seg = b - a
        seg_len2 = seg.length_squared
        if seg_len2 < eps_weld * eps_weld:
            continue
        linked = {v.index for v in e.verts}
        for f in e.link_faces:
            linked.update(v.index for v in f.verts)
        for (_co, idx, _dist) in kd.find_range(mid, radius):
            if idx in linked:
                continue
            p = bm.verts[idx].co
            t = max(0.0, min(1.0, (p - a).dot(seg) / seg_len2))
            if 0.0 < t < 1.0 and (a + seg * t - p).length < eps_weld:
                t_junctions.append((idx, e.index, tuple(p)))

    coplanar = coplanar_overlap_faces(bm)

    n_faces = len(bm.faces)
    return {
        "n_verts": len(bm.verts),
        "n_edges": len(bm.edges),
        "n_faces": n_faces,
        "n_quads": n_quads,
        "n_tris": n_tris,
        "n_ngons": n_ngons,
        "quads_pct": (100.0 * n_quads / n_faces) if n_faces else 100.0,
        "nonmanifold_edges": nonmanifold,
        "zero_area_faces": zero_area,
        "doubled_verts": sorted(doubled),
        "t_junctions": t_junctions,
        "coplanar_overlaps": coplanar,
    }


#: Two parallel faces closer than this (plane-to-plane) z-fight on screen.
#: Intentional separations in the kit are >= 2 mm, so 1.5 mm splits clean.
EPS_COPLANAR = 1.5e-3


def _poly_clip_area(pa, pb):
    """Area of the intersection of two convex CCW 2D polygons
    (Sutherland-Hodgman clip of @p pa by @p pb's half-planes)."""
    out = list(pa)
    for i in range(len(pb)):
        cx, cy = pb[i]
        dx, dy = pb[(i + 1) % len(pb)]
        ex, ey = dx - cx, dy - cy
        cur, out = out, []
        if not cur:
            return 0.0
        for j in range(len(cur)):
            px, py = cur[j]
            qx, qy = cur[(j + 1) % len(cur)]
            sp = ex * (py - cy) - ey * (px - cx)   # >= 0: inside
            sq = ex * (qy - cy) - ey * (qx - cx)
            if sp >= 0.0:
                out.append((px, py))
            if (sp > 0.0) != (sq > 0.0) and sp != sq:
                t = sp / (sp - sq)
                out.append((px + (qx - px) * t, py + (qy - py) * t))
    area = 0.0
    for j in range(len(out)):
        ax, ay = out[j]
        bx, by = out[(j + 1) % len(out)]
        area += ax * by - bx * ay
    return abs(area) * 0.5


def coplanar_overlap_faces(bm, eps_plane=EPS_COPLANAR, eps_ang=1.0e-4,
                           eps_area=1.0e-6, cell=0.6):
    """Z-fighting audit: pairs of (near-)parallel faces whose planes are
    closer than @p eps_plane AND whose outlines overlap in area (shared
    edges / corners are legal -- the overlap must exceed ~1 mm^2 after
    clipping).  Candidate pairs come from a face-AABB spatial hash, so
    dense grids stay near-linear.  Faces sharing a vert are exempt (the
    welded-grid case; abutment defects there surface as T-junctions).
    Returns [(face_a, face_b, world_co), ...]."""
    bm.normal_update()      # fresh bmesh faces carry zero normals until this
    bm.verts.index_update()  # and verts all sit at index -1 (breaks the
    bm.faces.index_update()  # shared-vert exemption + reported indices)
    faces = []
    grid = {}
    for f in bm.faces:
        n = f.normal
        if n.length_squared < 0.5:          # degenerate; zero_area covers it
            continue
        c = f.calc_center_median()
        vs = [v.co for v in f.verts]
        lo = [min(v[k] for v in vs) - eps_plane for k in range(3)]
        hi = [max(v[k] for v in vs) + eps_plane for k in range(3)]
        idx = len(faces)
        faces.append((f, n.copy(), n.dot(c), vs,
                      {v.index for v in f.verts}))
        for gx in range(int(lo[0] // cell), int(hi[0] // cell) + 1):
            for gy in range(int(lo[1] // cell), int(hi[1] // cell) + 1):
                for gz in range(int(lo[2] // cell), int(hi[2] // cell) + 1):
                    grid.setdefault((gx, gy, gz), []).append(idx)
    seen = set()
    hits = []
    for bucket in grid.values():
        for i in range(len(bucket)):
            ia = bucket[i]
            fa, na, da, va, vsa = faces[ia]
            for j in range(i + 1, len(bucket)):
                ib = bucket[j]
                key = (ia, ib)
                if key in seen:
                    continue
                seen.add(key)
                fb, nb, db, vb, vsb = faces[ib]
                if vsa & vsb:
                    continue
                dot = na.dot(nb)
                if abs(dot) < 1.0 - eps_ang:
                    continue
                if abs(da - (db if dot > 0.0 else -db)) > eps_plane:
                    continue
                # project both outlines onto the shared plane and clip.
                ax = 0 if abs(na.x) <= abs(na.y) or abs(na.x) <= abs(na.z) \
                    else 1
                t = [0.0, 0.0, 0.0]
                t[ax] = 1.0
                from mathutils import Vector as _V
                e1 = _V(t).cross(na).normalized()
                e2 = na.cross(e1)
                pa = [(v.dot(e1), v.dot(e2)) for v in va]
                pb = [(v.dot(e1), v.dot(e2)) for v in vb]

                def _ccw(p):
                    s = sum(p[k][0] * p[(k + 1) % len(p)][1] -
                            p[(k + 1) % len(p)][0] * p[k][1]
                            for k in range(len(p)))
                    return p if s >= 0.0 else p[::-1]
                if _poly_clip_area(_ccw(pa), _ccw(pb)) > eps_area:
                    co = fa.calc_center_median()
                    hits.append((fa.index, fb.index, tuple(co)))
    return hits


def ok(report, allow_tris=0, allow_boundary=True):
    """True iff @p report meets the quality bar: all quads (up to
    @p allow_tris triangles for genuinely unavoidable cases), zero ngons,
    zero mesh errors, zero T-junctions."""
    del allow_boundary  # boundary edges are legal (openings); kept for API clarity.
    return (report["n_ngons"] == 0 and report["n_tris"] <= allow_tris and
            not report["nonmanifold_edges"] and not report["zero_area_faces"] and
            not report["doubled_verts"] and not report["t_junctions"] and
            not report.get("coplanar_overlaps", []) and
            not report.get("uv_missing", False) and
            not report.get("uv_degenerate_faces", []))


def validate_object(obj, **kw):
    """Convenience: build a bmesh from @p obj's mesh, validate, free. Adds
    UV audit fields (rule 4): uv_missing, uv_degenerate_faces."""
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    try:
        rep = validate(bm, **kw)
        uv = bm.loops.layers.uv.active
        rep["uv_missing"] = uv is None
        degen = []
        if uv is not None:
            for f in bm.faces:
                area2 = 0.0
                loops = f.loops
                for i in range(len(loops)):
                    a2 = loops[i][uv].uv
                    b2 = loops[(i + 1) % len(loops)][uv].uv
                    area2 += a2.x * b2.y - b2.x * a2.y
                if abs(area2) < 1e-9:
                    degen.append(f.index)
        rep["uv_degenerate_faces"] = degen
        return rep
    finally:
        bm.free()


def summarize(report):
    """One-line human summary for smoke-check prints / ticket notes."""
    return ("faces={n_faces} quads={quads_pct:.1f}% tris={n_tris} ngons={n_ngons} "
            "nonmanifold={nm} zero_area={za} doubles={db} t_junctions={tj} "
            "coplanar={cp}").format(
        nm=len(report["nonmanifold_edges"]), za=len(report["zero_area_faces"]),
        db=len(report["doubled_verts"]), tj=len(report["t_junctions"]),
        cp=len(report.get("coplanar_overlaps", [])), **report)


def smoke():
    """Self-test for the coplanar-overlap (z-fighting) audit: synthetic
    bmeshes that must / must not trip the check.  Returns {case: (want,
    got, pass)}; every case must pass."""
    def quad(bm, pts):
        bm.faces.new([bm.verts.new(p) for p in pts])

    def case(build):
        bm = bmesh.new()
        build(bm)
        n = len(coplanar_overlap_faces(bm))
        bm.free()
        return n

    Z = 0.0
    out = {}

    def clean(bm):     # welded 2x1 grid: coplanar neighbours SHARE an edge
        a = [bm.verts.new(p) for p in ((0, 0, Z), (1, 0, Z), (1, 1, Z),
                                       (0, 1, Z))]
        b = [a[1], bm.verts.new((2, 0, Z)), bm.verts.new((2, 1, Z)), a[2]]
        bm.faces.new(a); bm.faces.new(b)
    out["welded_grid"] = (0, case(clean))

    def overlap(bm):   # classic z-fight: coplanar quads overlapping
        quad(bm, ((0, 0, Z), (1, 0, Z), (1, 1, Z), (0, 1, Z)))
        quad(bm, ((0.4, 0.2, Z), (1.4, 0.2, Z), (1.4, 0.8, Z),
                  (0.4, 0.8, Z)))
    out["coplanar_overlap"] = (1, case(overlap))

    def near(bm):      # 0.5 mm apart: still fights on screen
        quad(bm, ((0, 0, Z), (1, 0, Z), (1, 1, Z), (0, 1, Z)))
        quad(bm, ((0.2, 0.2, 5e-4), (0.8, 0.2, 5e-4), (0.8, 0.8, 5e-4),
                  (0.2, 0.8, 5e-4)))
    out["parallel_0p5mm"] = (1, case(near))

    def apart(bm):     # 3 mm apart: legal (the kit's >= 2 mm discipline)
        quad(bm, ((0, 0, Z), (1, 0, Z), (1, 1, Z), (0, 1, Z)))
        quad(bm, ((0.2, 0.2, 3e-3), (0.8, 0.2, 3e-3), (0.8, 0.8, 3e-3),
                  (0.2, 0.8, 3e-3)))
    out["parallel_3mm"] = (0, case(apart))

    def edge_only(bm):  # separate islands sharing only an outline edge
        quad(bm, ((0, 0, Z), (1, 0, Z), (1, 1, Z), (0, 1, Z)))
        quad(bm, ((1, 0, Z), (2, 0, Z), (2, 1, Z), (1, 1, Z)))
    out["edge_touch_only"] = (0, case(edge_only))

    def perp(bm):      # perpendicular intersection: no fight
        quad(bm, ((0, 0, Z), (1, 0, Z), (1, 1, Z), (0, 1, Z)))
        quad(bm, ((0.5, -0.5, -0.5), (0.5, 0.5, -0.5), (0.5, 0.5, 0.5),
                  (0.5, -0.5, 0.5)))
    out["perpendicular"] = (0, case(perp))

    def anti(bm):      # opposite-facing coincident quads (inside-out box lid)
        quad(bm, ((0, 0, Z), (1, 0, Z), (1, 1, Z), (0, 1, Z)))
        quad(bm, ((0.2, 0.2, Z), (0.2, 0.8, Z), (0.8, 0.8, Z),
                  (0.8, 0.2, Z)))
    out["antiparallel"] = (1, case(anti))

    def rot(bm):       # rotated (ajar-leaf style) plane pair, coincident
        import math as _m
        c, s = _m.cos(0.6), _m.sin(0.6)

        def R(x, y, z):
            return (x * c - y * s, x * s + y * c, z)
        quad(bm, (R(0, 0, 0), R(1, 0, 0), R(1, 0, 1), R(0, 0, 1)))
        quad(bm, (R(0.3, 0, 0.3), R(0.9, 0, 0.3), R(0.9, 0, 0.9),
                  R(0.3, 0, 0.9)))
    out["rotated_plane"] = (1, case(rot))

    return {k: (want, got, want == got) for k, (want, got) in out.items()}
