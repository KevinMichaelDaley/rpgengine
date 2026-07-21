---
id: rpg-gky0
status: closed
deps: [rpg-jro2]
links: [rpg-zygg]
created: 2026-07-20T00:13:08Z
type: task
priority: 1
assignee: KMD
parent: rpg-hjck
tags: [gi, lightmap, streaming, core]
---
# Multi-chunk lightmap streaming (scale the lightmap tier)

CORE (massive worlds): stream N lightmap SH chunks, not one global atlas. Extend client_light_stream to page multiple <prefix>_cNNN.flm chunks through fr_asset_stream (bounded resident SH-array layers, like hall_lit_dynamic's sh_stream but streamer-managed), set each mesh's render_scene sh_layer to the resident layer of ITS chunk (from the ZLM1 manifest), remap uv1 into that chunk's atlas rect, and GATE residency by the dual prepass's visible_lm channel (already produced by gi_vis_prepass_run_dual, currently unused with 1 chunk). Depends on the chunked lightmap bake (rpg-jro2/yfa4) for the _cNNN.flm + ZLM1 data.

## Acceptance Criteria

A chunked-baked level pages lightmap chunks by on-screen visibility (not all resident); per-mesh sh_layer follows its resident chunk; single-atlas remains the 1-chunk case.

