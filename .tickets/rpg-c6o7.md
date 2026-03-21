---
id: rpg-c6o7
status: open
deps: [rpg-haqg]
links: []
created: 2026-03-12T06:48:52Z
type: task
priority: 2
assignee: KMD
parent: rpg-0n7d
---
# §5.2 Collision Body and Joint Setup

See ref/scene_editor_design.md §5.2. Inspector: collision body section (shape/radius/height/mass), auto-fit from mesh. Joint type dropdown with per-type panels. Cone twist (3-axis limits with viewport cones). Hinge, twist, ball socket, distance, lock, aim, copy/limit rotation/position. Joint physics (stiffness/damping/compliance/drive). Constraint stack with enable/disable. Animation damping factor.

## Collision Body Shapes

Each bone can have a collision body with one of these shape types:
- **Primitive** (box, sphere, capsule) — parameters set manually or auto-fit from mesh bounding box
- **Convex hull** — defined by one of the following methods:

### Convex Hull Source Methods

Convex hulls for bone colliders cannot be naively inferred from all vertices weighted to a bone (the weight set is typically too large and includes distant vertices with small weights). Instead, the hull vertex set must come from one of:

1. **Manual marker placement** — spawn MARKER entities in prefab mode, parent to bone. All markers under a bone with hull collider type define the hull vertices. (Already supported via rpg-kte9.)

2. **Mesh vertex selection** — in mesh mode, select specific vertices, then assign them to a bone's hull. The selected vertex positions (in bone-local space) define the hull.

3. **Vertex group** — named vertex groups (weight paint groups) with a configurable **minimum weight threshold** for inclusion. Vertices with weight >= threshold for that bone are included in the hull. Default threshold = 0.5 (excludes low-influence vertices that would bloat the hull).

4. **Weight threshold auto-hull** — given a bone and a weight threshold (e.g., 0.8), automatically collect all mesh vertices with bone weight >= threshold, compute convex hull. Inspector shows vertex count and warns if > 64 (PHYS_CONVEX_MAX_VERTS). User can adjust threshold slider to include/exclude vertices and see hull update in real-time.

### Hull Constraints
- Minimum 4 vertices for valid hull (tetrahedron)
- Maximum PHYS_CONVEX_MAX_VERTS (64) — warn if exceeded, offer decimation
- Hull wireframe renders in collision overlay, updates live during editing

## Acceptance Criteria

- [ ] Inspector shows collision body section per bone (shape type, parameters)
- [ ] Primitive shapes (box/sphere/capsule) with auto-fit from mesh
- [ ] Convex hull via markers, vertex selection, vertex group, or weight threshold
- [ ] Weight threshold slider with live hull preview in viewport
- [ ] Joint type dropdown with per-type parameter panels
- [ ] Constraint limits visible as colored overlays (cones, arcs, etc.)
- [ ] All 11 joint types supported with full parameter editing
- [ ] Constraint stack with enable/disable per constraint
- [ ] Cross-reference with ref/scene_editor_spec.md and ref/scene_editor_ux.md

