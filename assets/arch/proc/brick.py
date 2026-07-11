"""Procedural hewn-stone brick generator (Blender bpy/bmesh + numpy).

For the mesoscale bake pipeline (ticket rpg-ilmc): generate a single high-poly
hewn stone/brick unit whose faces read as tool-dressed stone and whose arrises
read as chipped, then lay many out into a Romanesque wall to bake normal / mask
maps for the material shader (rpg-lbky / rpg-droh).

Naive vertex noise does not give convincing hewn stone (Blender's noise is too
uniform). Instead we work BIG -> SMALL as a graph-signal problem:

  1. Box -> chip the edges/corners (bevel with per-element random offsets) to a
     coarse angular silhouette.
  2. Treat the mesh as a graph: vertices are nodes carrying xyz, edges couple
     them. Apply Laplacian kernels *selectively* -- FEATURE-PRESERVING smoothing
     (smooth flat faces, keep high-curvature arrises crisp) and optional
     sharpening (enhance chip ridges). This is the "coupled network of xyz +
     graph kernels" step.
  3. Refine in stages: subdivide, jitter along the normal (decreasing
     amplitude), re-apply the graph kernels; add fine Blender noise only at the
     finest levels where it reads as micro surface texture.

Output is one watertight, sanely-normalled brick object.
"""

import bpy
import bmesh
import numpy as np


def _box(length, height, depth):
    """A box bmesh centred at the origin: X=length, Y=depth, Z=height."""
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    for v in bm.verts:
        v.co.x *= length
        v.co.y *= depth
        v.co.z *= height
    return bm


def _chip(bm, rng, edge_chip, corner_chip):
    """Coarse hewn silhouette from intentional, seed-driven chipping only (no
    noise): knock a random subset of the box corners off, chamfer all arrises,
    then chip a random subset of arrises deeper. The irregular offsets and
    selection are the source of per-brick variation; the graph kernels sculpt it.
    """
    corners = [v for v in bm.verts if len(v.link_edges) == 3]
    knock = [v for v in corners if rng.random() < 0.7]
    if knock:
        bmesh.ops.bevel(bm, geom=knock, offset=corner_chip, segments=1,
                        affect='VERTICES', profile=0.35, clamp_overlap=True)
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bmesh.ops.bevel(bm, geom=list(bm.edges), offset=edge_chip, segments=1,
                    affect='EDGES', profile=0.5, clamp_overlap=True)
    sharp = [e for e in bm.edges
             if len(e.link_faces) == 2 and e.calc_face_angle() > 0.45]
    deep = [e for e in sharp if rng.random() < 0.3]
    if deep:
        bmesh.ops.bevel(bm, geom=deep, offset=edge_chip * 2.2, segments=1,
                        affect='EDGES', profile=0.3, clamp_overlap=True)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return bm


def _adjacency(bm):
    """Return (idx_i, idx_j, deg): flattened neighbour index arrays + degree,
    for vectorised graph operators over the current vertex order."""
    bm.verts.index_update()
    ne = len(bm.edges)
    pairs = np.fromiter(
        (idx for e in bm.edges for idx in (e.verts[0].index, e.verts[1].index)),
        dtype=np.int64, count=ne * 2).reshape(ne, 2)
    ii = np.concatenate([pairs[:, 0], pairs[:, 1]])
    jj = np.concatenate([pairs[:, 1], pairs[:, 0]])
    deg = np.bincount(ii, minlength=len(bm.verts)).astype(np.float64)
    deg[deg == 0] = 1.0
    return ii, jj, deg


def _laplacian(v, ii, jj, deg):
    """Umbrella Laplacian delta_i = mean(neighbours) - v_i (a smoothing signal;
    its magnitude approximates local curvature). bincount is far faster here
    than np.add.at."""
    n = v.shape[0]
    acc = np.empty_like(v)
    for c in range(3):
        acc[:, c] = np.bincount(ii, weights=v[jj, c], minlength=n)
    return acc / deg[:, None] - v


def _graph_smooth(bm, iters, lam, feature, sharpen=0.0):
    """Feature-preserving Laplacian smoothing over the mesh graph.

    Each iteration moves vertices toward their neighbourhood mean, but weighted
    per-vertex by w = 1/(1+(curv/feature)^2): flat verts (low curvature) smooth,
    high-curvature arrises are preserved. *sharpen* > 0 adds a counter-pass that
    pushes the preserved (arris) verts further out along the curvature signal,
    crisping the chipped ridges.
    """
    ii, jj, deg = _adjacency(bm)
    v = np.array([vert.co for vert in bm.verts], dtype=np.float64)
    for _ in range(iters):
        d = _laplacian(v, ii, jj, deg)
        curv = np.linalg.norm(d, axis=1)
        scale = feature * (np.median(curv) + 1e-6)
        w = 1.0 / (1.0 + (curv / scale) ** 2)          # 1 flat .. 0 arris
        v += lam * w[:, None] * d
        if sharpen > 0.0:
            v -= sharpen * (1.0 - w)[:, None] * d       # enhance arris deviation
    for k, vert in enumerate(bm.verts):
        vert.co = v[k]


