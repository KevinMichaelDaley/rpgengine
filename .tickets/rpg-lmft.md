---
id: rpg-lmft
status: in_progress
deps: []
links: []
created: 2026-02-18T04:53:48Z
type: task
priority: 2
assignee: KMD
---
# Convex decomposition for mesh colliders

## Problem

Large triangle meshes (e.g. armadillo: 212K tris) are too expensive for
per-triangle narrowphase.  The demo server runs at **42ms/tick** with 288
bodies — well above the 2ms budget — because every broadphase-overlapping
primitive must test against individual BVH-queried triangles via SAT or
closest-point routines.

## Goal

Replace high-poly triangle meshes with a set of convex hulls at load time.
Narrowphase then tests primitives against a small number of convex shapes
instead of thousands of triangles.  Target: armadillo collision cost drops
from ~30ms to <1ms (comparable to a few dozen box-vs-box tests).

## Design

### 1. Convex Hull Primitive (`PHYS_SHAPE_CONVEX = 4`)

Already reserved in `collider.h:34`.  Implement:

```c
typedef struct phys_convex_hull {
    phys_vec3_t *vertices;       /* unique hull vertices */
    uint32_t vertex_count;
    phys_vec3_t *face_normals;   /* outward face normals */
    uint32_t face_count;
    uint16_t *face_indices;      /* CCW vertex indices per face */
    uint16_t *face_offsets;      /* offset into face_indices per face */
    phys_vec3_t centroid;        /* precomputed centroid */
    phys_aabb_t aabb;            /* local-space AABB */
} phys_convex_hull_t;
```

Hard limits: ≤64 vertices, ≤64 faces per hull (keeps GJK/SAT fast).

### 2. Convex Decomposition Pipeline

At mesh load time (offline or on startup):

1. **Voxelize** the mesh interior at resolution ~32³–64³
2. **Flood-fill** connected voxel regions
3. **Approximate convex decomposition (ACD)**: iteratively split concave
   regions along the plane of maximum concavity until each piece is
   approximately convex (concavity threshold ≈ 0.05)
4. **Compute convex hull** of each piece's surface vertices (gift-wrapping
   or incremental algorithm)
5. **Simplify**: merge hulls that share large contact faces; decimate
   hulls exceeding 64 verts

Target: armadillo → 15–40 convex pieces.

### 3. Compound Convex Collider

A mesh body's collision shape becomes a **compound of convex hulls**:

```c
typedef struct phys_convex_compound {
    phys_convex_hull_t *hulls;
    uint32_t hull_count;
    phys_mesh_bvh_t bvh;         /* BVH over hull AABBs for broadphase */
} phys_convex_compound_t;
```

The existing compound collider pattern (`phys_compound_collider_t` /
`PHYS_SHAPE_COMPOUND`) can be extended, or a new shape type added.

### 4. Narrowphase: GJK + EPA

For convex-vs-convex and primitive-vs-convex:

- **GJK** (Gilbert-Johnson-Keerthi): boolean intersection test + closest
  points when separated
- **EPA** (Expanding Polytope Algorithm): penetration depth + contact
  normal when overlapping
- **Support function**: `phys_convex_hull_support(hull, direction)` →
  vertex with max dot product (linear scan over ≤64 verts)

Dispatch in `narrowphase.c`:
- `phys_sphere_vs_convex()` — GJK with sphere support
- `phys_box_vs_convex()` — GJK with OBB support
- `phys_capsule_vs_convex()` — GJK with capsule support
- `phys_convex_vs_convex()` — GJK + EPA

### 5. Fallback

Keep the existing triangle mesh path for:
- Meshes marked `solid` (interior detection needs triangles)
- CCD swept tests (still use triangle BVH)
- Meshes with < 100 triangles (decomposition overhead not worth it)

## File Plan

```
include/ferrum/physics/
  convex_hull.h          — phys_convex_hull_t, support function
  convex_decompose.h     — decomposition API
  convex_narrowphase.h   — GJK/EPA collision API

src/physics/collider/
  convex_hull.c          — hull construction, support query
  convex_decompose.c     — voxelize + ACD + hull extraction
  convex_simplify.c      — hull merging, vertex decimation

src/physics/collision/
  gjk.c                  — GJK algorithm
  epa.c                  — EPA algorithm
  narrowphase_convex.c   — primitive-vs-convex dispatch

tests/
  p113_convex_hull_tests.c
  p114_convex_decompose_tests.c
  p115_gjk_epa_tests.c
  p116_convex_narrowphase_tests.c
```

## Steps

1. **Convex hull struct + support function** — define type, implement
   `phys_convex_hull_support()`, build hull from point cloud
2. **GJK** — boolean intersection + closest points for separated shapes
3. **EPA** — penetration depth + normal for overlapping shapes
4. **Primitive-vs-convex narrowphase** — sphere, box, capsule vs hull
5. **Convex decomposition** — voxelize, ACD split, hull extraction
6. **Compound convex collider** — BVH over hulls, wire into world
7. **Demo integration** — decompose armadillo at load, benchmark

## Performance Targets

- Decomposition: <2s for 200K-tri mesh (one-time at load)
- Narrowphase: <50µs per convex-vs-primitive pair
- Armadillo total: <1ms with ~30 convex pieces (vs current ~30ms)
- Memory: <100KB per decomposed mesh (hulls + BVH)

## Dependencies

- Mesh collider system (Phase 9) ✅
- `PHYS_SHAPE_CONVEX` enum slot ✅

## References

- Approximate Convex Decomposition (Lien & Amato, 2006)
- V-HACD (Volumetric Hierarchical ACD) — algorithm reference
- GJK: "A fast procedure for computing the distance between complex
  objects in three-dimensional space" (Gilbert et al., 1988)
- EPA: "Collision Detection in Interactive 3D Environments" (van den Bergen)

