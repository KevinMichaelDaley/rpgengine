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


def _clip_chunks(bm, rng, count, depth, tilt=0.16):
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
        nrm[1] = float(rng.normal(0.0, tilt))            # ... only slightly rotated
        nrm[2] = float(rng.normal(0.0, tilt))
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


def _micro_node_group(group_name, mn, micro, seed_offset, env_strength=0.85):
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
    layers = [(1.0 / (mn * 0.16), (0.0, 0.0, 0.0), 1.0),
              (1.0 / (mn * 0.12), (13.1, 4.7, 9.3), 1.0),
              (1.0 / (mn * 0.09), (2.9, 21.4, 15.8), 0.9)]
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
    pit = nd('ShaderNodeMapRange', 500, -300)
    pit.clamp = True
    pit.inputs['From Min'].default_value = 0.0
    pit.inputs['From Max'].default_value = 0.55
    pit.inputs['To Min'].default_value = -mn * 0.022 * micro
    pit.inputs['To Max'].default_value = 0.0
    links.new(prev, pit.inputs['Value'])

    # Low-frequency fBm grain (few octaves) riding on top of the pits.
    grain = nd('ShaderNodeTexNoise', 100, 300)
    grain.noise_dimensions = '3D'
    grain.inputs['Scale'].default_value = 1.0 / (mn * 0.6)
    grain.inputs['Detail'].default_value = 2.0
    grain.inputs['Roughness'].default_value = 0.5
    links.new(base.outputs['Vector'], grain.inputs['Vector'])
    gmap = nd('ShaderNodeMapRange', 300, 300)
    gmap.inputs['To Min'].default_value = -mn * 0.014 * micro
    gmap.inputs['To Max'].default_value = mn * 0.014 * micro
    links.new(grain.outputs['Fac'], gmap.inputs['Value'])

    # Sum grain + pits -> raw scalar displacement.
    disp = nd('ShaderNodeMath', 660, 0); disp.operation = 'ADD'
    links.new(gmap.outputs['Result'], disp.inputs[0])
    links.new(pit.outputs['Result'], disp.inputs[1])

    # Broad low-frequency envelope: modulates amplitude across the brick so some
    # patches stay near-smooth while others are rougher -> non-uniform relief.
    # A narrow input band makes the swing high-contrast (near-bimodal) rather
    # than a gentle gradient; env_strength sets how flat the smooth patches go.
    env = nd('ShaderNodeTexNoise', 300, -760)
    env.noise_dimensions = '3D'
    env.inputs['Scale'].default_value = 1.0 / (mn * 2.2)
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

    # Offset = normal * modulated displacement.
    nrm = nd('GeometryNodeInputNormal', 700, 200)
    off_v = nd('ShaderNodeVectorMath', 900, 200); off_v.operation = 'SCALE'
    links.new(nrm.outputs['Normal'], off_v.inputs[0])
    links.new(dmod.outputs['Value'], off_v.inputs['Scale'])

    setp = nd('GeometryNodeSetPosition', 950, 0)
    links.new(gin.outputs['Geometry'], setp.inputs['Geometry'])
    links.new(off_v.outputs['Vector'], setp.inputs['Offset'])
    # Shade smooth so the resolved micro relief reads as form, not voxel facets.
    smooth = nd('GeometryNodeSetShadeSmooth', 1120, 0)
    links.new(setp.outputs['Geometry'], smooth.inputs['Geometry'])
    links.new(smooth.outputs['Geometry'], gout.inputs['Geometry'])
    return ng