def _facet_planes(v, vn, rng, count, tilt):
    """A set of candidate facet planes: random surface points with their
    (randomly tilted) normals. Returns (P, N) point/normal arrays."""
    count = min(count, len(v))
    idx = rng.choice(len(v), size=count, replace=False)
    P = v[idx].copy()
    Nn = vn[idx] + rng.normal(0.0, tilt, (count, 3))
    Nn /= np.linalg.norm(Nn, axis=1, keepdims=True) + 1e-12
    return P, Nn


def _facet_targets(v, P, Nn, offs):
    """Project every vertex onto its NEAREST facet plane; return the projected
    positions. These SEED the facet structure -- we don't move verts here, the
    sculpt kernels pull toward these targets. Signed distance to plane m is
    v.Nn_m - offs_m (offs = P.Nn), so a single matmul avoids the (N,M,3) blowup.
    """
    dist = v @ Nn.T - offs[None, :]                   # (N, M) signed distance
    m = np.argmin(np.abs(dist), axis=1)               # nearest plane per vertex
    dsel = dist[np.arange(v.shape[0]), m]
    return v - dsel[:, None] * Nn[m]


def _facet_sculpt(bm, rng, n_planes=28, tilt=0.35, iters=18,
                  pull=0.5, sharp=0.3, smooth=0.5, mix=(0.5, 0.2, 0.3),
                  temp0=1.0, temp1=0.1, maxdisp=0.03, feature=1.0,
                  kernel_noise=None):
    """Sculpt hewn facets on the coupled vertex network with MULTIPLE graph
    kernels chosen stochastically per vertex (KMD's scheme), under a SIMULATED-
    ANNEALING temperature that cools over the passes:

      - facet-pull : move toward the vertex's projection onto its nearest fitted
        plane (the seeded facet target) -> flat dressed facets.
      - graph-sharpen : v -= k * Laplacian -> crisps facet-boundary ridges.
      - graph-smooth  : v += k * Laplacian -> relaxes within a facet.

    Temperature (temp0 -> temp1, geometric) scales every kernel strength and the
    stochastic jitter each pass: hot early passes make big exploratory moves and
    facets form; cold late passes make tiny refinements so it anneals to a clean,
    stable faceting instead of ringing. Targets are re-projected every pass.
    """
    ii, jj, deg = _adjacency(bm)
    v = np.array([vt.co for vt in bm.verts], dtype=np.float64)
    v0 = v.copy()
    bm.normal_update()
    vn = np.array([vt.normal for vt in bm.verts], dtype=np.float64)
    n = len(v)
    P, Nn = _facet_planes(v, vn, rng, n_planes, tilt)
    offs = (P * Nn).sum(1)
    # Optional: a COHERENT Blender-noise field seeds the per-vertex kernel
    # SELECTION (not the displacement). Contiguous noise regions then pick the
    # same kernel, so sharpen/smooth/facet lay down in coherent patches -> fine
    # tooling that follows the stone rather than per-vertex salt.
    rfield = None
    if kernel_noise:
        from mathutils import noise as _bn, Vector as _V
        ko = _V((float(rng.uniform(0, 50)), float(rng.uniform(0, 50)),
                 float(rng.uniform(0, 50))))
        rfield = np.clip(np.array(
            [0.5 + 0.5 * _bn.noise(_V((p[0], p[1], p[2])) * kernel_noise + ko)
             for p in v]), 0.0, 1.0)
    for it in range(iters):
        temp = temp0 * (temp1 / temp0) ** (it / max(1, iters - 1))
        t = _facet_targets(v, P, Nn, offs)
        d = _laplacian(v, ii, jj, deg)
        curv = np.linalg.norm(d, axis=1)
        fscale = feature * (np.median(curv) + 1e-9)
        wflat = 1.0 / (1.0 + (curv / fscale) ** 2)      # 1 flat .. 0 sharp arris
        r = rfield if rfield is not None else rng.random(n)
        jit = 0.35 * temp
        s = rng.uniform(1.0 - jit, 1.0 + jit, (n, 1)) * temp
        # anneal the kernel MIX: facet/sharp probabilities cool with temp, so
        # cold late passes are dominated by (weak) smoothing -> convergence.
        p0 = mix[0] * temp
        p1 = p0 + mix[1] * temp
        fac = (r < p0)[:, None]
        shp = ((r >= p0) & (r < p1))[:, None]
        smo = (r >= p1)[:, None]
        v += fac * (pull * s) * (t - v)
        v -= shp * (sharp * s) * d
        # Feature-preserving smoothing: gated by wflat, so already-sharp arrises
        # are NOT smoothed away, and further weakened at cold temp (s carries the
        # annealed strength). Stochastic per-vertex choice keeps it uneven.
        v += smo * (smooth * s) * wflat[:, None] * d
        # bound the deformation so it can't run away from the block silhouette
        disp = v - v0
        mag = np.linalg.norm(disp, axis=1)
        over = mag > maxdisp
        if over.any():
            v[over] = v0[over] + disp[over] * (maxdisp / mag[over])[:, None]
    for k, vert in enumerate(bm.verts):
        vert.co = v[k]


