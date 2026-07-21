---
id: rpg-0rs4
status: closed
deps: []
links: []
created: 2026-07-21T01:36:33Z
type: task
priority: 1
assignee: KMD
parent: rpg-k23d
tags: [renderer, perf]
---
# Shared frustum culling for forward, depth-pre, and shadow draw loops

Section 1.1. No camera frustum culling exists anywhere in the main view path -- render_forward.c:175-191 (fwd_forward_submit) and depth_prepass.c:80-91 iterate every renderable unconditionally. A working AABB-vs-frustum test already exists in shadow_csm_render.c:34-55 (csm_cull) using the same inputs (r->model, r->mesh->aabb_min/max). Hoist csm_extract_planes/csm_cull into a shared helper (e.g. src/renderer/cull/frustum_cull.c), build a per-frame visibility mask from camera.proj*view, and use it in fwd_depth_submit, fwd_forward_submit, and the per-light shadow loops. A draw_distance knob falls out of the same test.

## Acceptance Criteria

Off-screen geometry is skipped in the forward, depth-pre, and shadow passes via one shared frustum-cull helper; draw_distance bounds the far cutoff.


## Notes

**2026-07-21T02:14:40Z**

DONE. New src/renderer/cull/frustum_cull.{c,h}: frustum_extract_planes(mvp)+_vp(proj,view), frustum_cull_aabb, frustum_cull_aabb_ex(+draw_distance). Wired into render_forward.c (fwd_forward_submit) + depth_prepass.c (depth_prepass_execute now takes draw_distance) + shadow_csm_render.c refactored to the shared helper. render_forward_config_t.draw_distance fed from render_config.draw_distance in client_scene_load.c. Tests: tests/renderer/frustum_cull_tests.c 8/8 (+micro-bench ~80ns/box, culls 254/20000). Full hall_lit_dynamic pipeline renders unchanged. Commit ba34b92d. Also fixed pre-existing link rot (gi_probe_gpu_active.c + gi_probe_tuning.c missing from visual targets).
