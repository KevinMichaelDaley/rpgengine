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


def _chip(bm, rng, edge_chip, corner_chip, chamfer_frac=0.6):
    """Coarse hewn silhouette from intentional, seed-driven chipping only (no
    noise): knock a random subset of the box corners off, chamfer a random SUBSET
    of arrises -- each at a slightly different depth and profile -- so some box
    edges stay sharp and none are dressed identically, then chip a random subset
    of arrises deeper. The irregular offsets/selection are the source of per-brick
    variation; the graph kernels sculpt it. ``chamfer_frac`` is the fraction of
    arrises that get the initial chamfer (the rest stay crisp)."""
    corners = [v for v in bm.verts if len(v.link_edges) == 3]
    knock = [v for v in corners if rng.random() < 0.7]
    if knock:
        bmesh.ops.bevel(bm, geom=knock, offset=corner_chip, segments=1,
                        affect='VERTICES', profile=0.35, clamp_overlap=True)
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    # Per-arris chamfer on a random subset, each with its own depth (0.6-1.6x)
    # and profile/angle (0.25-0.75); the unchamfered edges stay sharp.
    for e in [e for e in bm.edges if len(e.link_faces) == 2]:
        if rng.random() >= chamfer_frac or not e.is_valid:
            continue
        bmesh.ops.bevel(bm, geom=[e], segments=1, affect='EDGES',
                        offset=edge_chip * float(rng.uniform(0.6, 1.6)),
                        profile=float(rng.uniform(0.25, 0.75)),
                        clamp_overlap=True)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
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


def _tri_indices(bm):
    """Fan-triangulated (T,3) vertex-index array so vertex normals can be
    recomputed from the live positions in numpy (faces may be quads or tris)."""
    tris = []
    for f in bm.faces:
        vs = [vt.index for vt in f.verts]
        for k in range(1, len(vs) - 1):
            tris.append((vs[0], vs[k], vs[k + 1]))
    return np.asarray(tris, dtype=np.intp)


def _vertex_normals(v, tri, n):
    """Area-weighted vertex normals from the CURRENT positions (bincount scatter).
    Called every sculpt iteration so the normal-driven kernels track the geometry
    as they move it, instead of steering by the pre-move (or post-decimate) normals."""
    e1 = v[tri[:, 1]] - v[tri[:, 0]]
    e2 = v[tri[:, 2]] - v[tri[:, 0]]
    fn = np.cross(e1, e2)                       # magnitude ~ 2*triangle area
    idx = tri.ravel()
    vn = np.empty((n, 3))
    for c in range(3):
        vn[:, c] = np.bincount(idx, weights=np.repeat(fn[:, c], 3), minlength=n)
    return vn / (np.linalg.norm(vn, axis=1, keepdims=True) + 1e-12)


def _smooth_normals(vn, ii, jj, deg, iters):
    """Relax the NORMAL field over the 1-ring graph (neighbour-average + renormalise),
    WITHOUT moving any vertex. Evens out the blocky voxel/decimate normals the remesh
    leaves so the normal-driven kernels dress against a smooth normal field; the true
    arrises survive because their two face clusters keep pulling the normal apart."""
    for _ in range(max(0, iters)):
        vn = vn + _laplacian(vn, ii, jj, deg)           # = mean of neighbour normals
        vn /= np.linalg.norm(vn, axis=1, keepdims=True) + 1e-12
    return vn


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


def _corner_tags(vn, ii, jj, deg, crease_ref=0.12):
    """Tag creases/corners from 1-ring vertex-normal dispersion: for each vertex,
    1 - (mean dot of its normal with its neighbours') is ~0 on a flat patch and
    grows where facets meet at an angle. Returned in [0,1] (1 = strong arris/
    corner), so callers can protect these verts from smoothing."""
    dots = (vn[ii] * vn[jj]).sum(1)                     # per directed edge
    acc = np.bincount(ii, weights=dots, minlength=len(vn))
    disp = 1.0 - acc / np.maximum(deg, 1)               # 0 flat .. ~2 sharp
    return np.clip(disp / crease_ref, 0.0, 1.0)


def _box_edge_tags(v, band=0.72):
    """Tag verts on the brick's principal box arrises/corners: those near at
    least two of the bounding-box faces. Returns (tag, near):
      tag  -- [0,1], high only where >=2 axes sit close to an extreme (edge/
              corner), so callers keep those defining arrises crisp;
      near -- (n,3) per-axis proximity to a face (0 centre .. 1 face), used to
              find which axis an edge RUNS along (the least-near axis) so
              smoothing can be allowed along the edge and blocked only across it.
    """
    lo = v.min(0)
    hi = v.max(0)
    c = 0.5 * (lo + hi)
    half = 0.5 * (hi - lo) + 1e-9
    rel = np.abs(v - c) / half                          # 0 centre .. 1 face
    near = np.clip((rel - band) / (1.0 - band), 0.0, 1.0)
    s = np.sort(near, axis=1)                            # ascending per row
    return s[:, 1] * s[:, 2], near                       # two largest -> edge


