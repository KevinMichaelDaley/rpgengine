---
id: rpg-jro2
status: closed
deps: [rpg-yfa4]
links: []
created: 2026-07-19T21:27:19Z
type: task
priority: 1
assignee: KMD
parent: rpg-hjck
tags: [gi, exporter, bake]
---
# Exporter lightmap_unpack + chunked lightmap bake + zone manifest

Produce chunked lightmap data so the streamer has per-chunk payloads. lightmap_unpack: split the baked packed atlas into per-chunk pages <prefix>_cNNN.flm + a ZLM1 manifest <prefix>_manifest.bin (per-mesh chunk id + atlas rect). A level too small for multiple chunks is the 1-chunk special case. Add a zone manifest grouping chunks into zones. Exporter (scripts/export_scene.py) emits it (or a post-bake step). Blender headless available.

## Design

See ref/gi_streaming_design.md 'Data prerequisites'. ZLM1 format is defined by sh_stream_load in hall_lit_dynamic.c (magic ZLM1, hdr{nm,n_chunks,aw,ah}, per-mesh int32 chunk + uint32 rect[4]).

## Acceptance Criteria

great_hall exports _cNNN.flm + _manifest.bin (>=1 chunk); client streamer pages them; single-atlas path still works as 1 chunk.