def _remesh_voxel(bm, voxel_size, adaptivity=0.0):
    """Voxel-remesh the current bmesh in place: replaces the geometry with a
    ~voxel_size all-quad mesh. Normalises the topology (even vertex spacing, so
    graph kernels give controlled, evenly-spaced detail rather than density-
    dependent high-frequency spikes) and, at a smaller voxel_size each stage,
    increases the resolution -- true big->small refinement. *adaptivity* > 0
    keeps more resolution on curved/sharp regions and less on flat faces.
    """
    tmp = bpy.data.meshes.new("_rm_src")
    bm.to_mesh(tmp)
    obj = bpy.data.objects.new("_rm_src", tmp)
    bpy.context.scene.collection.objects.link(obj)
    mod = obj.modifiers.new("rm", 'REMESH')
    mod.mode = 'VOXEL'
    mod.voxel_size = voxel_size
    mod.adaptivity = adaptivity
    bpy.context.view_layer.update()
    dg = bpy.context.evaluated_depsgraph_get()
    out = bpy.data.meshes.new_from_object(obj.evaluated_get(dg))
    bm.clear()
    bm.from_mesh(out)
    bpy.data.objects.remove(obj, do_unlink=True)
    bpy.data.meshes.remove(tmp)
    bpy.data.meshes.remove(out)


def _decimate(bm, ratio):
    """Collapse-decimate the bmesh in place to *ratio* of its faces, forcing
    angular flat facets. Applied heavily on the coarse stages and ANNEALED down
    (ratio -> 1) on finer stages, so big dressed facets form first and finer
    detail is layered on without being decimated away. No-op at ratio >= ~1."""
    if ratio >= 0.999:
        return
    tmp = bpy.data.meshes.new("_dec_src")
    bm.to_mesh(tmp)
    obj = bpy.data.objects.new("_dec_src", tmp)
    bpy.context.scene.collection.objects.link(obj)
    mod = obj.modifiers.new("dec", 'DECIMATE')
    mod.decimate_type = 'COLLAPSE'
    mod.ratio = ratio
    bpy.context.view_layer.update()
    dg = bpy.context.evaluated_depsgraph_get()
    out = bpy.data.meshes.new_from_object(obj.evaluated_get(dg))
    bm.clear()
    bm.from_mesh(out)
    bpy.data.objects.remove(obj, do_unlink=True)
    bpy.data.meshes.remove(tmp)
    bpy.data.meshes.remove(out)


def _clip_chunks(bm, rng, count, depth, tilt=0.3):
    """Stochastically clip small planar segments off corners/faces (chips &
    cracks), BEFORE a remesh normalises them. Each clip picks a random surface
    vertex, then bisects ONLY the faces within a small radius of it (so it takes
    a LITTLE off, not a whole slab) with a plane facing outward-from-centre, and
    caps the flat cut. `count` (probability) and `depth` are annealed down by the
    caller: bigger/deeper spalls first, small chips later."""
    for _ in range(count):
        nv = len(bm.verts)
        if nv < 40:
            break
        bm.verts.ensure_lookup_table()
        allco = np.array([w.co for w in bm.verts], dtype=np.float64)
        center = allco.mean(axis=0)
        p = allco[int(rng.integers(nv))]
        outward = p - center
        on = np.linalg.norm(outward)
        if on < 1e-9:
            continue
        nrm = outward / on + rng.normal(0.0, tilt, 3)
        nrm /= np.linalg.norm(nrm) + 1e-12
        # plane just inside the surface at p, so the cut-off piece is a small
        # cap near p; the rest of the brick is the big side.
        co = p - nrm * depth
        bmesh.ops.bisect_plane(bm, geom=list(bm.verts) + list(bm.edges)
                               + list(bm.faces), dist=1e-6,
                               plane_co=co.tolist(), plane_no=nrm.tolist())
        # ALWAYS keep the bigger side: delete faces on whichever side is smaller.
        plus, minus = [], []
        for f in bm.faces:
            c = f.calc_center_median()
            s = ((c.x - co[0]) * nrm[0] + (c.y - co[1]) * nrm[1]
                 + (c.z - co[2]) * nrm[2])
            (plus if s > 0.0 else minus).append(f)
        smaller = plus if len(plus) <= len(minus) else minus
        if 0 < len(smaller) < len(bm.faces):
            bmesh.ops.delete(bm, geom=smaller, context='FACES')
            openb = [e for e in bm.edges if len(e.link_faces) == 1]
            if openb:
                bmesh.ops.holes_fill(bm, edges=openb)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)


