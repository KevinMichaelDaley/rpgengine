---
id: rpg-oda7
status: open
deps: []
links: []
created: 2026-07-19T21:26:57Z
type: task
priority: 1
assignee: KMD
parent: rpg-hjck
tags: [gi, asset, streaming, client]
---
# Client light-data streaming subsystem (decouple + scaffold)

Standalone client subsystem that streams baked light data (lightmap SH chunks, SDF/voxel chunks, probes) via fr_asset_stream, SEPARATE from scene-descriptor loading. Owns one fr_asset_stream (job system as executor) + a chunk table per light-data kind + the visibility prepass. load() decodes on a job fiber (no GL); upload()/evict() run on the render thread via gpu_cmd_queue GPU_CMD_CUSTOM. Remove the synchronous 1.1GB lightmap load from client_scene_load; the descriptor loader keeps only manifest-metadata uv1 remap. FIRST INCREMENT: single atlas registered as 1 lightmap chunk, existing 8 _cNNN.sdf as SDF chunks; feed resident sh_layer + SDF into the render_world/gi_runtime. Design generically for zones+chunks (see ref/gi_streaming_design.md).

## Design

See ref/gi_streaming_design.md sections: 'Layering', 'fr_asset_stream integration', 'Renderer consumption', build-order step 1. Reuse resource_loader.c decode-on-fiber -> GPU_CMD upload pattern. Files: include/ferrum/renderer/light_stream.h + src/renderer/world/light_stream_*.c (<=4 non-static fns/file).

## Acceptance Criteria

Client renders great_hall with lightmap+SDF fed by fr_asset_stream (not synchronous); client_scene_load no longer opens the .flm; RAM/VRAM residency observed via fr_asset_stream_ram_used/vram_used; single atlas works as the 1-chunk case.

