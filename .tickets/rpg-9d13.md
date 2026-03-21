---
id: rpg-9d13
status: open
deps: [rpg-haqg, rpg-2585]
links: [rpg-c6o7]
created: 2026-03-21T07:00:00Z
type: task
priority: 2
assignee: kmd
parent: rpg-c6o7
---
# §5.2.2 Convex hull bone colliders

Implement four methods for defining convex hull collision geometry per bone.

## Methods

1. **Manual marker placement** — spawn MARKER entities in prefab mode, parent to bone. All markers under a bone with hull collider type define hull vertices. (Already partially supported via rpg-kte9.)

2. **Mesh vertex selection** — in mesh mode, select specific vertices, then assign to a bone's hull. Selected vertex positions converted to bone-local space.

3. **Vertex group** — named vertex weight groups with a configurable minimum weight threshold for inclusion. Vertices with weight >= threshold included in hull. Default threshold = 0.5.

4. **Weight threshold auto-hull** — slider in inspector sets minimum bone weight. All mesh vertices with weight >= threshold are collected, convex hull computed. Inspector shows vertex count, warns if > 64 (PHYS_CONVEX_MAX_VERTS). Hull wireframe updates in real-time as slider moves.

## Constraints

- Minimum 4 vertices for valid hull (tetrahedron)
- Maximum PHYS_CONVEX_MAX_VERTS (64) — warn if exceeded, offer decimation
- Hull wireframe renders in collision overlay, updates live during editing

## Key files

- src/editor/scene/scene_ui_bone_inspector.c — hull source selector, threshold slider
- src/physics/collider/convex_decompose.c — phys_convex_hull_build()
- src/editor/scene/scene_viewport_collision_overlay.c — hull wireframe rendering
- src/editor/scene/prefab/prefab_hull_build.c — existing hull build from markers
