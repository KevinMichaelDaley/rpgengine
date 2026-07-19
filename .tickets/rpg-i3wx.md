---
id: rpg-i3wx
status: open
deps: [rpg-51nf]
links: []
created: 2026-07-19T07:44:03Z
type: task
priority: 2
assignee: KMD
parent: rpg-hjck
tags: [renderer, gi]
---
# Reusable render-world / scene builder module (lift from demo)

Lift the 10-step scene assembly currently inline in hall_lit_dynamic.c main() into a reusable src/ module that, given a loaded scene descriptor + streamed assets, builds the render_scene_t, configures+inits render_forward, places/loads probes, and inits gi_runtime with all setters -- then exposes a per-frame update(scene,view,proj,colliders) that calls gi_runtime_frame + render_forward_render.

## Design

Preserve the coupling contracts: gi cfg.froxel == fwd cfg.cluster; material_extra_bind=gi_bind_cb (gi_runtime_bind at unit 24); probe index order (z*dim[1]+y)*dim[0]+x; DYNAMIC_INDIRECT light flag; render_scene dynamic_from split. Probes/static-irradiance/importance volumes now come from the descriptor+streamer (T1/T2/T3) instead of being computed inline. Keep build_static_irr_volume (promote to src/) for baked indirect. No behavior change vs the demo when fed the great_hall descriptor.

## Acceptance Criteria

A single reusable call assembles + renders the great_hall scene identically to hall_lit_dynamic.c (same look, same ~fps); the demo is refactored to call the new module; module has no static/global scene state.

