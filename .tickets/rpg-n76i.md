---
id: rpg-n76i
status: open
deps: []
links: []
created: 2026-07-21T01:37:54Z
type: task
priority: 1
assignee: KMD
parent: rpg-k23d
tags: [renderer, perf, shader]
---
# Compile-time PBR shader variants (LOW/MED/FULL)

Section 3.1. pbr_shader.c is one ubershader: u_gi_enabled, u_probe_grid_on, u_csm_pcss, u_clustered, u_sh_enabled, 7 u_has_* flags, 12 debug modes -- all runtime-branched. Uniform branches skip execution but NOT register allocation: the live sets (vec3 L[9] from sh9_fetch :146-150, float y[9], six vec4 trilinear accumulators :443, froxel loop state) are allocated simultaneously, capping occupancy on GCN/Iris and stalling every fetch. pbr_shader_create already assembles source strings -- prepend a #define block and build 3-4 variants (FULL / NO_GI / LOW = GI off + SH4 + fixed 4-tap PCF + no PCSS + no debug), selected via a pbr_quality config field. Compile debug modes out of release. Est. 20-40% forward-pass gain on register-limited GPUs, independent of other fixes.

## Acceptance Criteria

pbr_shader builds selectable LOW/MED/FULL variants via #define at create time chosen by pbr_quality; disabled features cost no registers; debug modes are compiled out of release.

