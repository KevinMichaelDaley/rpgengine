---
id: rpg-zket
status: open
deps: [rpg-w1qe]
links: []
created: 2026-07-13T05:09:19Z
type: epic
priority: 1
assignee: KMD
---
# Clustered forward+ render pipeline + scene interface + light entities

The full realtime render pipeline in the core renderer: a clustered forward+ shading path that drives the PBR shader, plus the scene submission interface (renderables, camera, lights) and first-class light entity types (point/spot/directional/area). Sets up froxel/cluster light culling so each shading invocation sees only the lights affecting its cluster. This is the mainline lit path; the deferred particle-light pass is a separate post-stage (its own epic).

## Design

Lives entirely in the core renderer module; NONE embedded in demo_client (demo_client only constructs a scene and calls the pipeline). Extends the existing render_pipeline_* graph with passes: depth pre-pass -> cluster/froxel light cull -> forward+ shading (PBR). Scene interface: render_scene_t / draw-list of (mesh + material + transform) + camera + light list. Light entities carry position/dir/color/intensity/radius/cone and a realtime-vs-baked flag. Cluster grid + per-cluster light index buffers uploaded to GPU (SSBO/UBO). TDD + extreme modularity.

## Acceptance Criteria

A scene of renderables + lights submitted through the scene interface renders via the clustered forward+ pipeline using the PBR shader, with correct per-cluster light culling (many lights, only relevant ones shaded per fragment). Light entities are first-class and editable. Pipeline is reusable and entirely in core; demo_client only builds a scene and invokes it. Clean under -Wpedantic.

