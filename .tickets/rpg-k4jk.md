---
id: rpg-k4jk
status: open
deps: [rpg-51nf, rpg-ft0g]
links: []
created: 2026-07-19T07:44:03Z
type: task
priority: 4
assignee: KMD
parent: rpg-hjck
tags: [server, gi, headless]
---
# Server headless GI/visibility-stealth subset (libheadless); rename demo_server->server

Give the server a HEADLESS (no-GL) light/visibility model: load the level collision geo + a very low-res SDF + a coarse set of fixed priority probes, and compute per-probe direct lighting + baked indirect for visibility/stealth queries (how lit is a point / can an observer see it). Rename demo_server -> server.

## Design

MUST be GL-free (server links libheadless.a). Do NOT use the GPU compute probe path; reuse the CPU lightmap/GI code (src/lightmap lm_* -- lm_sdf sampling, lm_sh irradiance, lm_light direct) and the coarse probe set from T2. Expose a query API: sample_light(point)/visibility(observer,point) for stealth/AI. Feeds NPC/stealth systems, not rendering. Keep it cheap enough to run alongside the 30/60Hz physics tick.

## Acceptance Criteria

Server loads collision geo + low-res SDF + coarse probes headlessly and answers a lit-ness/visibility query for arbitrary points; builds and links into libheadless.a with no GL symbols; a headless test asserts lit vs shadowed points in the great_hall.


## Notes

**2026-07-19T18:27:14Z**

Deferred to end-ish of the pipeline (priority lowered). Nothing on the critical path depends on it now (q1cp dep removed).