def build_brick(name="brick", seed=0, length=0.25, height=0.09, depth=0.11,
                edge_chip=0.007, corner_chip=0.02, detail=3, refine=0.25,
                chip=0.5, collection=None):
    """Build one hewn brick object and return it (graph-based, no noise displace).

    Phase A: box -> seed-driven chipping -> multi-resolution loop (voxel-remesh
    to uniform topology, finer each stage; annealed multi-kernel facet sculpt;
    annealed decimation) -> angular dressed-stone facets.
    Phase B: a few cold, bounded refinement passes whose kernel SELECTION is
    seeded by a coherent Blender-noise field, laying fine tooling over the facets.

    refine -- magnitude of the Phase-B fine tooling (0 = none, ~1 = strong); it
              scales the strictly-bounded Phase-B displacement.
    """
    rng = np.random.default_rng(seed)
    bm = _box(length, height, depth)
    _chip(bm, rng, edge_chip, corner_chip)
    md0 = 0.32 * min(height, depth)         # coarse deformation bound
    mn = min(length, height, depth)

    # --- Phase A: annealed faceting + decimation -> angular hewn base ---
    # decimate stays low (heavy faceting) so the block reads as dressed facets.
    # (voxel, n_planes, tilt, iters, maxdisp, temp0, temp1, mix, decimate)
    schedule = [
        (mn / 6.0,  16, 0.28, 16, md0,        1.00, 0.15, (0.48, 0.28, 0.24), 0.25),
        (mn / 13.0, 40, 0.36, 14, md0 * 0.55, 0.70, 0.10, (0.42, 0.30, 0.28), 0.42),
        (mn / 26.0, 88, 0.44, 12, md0 * 0.26, 0.48, 0.06, (0.36, 0.30, 0.34), 0.46),
    ][:max(1, detail)]
    ns = len(schedule)
    for stage, (vox, npl, tilt, iters, mdisp, t0, t1, mix, dec) in enumerate(schedule):
        # Stochastic chip/crack pass BEFORE the remesh, annealed down per stage
        # (more, deeper spalls early; small chips late).
        cw = (ns - stage) / ns
        nclip = int(round(3.0 * chip * cw))
        if nclip > 0:
            _clip_chunks(bm, rng, count=nclip, depth=mn * 0.16 * cw)
        _remesh_voxel(bm, vox)
        _facet_sculpt(bm, rng, n_planes=npl, tilt=tilt, iters=iters,
                      pull=0.45, sharp=0.38, smooth=0.42, mix=mix,
                      temp0=t0, temp1=t1, maxdisp=mdisp, feature=1.0)
        _decimate(bm, dec)

    # --- Phase B: fine refinement on the faceted base (topology kept) ---
    # A few cold passes with STRICTLY limited displacement and small feature
    # scale, where a coherent Blender-noise field seeds the kernel selection, so
    # fine tooling/chip patches lay down coherently over the dressed facets. Its
    # magnitude is the `refine` parameter (scales the displacement bound).
    if refine > 0.0:
        _facet_sculpt(bm, rng, n_planes=130, tilt=0.5, iters=6,
                      pull=0.30, sharp=0.45, smooth=0.35, mix=(0.30, 0.40, 0.30),
                      temp0=0.22, temp1=0.05, maxdisp=md0 * 0.08 * refine,
                      feature=1.0, kernel_noise=70.0)
    _graph_smooth(bm, iters=1, lam=0.10, feature=1.0)   # barely-there settle

    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    mesh = bpy.data.meshes.new(name)
    bm.to_mesh(mesh)
    bm.free()
    obj = bpy.data.objects.new(name, mesh)
    (collection or bpy.context.scene.collection).objects.link(obj)
    return obj