def _facet_sculpt(bm, rng, n_planes=28, tilt=0.35, iters=18,
                  pull=0.5, sharp=0.3, smooth=0.5, pinch=0.4, flatten=0.4,
                  flat_normal=0.5, edge_sharp=0.38, smooth_flat_pow=1.0,
                  smooth_feature=1.0, norm_smooth_iters=2,
                  mix=(0.5, 0.2, 0.12, 0.12), temp0=1.0,
                  temp1=0.1, maxdisp=0.03, feature=1.0, corner_preserve=0.75,
                  box_preserve=0.7, box_run_reduce=0.5, nfl_axis_swap=0.28,
                  axis_rand=0.15, kernel_noise=None):
    """Sculpt hewn facets on the coupled vertex network with MULTIPLE graph
    kernels chosen stochastically per vertex (KMD's scheme), under a SIMULATED-
    ANNEALING temperature that cools over the passes:

      - facet-pull   : move toward the vertex's projection onto its nearest
        fitted plane (the seeded facet target) -> flat dressed facets.
      - graph-sharpen: v -= k * Laplacian -> crisps facet-boundary ridges.
      - dir-pinch    : v -= k * (Laplacian . b) b along a per-pass random axis b
        -> anisotropic chisel creases running across b.
      - axis-flatten : smooth ONLY one axis component -> flattens faces whose
        normal is along that axis (dressed, near-axis-aligned faces).
      - graph-smooth : v += k * Laplacian -> relaxes within a facet.

    ``mix`` = (facet, sharp, pinch, flatten) selection probabilities; smoothing
    takes the remainder. Corner/arris verts (tagged by normal dispersion) are
    smoothed far LESS OFTEN -- ``corner_preserve`` is the fraction of smoothing
    events they skip -- so chipped edges stay crisp instead of rounding off.

    Temperature (temp0 -> temp1, geometric) scales every kernel strength and the
    jitter each pass: hot early passes form facets, cold late passes refine, so
    it anneals to a clean faceting. Targets are re-projected every pass.
    """
    ii, jj, deg = _adjacency(bm)
    v = np.array([vt.co for vt in bm.verts], dtype=np.float64)
    v0 = v.copy()
    bm.normal_update()
    vn = np.array([vt.normal for vt in bm.verts], dtype=np.float64)
    tri = _tri_indices(bm)                      # for per-iteration normal recompute
    n = len(v)
    vn = _smooth_normals(vn, ii, jj, deg, norm_smooth_iters)   # relax remesh normals
    P, Nn = _facet_planes(v, vn, rng, n_planes, tilt)
    offs = (P * Nn).sum(1)
    corner = _corner_tags(vn, ii, jj, deg)              # persistent crease TAG
    box_edge, box_near = _box_edge_tags(v0)             # brick's principal arrises
    # The 8 box CORNERS are where all three axes sit near a face, so the SMALLEST
    # per-axis nearness is high only there (on an arris the run axis is far -> low).
    box_corner = np.sort(box_near, axis=1)[:, 0]
    # Anisotropic smoothing weight for the box arrises: an edge RUNS along its
    # least-near axis; smoothing ALONG that axis straightens the edge (fine),
    # smoothing across it (the two most-near axes) rounds it (bad). So keep the
    # run-axis component at full weight and suppress the two transverse ones by
    # box_edge*box_preserve. Flat faces (box_edge~0) keep isotropic smoothing.
    box_order = np.argsort(box_near, axis=1)            # [:,0] = run axis
    smooth_aniso = np.ones((n, 3))
    rows = np.arange(n)
    # Across the edge: strong downweight (keeps the arris from rounding). Along
    # the edge: a partial downweight too (box_run_reduce) -- fully free along-edge
    # smoothing still rounds SHORT edges (the vertical/end arrises), so limit it.
    supp_trans = 1.0 - box_edge * box_preserve
    supp_run = 1.0 - box_edge * box_preserve * box_run_reduce
    smooth_aniso[rows, box_order[:, 0]] = supp_run
    smooth_aniso[rows, box_order[:, 1]] = supp_trans
    smooth_aniso[rows, box_order[:, 2]] = supp_trans
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
        # Refresh vertex normals from the moved geometry so the normal-driven
        # kernels (axis-flatten, normal-flatten) act on current normals, not the
        # stale ones left over from the previous pass / the last decimate -- then
        # relax the normal field (normals only, no vertex motion) so the remesh
        # faceting doesn't steer the dressing.
        if it > 0:
            vn = _smooth_normals(_vertex_normals(v, tri, n), ii, jj, deg,
                                 norm_smooth_iters)
        fscale = feature * (np.median(curv) + 1e-9)
        wflat = 1.0 / (1.0 + (curv / fscale) ** 2)      # 1 flat .. 0 sharp arris
        r = rfield if rfield is not None else rng.random(n)
        jit = 0.35 * temp
        s = rng.uniform(1.0 - jit, 1.0 + jit, (n, 1)) * temp
        # anneal the kernel MIX: kernel probabilities cool with temp, so cold
        # late passes are dominated by (weak) smoothing -> convergence.
        p0 = mix[0] * temp
        p1 = p0 + mix[1] * temp
        p2 = p1 + mix[2] * temp
        p3 = p2 + mix[3] * temp
        # Edge-sharpen band (mix[4], optional -> 0 if the mix has only 4 entries):
        # an anti-Laplacian push weighted to EXISTING creases, crisping arrises.
        mix4 = mix[4] if len(mix) > 4 else 0.0
        pes = p3 + mix4 * temp
        # Split the smoothing remainder: most is a flatten-ALONG-THE-NORMAL kernel
        # (removes only the Laplacian's normal component -> dresses the face flat
        # WITHOUT the tangential pull that rounds edges), the rest isotropic
        # smoothing. This trades away most of the rounding-prone smoothing.
        p4 = pes + (1.0 - pes) * 0.58
        fac = (r < p0)[:, None]
        shp = ((r >= p0) & (r < p1))[:, None]
        pin_sel = (r >= p1) & (r < p2)
        flt = (r >= p2) & (r < p3)
        es_sel = (r >= p3) & (r < pes)
        nfl_sel = (r >= pes) & (r < p4)
        smo_sel = (r >= p4)
        # In the first third of the passes, redirect a fraction of the normal-
        # flatten verts to AXIS-flatten instead: early passes then bias toward
        # axis-aligned dressed faces (box character), later passes keep normal-
        # flatten (which follows arbitrary facet orientations).
        if it < max(1, iters // 3) and nfl_axis_swap > 0.0:
            swap = nfl_sel & (rng.random(n) < nfl_axis_swap)
            flt = flt | swap
            nfl_sel = nfl_sel & ~swap
        # Pinch never touches the box arrises (it would distort the corners).
        pin = (pin_sel & (rng.random(n) > box_edge * box_preserve))[:, None]
        # Both smoothing kernels are cut on detected creases; box arrises are cut
        # for normal-flatten (would round them) and handled anisotropically for
        # isotropic smoothing (below). Flat faces are unaffected.
        edge_keep = np.maximum(corner * corner_preserve, box_edge * box_preserve)
        nfl = (nfl_sel & (rng.random(n) > edge_keep))[:, None]
        smo = (smo_sel & (rng.random(n) > corner * corner_preserve))[:, None]
        v += fac * (pull * s) * (t - v)
        v -= shp * (sharp * s) * d
        # Directional pinch: remove the Laplacian component along a random axis b
        # -> exaggerates cross-b deviations into anisotropic chisel creases.
        b = rng.normal(0.0, 1.0, 3)
        b /= np.linalg.norm(b) + 1e-12
        dproj = d @ b
        v -= pin * (pinch * s) * (dproj[:, None] * b[None, :])
        # Axis-flatten: flatten each selected vertex's DOMINANT-axis component
        # (the axis closest to its normal) toward its neighbours -> dresses each
        # face flat against its own axis plane. Occasionally, for INTERIOR verts
        # (off the box arrises), use a random axis instead -> a little internal
        # faceting variety rather than a single global flatten direction.
        if flt.any():
            ax = np.argmax(np.abs(vn), axis=1)
            rmask = (rng.random(n) < axis_rand) & (box_edge < 0.3)
            ax = np.where(rmask, rng.integers(0, 3, n), ax)
            rows = np.nonzero(flt)[0]
            axr = ax[rows]
            v[rows, axr] += (flatten * s[rows, 0]) * d[rows, axr]
        # Normal-flatten: move along the surface normal by the Laplacian's normal
        # component -> presses the face flat with NO tangential motion, so it
        # dresses faces without rounding/shrinking edges the way smoothing does.
        dn = (d * vn).sum(1, keepdims=True)
        v += nfl * (flat_normal * s) * wflat[:, None] * dn * vn
        # Edge-sharpen: anti-Laplacian push (v -= k*d moves AWAY from the neighbour
        # mean) CONCENTRATED on existing creases via the edge weight (1 - wflat),
        # so it re-crisps the arrises the voxel remesh rounded while leaving the
        # flat faces untouched (unlike the general `sharp`, which acts everywhere).
        # Keep edge-sharpen OFF the 8 box corners: sharpening there just spikes the
        # corner outward along the diagonal and, once bbox-clamped, pinches it into
        # a welt. Full strength stays on the arris runs (box_corner ~ 0 there).
        es = (es_sel & (rng.random(n) > box_corner * corner_preserve))[:, None]
        v -= es * (edge_sharp * s) * (1.0 - wflat)[:, None] * d
        # Feature-preserving smoothing: gated by its OWN flatness weight. A larger
        # smooth_feature widens the flatness scale (raises the angular threshold),
        # so mildly-angular decimation facets read as flat and get smoothed while
        # the true near-90 arrises stay far above it; smooth_flat_pow then restricts
        # it to only the nearly-flat verts. Weakened at cold temp (s), and
        # DOWNWEIGHTED anisotropically on box arrises (smooth_aniso) -- full along
        # the edge, reduced across it, so it never smooths transverse to a corner.
        wflat_s = 1.0 / (1.0 + (curv / (fscale * smooth_feature)) ** 2)
        if smooth_flat_pow != 1.0:
            wflat_s = wflat_s ** smooth_flat_pow
        v += smo * (smooth * s) * wflat_s[:, None] * (d * smooth_aniso)
        # bound the deformation so it can't run away from the block silhouette
        disp = v - v0
        mag = np.linalg.norm(disp, axis=1)
        over = mag > maxdisp
        if over.any():
            v[over] = v0[over] + disp[over] * (maxdisp / mag[over])[:, None]
    for k, vert in enumerate(bm.verts):
        vert.co = v[k]


def _remesh_voxel(bm, voxel_size, adaptivity=0.0, decimate=1.0):
    """Voxel-remesh the current bmesh in place: replaces the geometry with a
    ~voxel_size all-quad mesh. Normalises the topology (even vertex spacing, so
    graph kernels give controlled, evenly-spaced detail rather than density-
    dependent high-frequency spikes) and, at a smaller voxel_size each stage,
    increases the resolution -- true big->small refinement. *adaptivity* > 0
    keeps more resolution on curved/sharp regions.

    *decimate* < 1 also collapse-decimates to that face ratio (in the SAME
    modifier evaluation), so every remesh both normalises the topology and
    faceting-reduces it -- forcing angular dressed facets at every scale.
    """
    tmp = bpy.data.meshes.new("_rm_src")
    bm.to_mesh(tmp)
    obj = bpy.data.objects.new("_rm_src", tmp)
    bpy.context.scene.collection.objects.link(obj)
    mod = obj.modifiers.new("rm", 'REMESH')
    mod.mode = 'VOXEL'
    mod.voxel_size = voxel_size
    mod.adaptivity = adaptivity
    if decimate < 0.999:
        dmod = obj.modifiers.new("dec", 'DECIMATE')
        dmod.decimate_type = 'COLLAPSE'
        dmod.ratio = decimate
    bpy.context.view_layer.update()
    dg = bpy.context.evaluated_depsgraph_get()
    out = bpy.data.meshes.new_from_object(obj.evaluated_get(dg))
    bm.clear()
    bm.from_mesh(out)
    bpy.data.objects.remove(obj, do_unlink=True)
    bpy.data.meshes.remove(tmp)
    bpy.data.meshes.remove(out)


def _clip_chunks(bm, rng, count, depth, tilt=0.035):
    """Chisel the rough ENDS of the brick. A near LENGTH-axis plane, only
    slightly rotated (small tolerance in the two perpendicular directions), trims
    one end inward -- so the brick keeps its full HEIGHT and WIDTH (only its
    length varies, like a coursing brick's rough-cut ends) and the cut is only
    slightly diagonal, intersecting the boundary. A near-axis plane always shears
    a whole slab, so cutting a Y/Z face would shrink width/height -- hence
    length-axis ends only. Always keeps the body side. `count` (probability) and
    `depth` anneal down per stage."""
    for _ in range(count):
        nv = len(bm.verts)
        if nv < 40:
            break
        bm.verts.ensure_lookup_table()
        allco = np.array([w.co for w in bm.verts], dtype=np.float64)
        s = 1.0 if rng.random() < 0.5 else -1.0          # +X or -X end
        p = allco[int(np.argmax(s * allco[:, 0]))]       # the extreme end vertex
        nrm = np.array([s, 0.0, 0.0])                    # length-axis normal ...
        # ... rotated only a hair off axis: `tilt` is the max off-axis tangent
        # (tan(2 deg) ~= 0.035), so a chisel shears at most ~2 degrees.
        nrm[1] = float(rng.uniform(-tilt, tilt))
        nrm[2] = float(rng.uniform(-tilt, tilt))
        nrm /= np.linalg.norm(nrm) + 1e-12
        plane_co = p - nrm * depth                       # trim the end inward
        bmesh.ops.bisect_plane(bm, geom=list(bm.verts) + list(bm.edges)
                               + list(bm.faces), dist=1e-6,
                               plane_co=plane_co.tolist(), plane_no=nrm.tolist())
        # Keep the side containing the brick BODY (its centroid); delete only the
        # thin cap on the far side. Face COUNT is an unreliable "bigger" test on
        # uneven tessellation (it can keep the small cap -> triangular brick);
        # the centroid side is robust.
        cc = np.array([w.co for w in bm.verts], dtype=np.float64).mean(axis=0)
        cs = (cc - plane_co) @ nrm
        drop = []
        for f in bm.faces:
            c = f.calc_center_median()
            s = ((c.x - plane_co[0]) * nrm[0] + (c.y - plane_co[1]) * nrm[1]
                 + (c.z - plane_co[2]) * nrm[2])
            if (s > 0.0) != (cs > 0.0):                  # opposite side from body
                drop.append(f)
        if 0 < len(drop) < len(bm.faces):
            bmesh.ops.delete(bm, geom=drop, context='FACES')
            openb = [e for e in bm.edges if len(e.link_faces) == 1]
            if openb:
                bmesh.ops.holes_fill(bm, edges=openb)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)