def build_brick(name="brick", seed=0, length=0.25, height=0.09, width=0.11,
                edge_chip=0.007, corner_chip=0.02, detail=3, refine=0.25,
                chip=0.5, cracks=0.6, cracks2=0.5, decimate=0.6,
                micro=1.0, micro_env=0.85, micro_voxel_div=85.0, collection=None):
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
    bm = _box(length, height, width)        # height & width are fixed (coursing)
    _chip(bm, rng, edge_chip, corner_chip)
    md0 = 0.32 * min(height, width)         # coarse deformation bound
    mn = min(length, height, width)

    # --- Phase A: annealed faceting -> angular hewn base ---
    # Every remesh also decimates (the `decimate` param) so each scale is
    # normalised AND faceting-reduced into angular dressed facets.
    # (voxel, n_planes, tilt, iters, maxdisp, temp0, temp1, mix)
    schedule = [
        (mn / 6.0,  16, 0.28, 16, md0,        1.00, 0.15, (0.48, 0.28, 0.24)),
        (mn / 13.0, 40, 0.36, 14, md0 * 0.55, 0.70, 0.10, (0.42, 0.30, 0.28)),
        (mn / 26.0, 88, 0.44, 12, md0 * 0.26, 0.48, 0.06, (0.36, 0.30, 0.34)),
    ][:max(1, detail)]
    ns = len(schedule)
    for stage, (vox, npl, tilt, iters, mdisp, t0, t1, mix) in enumerate(schedule):
        # Stochastic end-chisel pass BEFORE the remesh, annealed down per stage.
        cw = (ns - stage) / ns
        nclip = int(round(3.0 * chip * cw))
        if nclip > 0:
            _clip_chunks(bm, rng, count=nclip, depth=mn * 0.16 * cw)
        _remesh_voxel(bm, vox, decimate=decimate)
        _facet_sculpt(bm, rng, n_planes=npl, tilt=tilt, iters=iters,
                      pull=0.45, sharp=0.38, smooth=0.42, mix=mix,
                      temp0=t0, temp1=t1, maxdisp=mdisp, feature=1.0)

    # --- Crack pass 1: subtle, ABSOLUTELY bounded, before the fine pass so it
    #     gets weathered down by Phase B (hairline/healed cracks) ---
    if cracks > 0.0:
        _crack(bm, rng, seeds=max(3, int(round(5 * cracks))), steps=15,
               step_len=mn * 0.03, branch=0.14, depth=mn * 0.014 * cracks,
               width=mn * 0.055, width_var=0.8, width_freq=45.0,
               vor_scale=34.0, vor_bias=0.35, wander=0.45, corner_bias=2.5,
               profile=1.4, maxdisp=mn * 0.02)
        _remesh_voxel(bm, mn / 26.0, decimate=decimate)  # settle the cracks in

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

    # --- Crack pass 2: crisp, stronger, narrower, less frequent -- applied LAST
    #     (no remesh/smoothing after) so it stays sharp ---
    if cracks2 > 0.0:
        _crack(bm, rng, seeds=max(2, int(round(3 * cracks2))), steps=13,
               step_len=mn * 0.03, branch=0.06, depth=mn * 0.05 * cracks2,
               width=mn * 0.028, width_var=0.75, width_freq=62.0,
               vor_scale=48.0, vor_bias=0.4, wander=0.4, corner_bias=3.0,
               profile=2.4, maxdisp=mn * 0.055)

    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    mesh = bpy.data.meshes.new(name)
    bm.to_mesh(mesh)
    bm.free()
    obj = bpy.data.objects.new(name, mesh)
    (collection or bpy.context.scene.collection).objects.link(obj)

    # --- Micro-scale detail phase: added as MODIFIERS (not applied) so the very
    #     fine remesh the displacement needs can be DISABLED IN VIEWPORT for a
    #     fast coarse preview, while render/bake evaluates it and resolves the
    #     micro detail. Order: fine remesh -> geometry-nodes displacement. ---
    if micro > 0.0:
        fine = obj.modifiers.new("micro_remesh", 'REMESH')
        fine.mode = 'VOXEL'
        fine.voxel_size = mn / micro_voxel_div      # finer than any build stage
        fine.adaptivity = 0.0
        fine.show_viewport = False                  # off in viewport, on in render
        seed_off = tuple(float(x) for x in rng.uniform(-50.0, 50.0, 3))
        ng = _micro_node_group(name + "_micro", mn, micro, seed_off,
                               env_strength=micro_env)
        gn = obj.modifiers.new("micro_detail", 'NODES')
        gn.node_group = ng
        gn.show_viewport = False
    return obj
