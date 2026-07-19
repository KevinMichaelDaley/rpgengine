---
id: rpg-i3wx
status: closed
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


## Notes

**2026-07-19T18:10:33Z**

Inherits from rpg-nbp2: when lifting the demo assembly, rewire the two existing GL chunk streamers to flow through the new streaming manager -- retire the demo-local sh_stream (promote its GL_TEXTURE_2D_ARRAY upload into a load/upload callback) and make gi_sdf_stream's 3D-texture upload a callback too, registering both via fr_chunk_table over an fr_asset_stream_t. The headless residency model + chunk_table + probe gating already exist (rpg-nbp2); this is the GL wiring at the point the streamers are instantiated.

**2026-07-19T18:30:17Z**

Scope: this ticket = the reusable render_world engine-assembly module (render_forward + gi_runtime + render_scene + gi_bind wiring + probe grid + GI setters) lifted out of hall_lit_dynamic.c, + refactor the demo to route through it, verified pixel-identical. The GL chunk-streamer rewiring (sh_stream/gi_sdf_stream through the manager) moves to rpg-8302 where the streamer feeds render_world.

**2026-07-19T18:41:38Z**

Completed. New src/renderer/world/ render_world module (in liball.a): owns render_forward_t + gi_runtime_t over a borrowed render_scene_t, enforces the three contracts (gi.froxel==fwd.cluster, GI bind at unit 24, probe grid + all GI setters from config). render_world_init/update/destroy. hall_lit_dynamic.c refactored to route through it -- removed the g_gi global, the inline gi_bind_cb, the inline render_forward_init/gi_runtime_init/setters, and the per-frame gi_runtime_frame+render_forward_render (now one render_world_update). Verified: user confirmed identical look; ~100 fps (matches pre-refactor; pixel delta was only the fire-flicker RNG + GI temporal accumulation, run-to-run). -Wall -Wextra -Wpedantic clean. GL chunk-streamer rewiring moved to rpg-8302.
