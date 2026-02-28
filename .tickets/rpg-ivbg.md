---
id: rpg-ivbg
status: closed
deps: [rpg-clq6]
links: []
created: 2026-02-28T22:24:47Z
type: task
priority: 2
assignee: KMD
parent: rpg-6fi0
tags: [editor, mesh, uv]
---
# UV projection methods (planar, box, cylindrical, spherical)

Implement UV projection methods for generating texture coordinates on selected faces.

planar: Project UVs onto a plane defined by an axis (X, Y, or Z). Vertices are projected orthographically onto the plane, normalized to [0,1].

box: 6-sided box projection. Each face is projected onto the nearest axis-aligned plane. Produces clean UVs for architectural geometry.

cylindrical: Wrap UVs around a cylinder axis. The angular position maps to U, the height maps to V. Good for columns and round objects.

spherical: Wrap UVs around a sphere. Longitude maps to U, latitude maps to V. Good for round/organic shapes.

All methods operate on selected faces only. Unselected face UVs are unchanged.

Args:
- unwrap: {"method": "planar"|"box"|"cylindrical"|"spherical", "axis": "x"|"y"|"z"}

Files to create:
- include/ferrum/editor/mesh/mesh_uv.h — UV projection API
- src/editor/mesh/mesh_uv_planar.c — planar projection
- src/editor/mesh/mesh_uv_box.c — box projection
- src/editor/mesh/mesh_uv_cylindrical.c — cylindrical projection
- src/editor/mesh/mesh_uv_spherical.c — spherical projection
- src/editor/commands/cmd_unwrap.c — command handler
- tests/editor/mesh_uv_project_tests.c

## Acceptance Criteria

- Planar: UVs in [0,1] range for selected faces
- Box: each face projected to nearest axis plane, no stretch for axis-aligned faces
- Cylindrical: U wraps 0→1 around axis, V spans height
- Spherical: U = longitude, V = latitude
- Unselected face UVs unchanged
- Tests: planar each axis, box on cube, cylindrical on cylinder, spherical on sphere, partial selection

