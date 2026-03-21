---
id: rpg-z9db
status: closed
deps: [rpg-air9]
links: []
created: 2026-03-17T06:48:00Z
type: feature
priority: 1
assignee: KMD
tags: [editor, physics, collider, entity]
---
# Collider-only primitive entity types

## Summary

Add collider-only versions of all primitive types (sphere, box, capsule, convex hull) to the editor entity system. These are visually empty objects that have physics colliders but no rendered geometry — they appear only as wireframe overlays when collision display is toggled (C key). The physics engine already has full support for these shapes; this ticket wires them into the editor/entity pipeline and asset tree.

## Background

The physics engine supports sphere, box, capsule, convex hull, halfspace, mesh, compound, and point colliders. The editor currently has BOX, SPHERE, CAPSULE entity types that are both visual AND physical — they render as solid primitives AND have physics bodies. For bone collision (ragdoll, IK), users need to attach invisible collision shapes to bones without adding visual geometry.

## Deliverables

### New Entity Types
Add to `edit_entity.h` / entity type registry:
- `EDIT_ENTITY_TYPE_COLLIDER_SPHERE` — invisible sphere collider
- `EDIT_ENTITY_TYPE_COLLIDER_BOX` — invisible box collider
- `EDIT_ENTITY_TYPE_COLLIDER_CAPSULE` — invisible capsule collider
- `EDIT_ENTITY_TYPE_COLLIDER_HULL` — invisible convex hull collider (vertex data from markers or explicit point set)

These types:
- Have physics bodies with the corresponding collider shape
- Do NOT render in the solid pass (no geometry in the forward pass)
- DO render as wireframe in collision overlay mode (C key toggle)
- Support full transform gizmos (translate, rotate, scale)
- Have inspector properties: shape parameters (radius, half-extents, height), mass, static/kinematic flags, collision group

### Spawn Integration
- `cmd_spawn.c`: accept `type: "collider_sphere"`, `"collider_box"`, `"collider_capsule"`, `"collider_hull"` in spawn command
- Physics bridge `on_spawn`: create body with appropriate collider shape
- Shape parameters passed via entity attrs or spawn args (e.g., `{"type":"collider_capsule","radius":0.3,"height":1.0}`)

### Viewport Rendering
- `scene_viewport_draw.c`: skip collider-only types in the solid pass
- `scene_viewport_collision_overlay.c`: render collider wireframe using existing primitive mesh generators (box/sphere/capsule already exist)
- For hull colliders: render wireframe from the hull vertex data

### Asset Tree Support
- Collider entities are saved/loaded as part of scene files
- Collider parameters stored in entity_attrs (new SCRIPT_KEYs for shape params)
- Serialized identically to other entity types via edit_entity_json

### Outliner
- Show collider entities in outliner with distinct icon/prefix (e.g., "[C]" prefix or collision icon)
- Collider entities can be parented to other entities (especially bones, in prefab mode)

## Acceptance Criteria
- [ ] `spawn {"type":"collider_sphere","pos":[0,1,0],"radius":0.5}` creates an invisible sphere collider
- [ ] Pressing C shows wireframe for all collider-only entities
- [ ] Collider entities have physics bodies and participate in simulation
- [ ] Collider entities are fully transformable via gizmos
- [ ] Inspector shows shape-specific properties (radius, height, half-extents)
- [ ] Save/load preserves collider entities and their parameters
- [ ] Collider entities do NOT appear in the solid render pass

