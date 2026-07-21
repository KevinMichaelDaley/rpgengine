---
id: rpg-tl21
status: open
deps: []
links: []
created: 2026-07-21T01:37:54Z
type: task
priority: 2
assignee: KMD
parent: rpg-k23d
tags: [renderer, perf]
---
# Cut per-draw driver overhead: sort + redundancy filter + frame UBO

Section 1.2. Per renderable: material_bind (material.c:37-78) issues up to 7 glActiveTexture+glBindTexture + ~26 glUniform*, each through shader_uniform_resolve (shader_uniforms_init.c:5-21) -- a linear strcmp scan over up to 64/128 entries; hundreds of draws -> tens of thousands of strcmps/frame. texture_bind (texture_bind.c:5-14) has no last-bound check; draws submit in scene insertion order (no sort). The fix infra already exists unused on this path: draw/draw_list.h sort_key, draw_list_sort.c (radix), ubo/instance_data_ubo.c + ubo/frame_params_ubo.c.
Fixes: (a) skip re-emission in material_bind when mat==prev; same per-unit in texture_bind. (b) pointer-compare fast path in shader_uniform_find before strcmp (call sites pass literals). (c) sort by material->mesh->depth (adopt draw_list_sort). (d) move per-frame constants into frame_params_ubo. Note (section 1.8): UBO wrappers bind-to-0 after upload and subdata at offset 0 without orphaning -- orphan/ring-offset before per-frame use.

## Acceptance Criteria

Redundant material/texture binds and uniform re-emits are filtered; uniform lookup has a pointer-compare fast path; opaque draws sort material->mesh->depth; per-frame constants live in a UBO.

