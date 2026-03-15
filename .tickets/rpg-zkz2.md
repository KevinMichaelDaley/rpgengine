---
id: rpg-zkz2
status: open
deps: []
links: []
created: 2026-03-14T11:50:58Z
type: feature
priority: 2
assignee: KMD
tags: [renderer, editor, lighting]
---
# Light system: types, entities, culling, and shader uniforms

Implement a proper light system for the engine and editor.

## Light types and structs

Define light type structs with all necessary parameters:
- **Directional light**: direction, color, intensity, shadow cascade config
- **Point light**: position, color, intensity, radius/attenuation, shadow cubemap config
- **Spot light**: position, direction, color, intensity, inner/outer cone angles, range, shadow map config
- **Area light** (future): shape, dimensions, color, intensity

Each light type should have a corresponding C struct (e.g., light_directional_t, light_point_t, light_spot_t) with all rendering-relevant parameters.

## Entity attributes for lights

- Add light entity type(s) to the editor entity system
- Light entities should be spawnable, movable, and configurable via the inspector panel
- Light properties (color, intensity, range, cone angles, etc.) stored as entity attributes
- Visual representation in editor: directional = arrow, point = sphere gizmo, spot = cone gizmo

## Distance/frustum-based culling

- Frustum culling for all light types (exclude lights outside camera frustum)
- Distance-based culling for point/spot lights (exclude lights beyond effective range)
- Per-light bounding volume computation (sphere for point, cone for spot)
- Tiled or clustered light assignment for forward+ rendering (light_cull pass in pipeline)

## Shader uniform integration

- Define UBO or SSBO layout for light arrays (directional, point, spot)
- Upload active lights to GPU each frame after culling
- Update all viewport shaders (Blinn-Phong/shaded, matcap) to sample from light uniforms
- Support configurable max light counts per type
- Shadow map sampling integration (future pass)

## Implementation notes

- The render pipeline already has a LIGHT_CULL pass slot (pass type 3) ready for tiled light assignment
- Frame params UBO (binding point 1) already has camera position; light UBO should use binding point 4
- Start with directional + point lights; spot lights can follow
- Editor should default to a single directional light matching the current hardcoded (0.577, 0.577, 0.577) direction

