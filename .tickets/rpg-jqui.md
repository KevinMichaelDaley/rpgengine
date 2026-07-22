---
id: rpg-jqui
status: closed
deps: []
links: []
created: 2026-07-22T10:15:50Z
type: feature
priority: 1
assignee: KMD
---
# Renderer: material opacity/transmission plumbing end-to-end

Prerequisite for translucent shadows + sorted transparency: an opacity scalar (1 = opaque) flowing exporter -> .scene descriptor -> scene_desc parser -> render_material_t -> u_opacity uniform. Blender exporter reads the Principled Alpha input; la_glass gets alpha ~0.35 in the LA palette. Materials with opacity < 1 classify as translucent for the shadow and forward passes.

## Acceptance Criteria

Unit test: scene_desc material with opacity parses into render_material_t (default 1.0); material_bind uploads u_opacity; exporter writes opacity for alpha<1 materials.

