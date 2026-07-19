---
id: rpg-ft0g
status: open
deps: []
links: []
created: 2026-07-19T07:44:03Z
type: task
priority: 1
assignee: KMD
parent: rpg-hjck
tags: [gi, format]
---
# Probe placement spec: manual probes + distance/LOD resolution + box importance

The probe grid is ALREADY generated from a resolution at runtime (hall_lit_dynamic.c: GI_PSPACE/GI_VSPACE -> pnx*pny*pnz -> gi_runtime_set_probe_grid; and gi_runtime already accepts explicit probe_pos_in). The DEFAULT (auto-generate from a resolution) stays. This task adds the small set of overrides the level pipeline needs, plus a light on-disk spec so a level can ship them:
1. Optional MANUALLY-PLACED probes (feed gi_runtime probe_pos_in -- already plumbed; just needs a file + descriptor entry).
2. Resolution by DISTANCE / LOD (denser probes near the player / important geometry, sparser far -- vary cell size by distance/LOD instead of one uniform spacing).
3. Per-region IMPORTANCE OVERRIDES shaped like BOXES that raise/lower probe density (and streaming priority) inside them.

## Design

Keep the runtime default: no probe file -> auto-grid from a resolution (current behavior, unchanged). The spec is additive:
- A small .probes descriptor (referenced by the scene descriptor, rpg-51nf): optional list of manual probe positions; a base resolution + a distance/LOD falloff rule; and a list of AABB importance boxes {min, max, density_mult, priority_bias}.
- Manual probes: pass straight through as gi_runtime probe_pos_in (grid index order (z*dim[1]+y)*dim[0]+x preserved when they form a grid).
- Importance boxes modulate the generated density per region (and feed the streamer's priority, rpg-nbp2/rpg-3ldk).
- Optional baked probe SH may be serialized too (coarse for the server-stealth model rpg-k4jk, dense for the client), but is NOT required -- probes can be (re)generated at load. Reuse lm_sdf_file.c-style binary IO; loadable headless (no GL).

CHUNK-DRIVEN LOAD/GEN: which probes are loaded or generated is gated by which baked light-data CHUNKS are resident (rpg-nbp2). A probe belongs to the SDF/lightmap chunk whose world box (gi_sdf_chunk.origin/dims) contains it; when a chunk pages in, its probes are generated/loaded, and when it evicts they drop -- so probe residency tracks the light-data chunk residency and the same server-suggested priority order.

## Acceptance Criteria

Default path unchanged (no file -> auto-grid). A level can (a) load manually-placed probes, (b) specify probe resolution that varies by distance/LOD, and (c) raise probe density inside AABB importance boxes; the loaded manual set reproduces a hall_lit_dynamic-style pg_origin/pg_cell/pg_dim grid; probe load/generation is gated by resident light-data chunks (probe in an evicted chunk's box is not resident); spec loads headlessly.

