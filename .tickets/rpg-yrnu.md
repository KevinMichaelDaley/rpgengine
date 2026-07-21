---
id: rpg-yrnu
status: open
deps: []
links: [rpg-hjck]
created: 2026-07-20T00:08:18Z
type: epic
priority: 1
assignee: KMD
tags: [zones, streaming, gi, world]
---
# World/zone streaming (open-world two-level residency)

CORE feature (not polish): two-level residency for large open worlds/zones above the per-chunk GI streaming already built. A world/level = many ZONES (large spatial regions); zones are the open-world paging unit -- admitted/evicted by proximity + coarse visibility BEFORE their chunks stream. Each zone groups the fine units beneath it: the baker's light-data chunks (lightmap SH, SDF/voxel, probes), the geometry/meshes, materials, and colliders for that region. The chunk-level streaming (fr_asset_stream + dual visibility prepass + gi_runtime external residency) is done (rpg-zygg); zones add the coarse tier: a zone must be admitted before its chunks page in; distant zones fully evict (RAM/VRAM bounded regardless of world size). Includes: a zone manifest (world boxes -> zone id -> its chunk/asset set) emitted by the exporter; a zone table over fr_asset_stream that gates the chunk tables; per-zone streaming priority from player position (ties into rpg-3ldk STREAM_PRIORITY); zone borders / far-field handoff so GI + geometry are seamless across zone boundaries; server-side zone interest for network streaming. great_hall = one zone (trivial). See ref/gi_streaming_design.md 'Two-level residency: zones -> chunks'.

## Design

Builds ON the completed chunk streaming (rpg-zygg / rpg-oda7). Zone table = a coarse fr_chunk_table-like layer whose entries are zones (world boxes) each owning a set of chunk ids; zone admission registers/activates its chunks, eviction removes them. Reuse fr_asset_stream residency + budgets. Exporter emits the zone manifest. Coordinate with the client render-world + server level-load.

