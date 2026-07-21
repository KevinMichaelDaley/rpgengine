---
id: rpg-47qi
status: open
deps: []
links: []
created: 2026-07-21T01:35:45Z
type: task
priority: 3
assignee: KMD
parent: rpg-k23d
tags: [renderer, perf, cleanup]
---
# Latch renderer env vars at init instead of per-frame/per-dispatch getenv

Sections 1.8, 7.2. ~14 getenv calls remain in the hot path: render_forward.c:99 (PBR_DEBUG every frame), shadow_csm_render.c:86 + shadow_csm_cascade.c:249 (CSM_DEBUG per cascade per frame), ~15 getenvs per GI dispatch (gi_probe_gpu.c:748-790), plus FR_ANISO/GI_SVOX/LEGACY_SDF/CLIENT_NOSUN/CLIENT_BAKE_*. Latch once at init like PROF/PBR_OVERDRAW already do (render_forward.c:204-210). Make config the source of truth, env a live-tuning override only.

## Acceptance Criteria

No getenv in per-frame or per-dispatch code paths; env vars are read once at init and act as overrides on config.

