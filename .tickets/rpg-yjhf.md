---
id: rpg-yjhf
status: open
deps: [rpg-o0a7]
links: []
created: 2026-03-02T18:38:47Z
type: task
priority: 2
assignee: KMD
---
# Phase 4a: render_material_t and material registry

Create the material type binding shader programs to parameters (uniforms + textures). See ref/renderer_spec.md §5.

Deliverables:
- include/ferrum/renderer/material/render_material.h: material_texture_slot_t enum (ALBEDO/NORMAL/ROUGHNESS/METALLIC/EMISSIVE/LIGHTMAP), material_flags_t enum (DOUBLE_SIDED/ALPHA_CLIP/ALPHA_BLEND/UNLIT/WIREFRAME), render_queue_t enum (OPAQUE/ALPHA_CLIP/TRANSPARENT/OVERLAY), render_material_t struct (shader_handle, textures[6], base_color[4], roughness, metallic, emissive_strength, alpha_cutoff, flags, queue)
- include/ferrum/renderer/material/material_registry.h: material_registry_t (1024 capacity, freelist, generation counters), handle type
- src/renderer/material/render_material.c: render_material_bind() applies state in order: glUseProgram (if changed), bind textures to units 0-5 (only changed slots), upload material UBO, set render state (cull, blend, depth)
- src/renderer/material/material_registry.c: init/insert/remove/get with generation-guarded handles
- Material UBO layout (std140 binding=0): base_color(vec4), roughness(float), metallic(float), emissive_strength(float), alpha_cutoff(float)
- Tests in tests/p004_renderer_material_tests.c

Depends on: rpg-o0a7 (scene graph needed for entity-material binding)