def _crack(bm, rng, seeds=3, steps=16, step_len=0.004, branch=0.10, depth=0.02,
           width=0.03, width_var=0.75, width_freq=45.0, vor_scale=40.0,
           vor_bias=0.35, wander=0.4, corner_bias=2.5, profile=1.8, maxdisp=None):
    """Grow cracks as CONTINUOUS tangent-space walks (decoupled from the mesh
    grid, so they wander at any angle instead of snapping to horizontal/vertical
    edges), seeded preferentially at CORNERS (curvature-weighted -- corners crack
    most), then indent them as variable-width organic divots.

    Each walk steps a fixed length along its heading in the local tangent plane;
    the heading turns randomly each step (wander) and is nudged toward Voronoi
    cell edges (natural fracture lines), with forward momentum so it wanders but
    does not loop. The path points seed KD-tree divots whose radius/depth vary by
    noise (organic width) with a `profile`-sharpened falloff. Caller may remesh
    (pass 1, weathered) or leave it crisp (pass 2)."""
    from mathutils import noise as bn, Vector
    from mathutils.kdtree import KDTree
    bm.verts.ensure_lookup_table()
    bm.normal_update()
    N = len(bm.verts)
    if N < 40:
        return
    co = np.array([v.co for v in bm.verts], dtype=np.float64)
    nrm = np.array([v.normal for v in bm.verts], dtype=np.float64)
    # curvature (|umbrella Laplacian|) -> corner/edge weight for seeding
    acc = np.zeros((N, 3))
    deg = np.zeros(N)
    for e in bm.edges:
        a, b = e.verts[0].index, e.verts[1].index
        acc[a] += co[b]; deg[a] += 1.0
        acc[b] += co[a]; deg[b] += 1.0
    deg[deg == 0] = 1.0
    curv = np.linalg.norm(acc / deg[:, None] - co, axis=1)
    wgt = curv ** corner_bias
    probs = wgt / wgt.sum() if wgt.sum() > 1e-12 else np.full(N, 1.0 / N)
    kd = KDTree(N)
    for i in range(N):
        kd.insert(Vector((co[i, 0], co[i, 1], co[i, 2])), i)
    kd.balance()

    def project(vec, nn):
        w = vec - nn * (vec @ nn)
        L = np.linalg.norm(w)
        return w / L if L > 1e-9 else vec

    def rot_axis(k, a, ang):
        c, s = np.cos(ang), np.sin(ang)
        return k * c + np.cross(a, k) * s + a * (a @ k) * (1.0 - c)

    def vf(px, py, pz):                                  # Voronoi F2-F1 (0 at cell edge)
        dd = bn.voronoi(Vector((px, py, pz)) * vor_scale)[0]
        return dd[1] - dd[0]

    seeds_i = rng.choice(N, size=min(seeds, N), replace=False, p=probs)
    walks = []
    for si in seeds_i:
        g = rng.normal(0.0, 1.0, 3)
        walks.append([co[si].copy(), project(g, nrm[si]), int(steps)])
    path = []
    guard = 0
    while walks and guard < seeds * steps * 4:
        guard += 1
        nxt = []
        for pos, dr, rem in walks:
            if rem <= 0:
                continue
            path.append((pos[0], pos[1], pos[2]))
            _, ji, _ = kd.find(Vector((pos[0], pos[1], pos[2])))
            nj = nrm[ji]
            dr = project(rot_axis(dr, nj, rng.uniform(-wander, wander)), nj)
            if vor_bias > 0.0:                           # steer toward Voronoi cell edges
                t1 = project(np.array([1.0, 0.0, 0.0]) if abs(nj[0]) < 0.9
                             else np.array([0.0, 1.0, 0.0]), nj)
                t2 = np.cross(nj, t1)
                eps = step_len * 0.9
                grad = ((vf(*(pos + eps * t1)) - vf(*(pos - eps * t1))) * t1
                        + (vf(*(pos + eps * t2)) - vf(*(pos - eps * t2))) * t2)
                gn = np.linalg.norm(grad)
                if gn > 1e-9:
                    dr = project((1.0 - vor_bias) * dr - vor_bias * grad / gn, nj)
            npos = pos + dr * step_len
            _, jk, _ = kd.find(Vector((npos[0], npos[1], npos[2])))
            npos = npos - nrm[jk] * ((npos - co[jk]) @ nrm[jk])   # stay on surface
            nxt.append([npos, project(dr, nrm[jk]), rem - 1])
            if rng.random() < branch and rem > 4:
                bd = project(rot_axis(dr, nj, (1.0 if rng.random() < 0.5 else -1.0)
                                      * rng.uniform(0.7, 1.2)), nj)
                nxt.append([pos.copy(), bd, rem // 2])
        walks = nxt
    # variable-width organic divots along the continuous path
    off = Vector((5.7, 2.3, 9.1))
    out = np.zeros(N)
    for px, py, pz in path:
        pv = Vector((px, py, pz))
        wr = width * (1.0 - width_var + width_var * (0.5 + 0.5 * bn.noise(pv * width_freq)))
        dp = depth * (0.4 + 0.6 * (0.5 + 0.5 * bn.noise(pv * width_freq + off)))
        if wr < 1e-6:
            continue
        for _vco, j, dist in kd.find_range(pv, wr):
            f = (1.0 - dist / wr) ** profile
            if dp * f > out[j]:
                out[j] = dp * f
    if maxdisp is not None:
        out = np.minimum(out, maxdisp)
    moved = co - nrm * out[:, None]
    for k, v in enumerate(bm.verts):
        v.co = moved[k]
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)


def _micro_node_group(group_name, mn, micro, seed_offset, env_strength=0.85,
                      quant_step=0.008, quant_mix=0.3, floor_frac=0.025):
    """Build the micro-detail Geometry Nodes group: fBm grain plus a cellular
    pitting field made of several SMOOTHED, offset, distorted Voronoi layers
    combined with Minimum and clamped to bounds so only the small cell cores
    dip inward. Displaces points along their normal. Returns the node group.

    ``seed_offset`` is a 3-vector translating the sampling space so every brick
    gets a different field. All feature sizes are fractions of ``mn`` (the
    brick's smallest dimension); ``micro`` scales overall amplitude.
    ``env_strength`` in [0,1] sets how deeply the low-frequency envelope
    suppresses relief in its smooth patches: 0 = uniform everywhere, 1 = smooth
    patches go fully flat (maximum contrast between smooth and rough)."""
    ng = bpy.data.node_groups.new(group_name, 'GeometryNodeTree')
    ng.interface.new_socket("Geometry", in_out='INPUT', socket_type='NodeSocketGeometry')
    ng.interface.new_socket("Geometry", in_out='OUTPUT', socket_type='NodeSocketGeometry')
    nodes, links = ng.nodes, ng.links

    def nd(t, x, y):
        n = nodes.new(t); n.location = (x, y); return n

    gin = nd('NodeGroupInput', -1000, 0)
    gout = nd('NodeGroupOutput', 1000, 0)

    # Sampling coordinate = local Position translated by the per-brick seed.
    pos = nd('GeometryNodeInputPosition', -900, -300)
    seed_v = nd('FunctionNodeInputVector', -900, -440)
    seed_v.vector = seed_offset
    base = nd('ShaderNodeVectorMath', -740, -340); base.operation = 'ADD'
    links.new(pos.outputs['Position'], base.inputs[0])
    links.new(seed_v.outputs['Vector'], base.inputs[1])

    # Shared cellular distortion: a low-scale vector noise nudges the sample
    # position so cell walls meander instead of sitting on a clean grid.
    dnoise = nd('ShaderNodeTexNoise', -740, -560)
    dnoise.noise_dimensions = '3D'
    dnoise.inputs['Scale'].default_value = 1.0 / (mn * 0.5)
    dnoise.inputs['Detail'].default_value = 2.0
    links.new(base.outputs['Vector'], dnoise.inputs['Vector'])
    dsub = nd('ShaderNodeVectorMath', -560, -600); dsub.operation = 'SUBTRACT'
    dsub.inputs[1].default_value = (0.5, 0.5, 0.5)
    links.new(dnoise.outputs['Color'], dsub.inputs[0])
    dscl = nd('ShaderNodeVectorMath', -400, -600); dscl.operation = 'SCALE'
    dscl.inputs['Scale'].default_value = mn * 0.35
    links.new(dsub.outputs['Vector'], dscl.inputs[0])
    dpos = nd('ShaderNodeVectorMath', -240, -400); dpos.operation = 'ADD'
    links.new(base.outputs['Vector'], dpos.inputs[0])
    links.new(dscl.outputs['Vector'], dpos.inputs[1])

    # Several small-cell SMOOTH_F1 Voronoi layers, each at its own scale and
    # constant offset, combined with Minimum -> union of pits. High smoothness
    # keeps the cell cores rounded rather than sharply pointed.
    layers = [(1.0 / (mn * 0.085), (0.0, 0.0, 0.0), 1.0),   # smaller cells = higher freq
              (1.0 / (mn * 0.060), (13.1, 4.7, 9.3), 1.0),
              (1.0 / (mn * 0.045), (2.9, 21.4, 15.8), 0.9)]
    prev = None
    for k, (scale, off, smooth) in enumerate(layers):
        ofs = nd('ShaderNodeVectorMath', -80, -200 - k * 220)
        ofs.operation = 'ADD'; ofs.inputs[1].default_value = off
        links.new(dpos.outputs['Vector'], ofs.inputs[0])
        vor = nd('ShaderNodeTexVoronoi', 100, -200 - k * 220)
        vor.voronoi_dimensions = '3D'; vor.feature = 'SMOOTH_F1'
        vor.inputs['Scale'].default_value = scale
        vor.inputs['Smoothness'].default_value = smooth
        vor.inputs['Randomness'].default_value = 1.0
        links.new(ofs.outputs['Vector'], vor.inputs['Vector'])
        if prev is None:
            prev = vor.outputs['Distance']
        else:
            mn_node = nd('ShaderNodeMath', 300, -200 - k * 220)
            mn_node.operation = 'MINIMUM'
            links.new(prev, mn_node.inputs[0])
            links.new(vor.outputs['Distance'], mn_node.inputs[1])
            prev = mn_node.outputs['Value']

    # Flatten with bounds: cell cores (small distance) map to full depth, and
    # anything past the band clamps flat. Negative -> pits bite inward. Gentle
    # slope (wide band, shallow depth) so pits are soft dishes, not spikes.
    pit_depth = mn * 0.011 * micro          # half depth: shallow pitting
    pit = nd('ShaderNodeMapRange', 500, -300)
    pit.clamp = True
    pit.inputs['From Min'].default_value = 0.0
    pit.inputs['From Max'].default_value = 0.55
    pit.inputs['To Min'].default_value = -pit_depth
    pit.inputs['To Max'].default_value = 0.0
    links.new(prev, pit.inputs['Value'])

    # Per-facet variation: sample a noise field by the surface NORMAL (shifted by
    # the per-brick seed) so the pitting amount varies from facet to facet -- some
    # facets are heavily pitted, others barely -- instead of being uniform. Then
    # clamp the modulated pit so it can never exceed the intended depth.
    fnrm = nd('GeometryNodeInputNormal', 300, -560)
    fscl = nd('ShaderNodeVectorMath', 460, -540)
    fscl.operation = 'SCALE'
    fscl.inputs['Scale'].default_value = 3.5
    links.new(fnrm.outputs['Normal'], fscl.inputs[0])
    foff = nd('ShaderNodeVectorMath', 560, -540)
    foff.operation = 'ADD'
    foff.inputs[1].default_value = seed_offset
    links.new(fscl.outputs['Vector'], foff.inputs[0])
    fnoise = nd('ShaderNodeTexNoise', 660, -540)
    fnoise.noise_dimensions = '3D'
    fnoise.inputs['Scale'].default_value = 1.0
    fnoise.inputs['Detail'].default_value = 1.0
    links.new(foff.outputs['Vector'], fnoise.inputs['Vector'])
    fvar = nd('ShaderNodeMapRange', 820, -540)
    fvar.clamp = True
    fvar.inputs['From Min'].default_value = 0.35  # bias: many facets get no pits
    fvar.inputs['From Max'].default_value = 0.85
    fvar.inputs['To Min'].default_value = 0.0     # fully unpitted facets -> sparse
    fvar.inputs['To Max'].default_value = 1.0
    links.new(fnoise.outputs['Fac'], fvar.inputs['Value'])
    pit_mod = nd('ShaderNodeMath', 660, -300)
    pit_mod.operation = 'MULTIPLY'
    links.new(pit.outputs['Result'], pit_mod.inputs[0])
    links.new(fvar.outputs['Result'], pit_mod.inputs[1])
    pit_clamp = nd('ShaderNodeClamp', 820, -300)
    pit_clamp.inputs['Min'].default_value = -pit_depth
    pit_clamp.inputs['Max'].default_value = 0.0
    links.new(pit_mod.outputs['Value'], pit_clamp.inputs['Value'])

    # Fine fBm grain (higher frequency, more octaves, shallow) riding on the pits:
    # tool-tooth tooling, not a coarse swell.
    grain = nd('ShaderNodeTexNoise', 100, 300)
    grain.noise_dimensions = '3D'
    grain.inputs['Scale'].default_value = 1.0 / (mn * 0.13)   # ~4.5x higher freq
    grain.inputs['Detail'].default_value = 5.0
    grain.inputs['Roughness'].default_value = 0.55
    links.new(base.outputs['Vector'], grain.inputs['Vector'])
    gmap = nd('ShaderNodeMapRange', 300, 300)
    gmap.inputs['To Min'].default_value = -mn * 0.006 * micro  # ~0.4x amplitude
    gmap.inputs['To Max'].default_value = mn * 0.006 * micro
    links.new(grain.outputs['Fac'], gmap.inputs['Value'])

    # Sum grain + per-facet-modulated pits -> raw scalar displacement.
    disp = nd('ShaderNodeMath', 980, 0); disp.operation = 'ADD'
    links.new(gmap.outputs['Result'], disp.inputs[0])
    links.new(pit_clamp.outputs['Result'], disp.inputs[1])

    # Broad low-frequency envelope: modulates amplitude across the brick so some
    # patches stay near-smooth while others are rougher -> non-uniform relief.
    # A narrow input band makes the swing high-contrast (near-bimodal) rather
    # than a gentle gradient; env_strength sets how flat the smooth patches go.
    env = nd('ShaderNodeTexNoise', 300, -760)
    env.noise_dimensions = '3D'
    env.inputs['Scale'].default_value = 1.0 / (mn * 3.4)   # bigger smooth patches
    env.inputs['Detail'].default_value = 1.0
    links.new(base.outputs['Vector'], env.inputs['Vector'])
    emap = nd('ShaderNodeMapRange', 500, -760)
    emap.clamp = True
    emap.inputs['From Min'].default_value = 0.38
    emap.inputs['From Max'].default_value = 0.62
    emap.inputs['To Min'].default_value = 1.0 - max(0.0, min(1.0, env_strength))
    emap.inputs['To Max'].default_value = 1.0
    links.new(env.outputs['Fac'], emap.inputs['Value'])
    dmod = nd('ShaderNodeMath', 700, -400); dmod.operation = 'MULTIPLY'
    links.new(disp.outputs['Value'], dmod.inputs[0])
    links.new(emap.outputs['Result'], dmod.inputs[1])

    # Quantize the displacement into discrete steps so the relief terraces into
    # chiselled facets instead of a smooth swell. Blend a fraction (quant_mix) of
    # the snapped signal back with the continuous one so it reads chiselled but
    # keeps a little softness: out = cont + quant_mix * (snap(cont) - cont).
    disp_out = dmod.outputs['Value']
    if quant_step > 0.0 and quant_mix > 0.0:
        snap = nd('ShaderNodeMath', 780, -520); snap.operation = 'SNAP'
        snap.inputs[1].default_value = mn * quant_step
        links.new(dmod.outputs['Value'], snap.inputs[0])
        diff = nd('ShaderNodeMath', 900, -520); diff.operation = 'SUBTRACT'
        links.new(snap.outputs['Value'], diff.inputs[0])
        links.new(dmod.outputs['Value'], diff.inputs[1])
        qadd = nd('ShaderNodeMath', 1020, -460); qadd.operation = 'MULTIPLY_ADD'
        links.new(diff.outputs['Value'], qadd.inputs[0])   # (snap - cont)
        qadd.inputs[1].default_value = quant_mix           # * quant_mix
        links.new(dmod.outputs['Value'], qadd.inputs[2])   # + cont
        disp_out = qadd.outputs['Value']

    # Minimum "water level": clamp the carve depth so grain stacking on pits can
    # never gouge deeper than -floor. (Bumps outward are left unbounded.)
    if floor_frac > 0.0:
        floor = nd('ShaderNodeMath', 1120, -520); floor.operation = 'MAXIMUM'
        links.new(disp_out, floor.inputs[0])
        floor.inputs[1].default_value = -mn * floor_frac
        disp_out = floor.outputs['Value']

    # Offset = normal * (quantized, floored) modulated displacement.
    nrm = nd('GeometryNodeInputNormal', 1120, 200)
    off_v = nd('ShaderNodeVectorMath', 1320, 200); off_v.operation = 'SCALE'
    links.new(nrm.outputs['Normal'], off_v.inputs[0])
    links.new(disp_out, off_v.inputs['Scale'])

    setp = nd('GeometryNodeSetPosition', 950, 0)
    links.new(gin.outputs['Geometry'], setp.inputs['Geometry'])
    links.new(off_v.outputs['Vector'], setp.inputs['Offset'])
    # Shade smooth so the resolved micro relief reads as form, not voxel facets.
    smooth = nd('GeometryNodeSetShadeSmooth', 1120, 0)
    links.new(setp.outputs['Geometry'], smooth.inputs['Geometry'])
    links.new(smooth.outputs['Geometry'], gout.inputs['Geometry'])
    return ng


def _clamp_bbox(bm, half, radius):
    """Absolute cap on vertex COORDINATES: clamp every vertex into the initial
    box (half-extents ``half`` = (length, width, height)/2) grown by ``radius``
    (a few mm). Sculpt/crack passes can only recess or lightly bump within this
    envelope, never balloon the brick into a lump."""
    lx, ly, lz = half[0] + radius, half[1] + radius, half[2] + radius
    for v in bm.verts:
        v.co.x = min(max(v.co.x, -lx), lx)
        v.co.y = min(max(v.co.y, -ly), ly)
        v.co.z = min(max(v.co.z, -lz), lz)


def _shade_auto_smooth(obj, angle_deg):
    """Smooth-shade the mesh but keep hard edges wherever the dihedral angle is at
    least ``angle_deg`` (the true ~90-degree brick arrises): mark those edges sharp
    and every face smooth. Blender derives the split corner normals from that
    directly (no modifier), so the remesh faceting shades smooth while the arrises
    stay crisp -- and, being plain mesh data, it round-trips through OBJ export."""
    import math
    me = obj.data
    bm = bmesh.new()
    bm.from_mesh(me)
    thr = math.radians(angle_deg)
    for f in bm.faces:
        f.smooth = True
    bm.normal_update()
    for e in bm.edges:
        lf = e.link_faces
        e.smooth = (len(lf) == 2 and lf[0].normal.angle(lf[1].normal, 0.0) < thr)
    bm.to_mesh(me)
    bm.free()
    me.update()


def _bake_modifiers(obj):
    """Apply the object's current modifier stack into a plain mesh, in place."""
    dg = bpy.context.evaluated_depsgraph_get()
    baked = bpy.data.meshes.new_from_object(obj.evaluated_get(dg))
    obj.modifiers.clear()
    old = obj.data
    obj.data = baked
    if old.users == 0:
        bpy.data.meshes.remove(old)
    return baked


def _relax_arris(obj, half, band, strength=1.0, boost_far_axis=None, boost=1.0):
    """RELAX arris (box-edge) vertices toward the ideal straight box edges with a
    weight that FADES with distance from the edge -- straight where it counts,
    organic elsewhere. For each vertex the two nearest box faces define the local
    arris; the vertex's distance to that arris line is d = hypot(d0, d1) of the
    two face gaps. Weight w = strength * (1 - d/band)^2 (0 beyond *band*), and the
    two near coords are lerped toward their box bound by w. Right on the arris the
    edge is pulled straight; a band in it fades to the untouched textured face, so
    no dead-flat shelf.

    boost_far_axis / boost: multiply the weight for arrises PARALLEL to that axis
    (i.e. whose far/untouched axis is boost_far_axis) -- e.g. crisp the
    depth-running (Y) edges of a wall harder than the face edges. Vectorised."""
    me = obj.data
    n = len(me.vertices)
    if n == 0:
        return
    co = np.empty(n * 3, dtype=np.float64)
    me.vertices.foreach_get("co", co)
    co = co.reshape(n, 3)
    b = np.array(half, dtype=np.float64)
    df = b - np.abs(co)                              # gap inside each face (n,3)
    order = np.argsort(df, axis=1)                   # ascending gaps
    idx = np.arange(n)
    a0, a1 = order[:, 0], order[:, 1]                # the two NEAREST faces = arris
    d = np.hypot(np.maximum(df[idx, a0], 0.0), np.maximum(df[idx, a1], 0.0))
    w = strength * np.clip(1.0 - d / max(band, 1e-9), 0.0, 1.0) ** 2
    if boost_far_axis is not None and boost != 1.0:
        far = order[:, 2]                            # arris runs parallel to this axis
        w = np.where(far == boost_far_axis, np.clip(w * boost, 0.0, 1.0), w)
    for a in (a0, a1):
        tgt = np.sign(co[idx, a]) * b[a]
        co[idx, a] = co[idx, a] * (1.0 - w) + tgt * w
    me.vertices.foreach_set("co", co.reshape(-1))
    me.update()


def build_brick(name="brick", seed=0, length=0.25, height=0.09, width=0.11,
                edge_chip=0.007, corner_chip=0.02, chamfer_frac=0.6, detail=3,
                refine=0.25,
                chip=0.5, cracks=0.72, cracks2=0.5, decimate=0.42, decimate0=0.4,
                adaptivity=0.1, fine_div=160.0, fine_adaptivity=0.6, disp_scale=0.19,
                bounds_radius=0.006, final_decimate=0.42, micro=0.8, micro_env=0.9,
                micro_quant=0.0, micro_floor=0.032, micro_voxel_div=220.0,
                auto_smooth_deg=60.0, boxy=0.0, depth_edge_boost=1.0,
                collection=None):
    """Build one hewn brick object and return it (graph-based, no noise displace).

    Phase A: box -> seed-driven chipping -> multi-resolution loop (voxel-remesh
    to uniform topology, finer each stage; annealed multi-kernel facet sculpt;
    annealed decimation) -> angular dressed-stone facets.
    Phase B: a few cold, bounded refinement passes whose kernel SELECTION is
    seeded by a coherent Blender-noise field, laying fine tooling over the facets.

    refine -- magnitude of the Phase-B fine tooling (0 = none, ~1 = strong); it
              scales the strictly-bounded Phase-B displacement.
    """
    # `boxy` in [0,1] keeps the SILHOUETTE a crisp rectangular box: it flattens
    # the facet tilt (so sides stay vertical / top horizontal instead of sloping
    # into dressed facets), clamps the graph deformation, and kills the arris
    # chamfer + end-clipping. The fine surface grain (micro/refine) is retained,
    # so the faces still read as tool-dressed stone -- just on a true box.
    boxy = float(min(max(boxy, 0.0), 1.0))
    tilt_k = 1.0 - 0.92 * boxy               # -> nearly axis-aligned facets
    disp_k = 1.0 - 0.85 * boxy               # -> shallow deformation
    edge_chip *= (1.0 - 0.7 * boxy)
    corner_chip *= (1.0 - 0.85 * boxy)
    chamfer_frac *= (1.0 - 0.95 * boxy)
    chip *= (1.0 - boxy)
    cracks *= (1.0 - 0.6 * boxy)
    cracks2 *= (1.0 - 0.6 * boxy)
    bounds_radius *= (1.0 - 0.7 * boxy)
    # Mask the smoothing kernel on the box arrises/corners: crank box_preserve /
    # corner_preserve toward 1.0 so cross-edge smoothing is fully suppressed there
    # (otherwise the default 0.7/0.92 still lets ~30%/8% through -> rounded corners).
    box_pre = 0.7 + 0.30 * boxy
    corn_pre_a = 0.92 + 0.08 * boxy
    corn_pre_b = 0.95 + 0.05 * boxy

    rng = np.random.default_rng(seed)
    bm = _box(length, height, width)        # height & width are fixed (coursing)
    _chip(bm, rng, edge_chip, corner_chip, chamfer_frac=chamfer_frac)
    md0 = disp_scale * min(height, width) * disp_k  # coarse deformation bound
    mn = min(length, height, width)

    # --- Phase A: annealed faceting -> angular hewn base ---
    # Every remesh also decimates (the `decimate` param) so each scale is
    # normalised AND faceting-reduced into angular dressed facets.
    # (voxel, n_planes, tilt, iters, maxdisp, temp0, temp1, mix)
    # mix = (facet, sharp, pinch, flatten) selection probs; smoothing = remainder.
    # Voxel sizes are FINE (coarse voxels bevel the arrises); the strong decimate
    # below collapses the extra polys back down while keeping the sharp edges.
    # `boxy` makes the voxel remesh MUCH finer: a coarse voxel bevels the box
    # arrises before any masking can help, so the resolution must be fine enough
    # that the remesh preserves the sharp box edges. Divisors grow with boxy.
    vd0 = 9.0 * (1.0 + 5.0 * boxy)          # stage 0 was the big edge-rounder
    vd1 = 20.0 * (1.0 + 3.0 * boxy)
    vd2 = fine_div * (1.0 + 0.6 * boxy)
    schedule = [
        (mn / vd0,  16, 0.28 * tilt_k, 16, md0,        1.00, 0.15, (0.40, 0.16, 0.10, 0.14, 0.16)),
        (mn / vd1,  40, 0.36 * tilt_k, 14, md0 * 0.55, 0.70, 0.10, (0.38, 0.16, 0.10, 0.14, 0.16)),
        (mn / vd2,  88, 0.44 * tilt_k, 12, md0 * 0.26, 0.48, 0.06, (0.36, 0.16, 0.10, 0.14, 0.16)),
    ][:max(1, detail)]
    ns = len(schedule)
    half = (0.5 * length, 0.5 * width, 0.5 * height)
    for stage, (vox, npl, tilt, iters, mdisp, t0, t1, mix) in enumerate(schedule):
        # Stochastic end-chisel pass BEFORE the remesh, annealed down per stage.
        cw = (ns - stage) / ns
        nclip = int(round(3.0 * chip * cw))
        if nclip > 0:
            _clip_chunks(bm, rng, count=nclip, depth=mn * 0.16 * cw)
        # The coarse (stage-0) collapse has its own ratio: too aggressive and the
        # base loses the geometry later stages need, ending up too smooth; too
        # gentle and it stays lumpy. Finer stages use the base `decimate`.
        dec = decimate0 if stage == 0 else decimate
        # Finest stage: a much smaller voxel resolves the arrises, and a high
        # adaptivity simplifies the (now huge) flat regions back down so the crisp
        # convex edges survive without an explosion in poly count.
        adap = fine_adaptivity if stage == ns - 1 else adaptivity
        _remesh_voxel(bm, vox, adaptivity=adap, decimate=dec)
        _facet_sculpt(bm, rng, n_planes=npl, tilt=tilt, iters=iters,
                      pull=0.45, sharp=0.38, smooth=0.26, pinch=0.4, flatten=0.7,
                      mix=mix, temp0=t0, temp1=t1, maxdisp=mdisp, feature=1.0,
                      smooth_feature=1.3, corner_preserve=corn_pre_a,
                      box_preserve=box_pre)
    _clamp_bbox(bm, half, bounds_radius)     # cap Phase-A bulges to bbox + radius

    # --- Crack pass 1: subtle, ABSOLUTELY bounded, before the fine pass so it
    #     gets weathered down by Phase B (hairline/healed cracks) ---
    if cracks > 0.0:
        _crack(bm, rng, seeds=max(3, int(round(5 * cracks))), steps=17,
               step_len=mn * 0.03, branch=0.15, depth=mn * 0.016 * cracks,
               width=mn * 0.052, width_var=0.8, width_freq=45.0,
               vor_scale=34.0, vor_bias=0.35, wander=0.45, corner_bias=2.5,
               profile=1.45, maxdisp=mn * 0.024)
        _remesh_voxel(bm, mn / fine_div, adaptivity=fine_adaptivity,
                      decimate=decimate)                  # settle the cracks in

    # --- Phase B: fine refinement on the faceted base (topology kept) ---
    # A few cold passes with STRICTLY limited displacement and small feature
    # scale, where a coherent Blender-noise field seeds the kernel selection, so
    # fine tooling/chip patches lay down coherently over the dressed facets. Its
    # magnitude is the `refine` parameter (scales the displacement bound).
    if refine > 0.0:
        _facet_sculpt(bm, rng, n_planes=130, tilt=0.5 * tilt_k, iters=6,
                      pull=0.30, sharp=0.48, smooth=0.24, pinch=0.35, flatten=0.45,
                      smooth_flat_pow=2.0, smooth_feature=1.5,
                      mix=(0.22, 0.26, 0.08, 0.10, 0.16), temp0=0.22, temp1=0.05,
                      maxdisp=md0 * 0.08 * refine, feature=1.0,
                      corner_preserve=corn_pre_b, box_preserve=box_pre,
                      kernel_noise=70.0)
    _graph_smooth(bm, iters=1, lam=0.06 * (1.0 - boxy), feature=1.0)  # settle (off when boxy)

    # --- Crack pass 2: crisp, stronger, narrower, less frequent -- applied LAST
    #     (no remesh/smoothing after) so it stays sharp ---
    if cracks2 > 0.0:
        _crack(bm, rng, seeds=max(2, int(round(3 * cracks2))), steps=13,
               step_len=mn * 0.03, branch=0.06, depth=mn * 0.05 * cracks2,
               width=mn * 0.028, width_var=0.75, width_freq=62.0,
               vor_scale=48.0, vor_bias=0.4, wander=0.4, corner_bias=3.0,
               profile=2.4, maxdisp=mn * 0.055)

    _clamp_bbox(bm, half, bounds_radius)     # final absolute coordinate cap
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    mesh = bpy.data.meshes.new(name)
    bm.to_mesh(mesh)
    bm.free()
    obj = bpy.data.objects.new(name, mesh)
    (collection or bpy.context.scene.collection).objects.link(obj)

    # --- Micro-scale detail phase: a fine voxel remesh (so the displacement can
    #     resolve) then a geometry-nodes displacement (grain + per-facet pitting).
    #     These are baked in below rather than left as live modifiers. ---
    if micro > 0.0:
        fine = obj.modifiers.new("micro_remesh", 'REMESH')
        fine.mode = 'VOXEL'
        fine.voxel_size = mn / micro_voxel_div      # finer than any build stage
        fine.adaptivity = 0.0
        seed_off = tuple(float(x) for x in rng.uniform(-50.0, 50.0, 3))
        ng = _micro_node_group(name + "_micro", mn, micro, seed_off,
                               env_strength=micro_env, quant_step=micro_quant,
                               floor_frac=micro_floor)
        gn = obj.modifiers.new("micro_detail", 'NODES')
        gn.node_group = ng
        # Realise the micro geometry, then collapse-decimate to ~half and realise
        # again -- shedding the dense voxel tessellation while keeping the detail.
        _bake_modifiers(obj)
        dec = obj.modifiers.new("halve", 'DECIMATE')
        dec.decimate_type = 'COLLAPSE'
        dec.ratio = final_decimate
        _bake_modifiers(obj)
        if ng.users == 0:                    # drop the now-orphaned micro group
            bpy.data.node_groups.remove(ng)
    if auto_smooth_deg > 0.0:
        _shade_auto_smooth(obj, auto_smooth_deg)
    return obj
