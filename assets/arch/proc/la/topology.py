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
    }


def ok(report, allow_tris=0, allow_boundary=True):
    """True iff @p report meets the quality bar: all quads (up to
    @p allow_tris triangles for genuinely unavoidable cases), zero ngons,
    zero mesh errors, zero T-junctions."""
    del allow_boundary  # boundary edges are legal (openings); kept for API clarity.
    return (report["n_ngons"] == 0 and report["n_tris"] <= allow_tris and
            not report["nonmanifold_edges"] and not report["zero_area_faces"] and
            not report["doubled_verts"] and not report["t_junctions"] and
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
            "nonmanifold={nm} zero_area={za} doubles={db} t_junctions={tj}").format(
        nm=len(report["nonmanifold_edges"]), za=len(report["zero_area_faces"]),
        db=len(report["doubled_verts"]), tj=len(report["t_junctions"]), **report)
