# Client GI / Light-Data Streaming Design (rpg-8302 / rpg-hjck)

Status: design captured 2026-07-19. Authority: user directives in the rpg-hjck
epic + conversation history (no prior doc existed). This is the target
architecture for how the **client** loads and renders a level's baked light data.

## Goal / rationale

Levels will eventually be **huge open worlds/zones**. You must never hold all of a
level's lightmaps, probes, and SDF/voxel data in RAM or VRAM at once. All of that
GI data streams in **by the asset streamer** (`fr_asset_stream`, rpg-nbp2),
disk→RAM→VRAM, under RAM/VRAM budgets, **gated by a visibility prepass** that says
which chunk indices are on-screen. This is a **separate subsystem** from loading
the scene descriptor.

Design it **generically for large streamed open worlds/zones** from the start —
the current single-level great_hall is just the small special case.

## Layering: descriptor load vs light-data streaming (MUST be separate)

- **Scene descriptor load** (`client_scene_load`, from the `.scene` file): geometry
  (meshes), materials, lights, colliders, and *metadata* references to the light
  data (lightmap/sdf prefixes, chunk/zone manifests). Builds the `render_world`
  skeleton. Remaps each mesh's `uv1` into its atlas rect using the **manifest**
  (metadata only — tiny). **No** chunk pixel/voxel/probe payloads are read here.
- **Light-data streaming** (new `client_light_stream` module): a standalone
  subsystem, its own init + per-frame tick, that owns the `fr_asset_stream`, the
  chunk tables, and the visibility prepass, and feeds only-resident chunks to the
  forward SH sampler + `gi_runtime`. The 1.1 GB single-atlas synchronous load in
  the descriptor path is exactly what this replaces.

## Two-level residency: zones → chunks

- **Zone**: a large spatial region. A world/level = many zones. Zones are the
  open-world paging unit: admitted/evicted by proximity + coarse visibility. A
  zone groups the chunks (and their manifest/boxes) beneath it. The streamer works
  **zone by zone AND chunk by chunk** — a zone must be admitted before its chunks
  stream; chunks page within admitted zones by fine visibility.
- **Chunk**: the fine unit within a zone. There are **two independent chunk grids**
  because their natural boundaries differ:
  - **SDF / albedo-voxel chunks**: axis-aligned voxel bricks on a fixed voxel grid,
    described by world boxes (`<prefix>_cNNN.sdf`). Boundaries = the voxel brick grid.
  - **Lightmap SH chunks**: atlas partitions — each chunk is one SH atlas image, and
    meshes are assigned to a chunk by the baker's atlas packing (`<prefix>_cNNN.flm`
    + `ZLM1` manifest). Boundaries = atlas packing, **not** aligned to SDF voxels.
- Because SDF and lightmap chunk boundaries are independent, they are **separate
  index sets** with separate residency.

## Visibility prepass: two index sets, one geometry pass

Extend `gi_vis_prepass` (today: separate MESH-mode and WORLD-mode passes) into a
**single low-res geometry pass writing two chunk indices to different channels** of
an integer MRT:
- channel R = **SDF chunk id** (per-fragment, from world position vs SDF chunk
  boxes — WORLD mode).
- channel G = **lightmap chunk id** (per-mesh uniform: the mesh's atlas chunk —
  MESH mode).
Two async PBO readbacks → the visible SDF-chunk set + the visible lightmap-chunk
set (1 frame late). Zone visibility derives from chunk visibility (a zone is wanted
if any of its chunks is visible) plus proximity.

## fr_asset_stream integration (+ job system + command-buffer separation)

One `fr_asset_stream` per client: `cfg.jobs` = the job system, `ram_budget`,
`vram_budget`, `max_in_flight`, `capacity`. Classes:
`FR_ASSET_LIGHTMAP_CHUNK`, `FR_ASSET_SDF_CHUNK`, `FR_ASSET_PROBE` (plus
MESH/TEXTURE/SKELETON/COLLIDER for the rest). One `fr_chunk_table` per light-data
kind layers world boxes over the stream; `fr_chunk_table_set_interest(point,
scale)` sets distance priority, visibility raises it further (visible chunks pinned
/ high priority).

Callbacks (thread boundary is the whole point — reuse the existing separation):
- `load` — runs on a **job fiber** (dispatched by the streamer via `job_dispatch`).
  Reads + decodes the chunk file into slot RAM. Returns RAM bytes. **No GL.**
- `upload` — runs on the **render/GL thread** (from `fr_asset_stream_tick`). Pushes a
  **`GPU_CMD_CUSTOM`** onto the `gpu_cmd_queue`; `gpu_executor_drain` executes it on
  the GL thread to upload into the resident GPU slot (SH 2D-array layer / SDF 3D
  texture / probe buffer). Returns VRAM bytes; frees the RAM copy. Same pattern as
  `src/renderer/resource/resource_loader.c` and hall_lit_dynamic's `finalize_*`.
- `evict` — render thread: free the GPU slot + RAM.

Zones: a coarse residency gate above chunks (a parent priority bucket or a separate
zone table). A zone's admission bundles its chunk manifest + boxes.

## Renderer consumption

- **Forward SH**: per-mesh `sh_layer` = the resident GPU layer of the mesh's lightmap
  chunk (or `-1` → not resident, fall back to probe GI / ambient). `uv1` is
  pre-remapped into the chunk atlas at mesh build (manifest rects). The forward pass
  has **no per-mesh rect uniform**, so uv1 must already be atlas-space
  (`lm_atlas_remap_uv`).
- **gi_runtime SDF**: bind the resident SDF chunk 3D textures. `gi_runtime` must
  accept **externally-managed** SDF residency instead of self-loading `gi_sdf_stream`
  (today it owns + RAM-caches every chunk — the thing we're removing).
- **Probes**: stream probe sets per chunk/zone; `gi_runtime` uses the resident set.

## Data prerequisites

- **Chunked lightmap**: the exporter/baker must emit `<prefix>_cNNN.flm` +
  `<prefix>_manifest.bin` (`ZLM1`: per-mesh chunk id + atlas rect). This is
  **`lightmap_unpack`** — unpacking the packed atlas into per-chunk pages + manifest.
  A level too small for multiple chunks is the **1-chunk special case**. A zone
  manifest groups chunks into zones.
- **SDF** already chunked (`<prefix>_cNNN.sdf`). **Probes** per chunk/zone.

## Build order (incremental, each verifiable)

1. **Decouple + scaffold** (do first): standalone `client_light_stream` owning
   `fr_asset_stream` + chunk table(s) + the vis prepass. Single atlas registered as
   **1 lightmap chunk**, existing 8 SDF chunks as SDF chunks. Wire `load` (fiber) +
   `upload` (`GPU_CMD_CUSTOM`); feed `sh_layer` + SDF into the render. **Remove** the
   synchronous lightmap load from `client_scene_load`.
2. **Dual-output visibility prepass** (SDF id + lightmap id → 2 channels, one pass).
3. **Real multi-chunk lightmap**: exporter `lightmap_unpack` + a chunked bake path +
   zone manifest.
4. **Zone-level residency gate** (zone-by-zone above chunk-by-chunk).
5. **gi_runtime accepts external SDF residency** (retire the self-load / RAM-cache).
6. **STREAM_PRIORITY** (server hints, rpg-3ldk) + player-distance interest feed the
   priorities; ties the whole thing to server-assigned order.

## Scaling to massive worlds (CORE, not polish)

The target is not one hall but **open zones / worlds of many castles, each with its
own great-hall-class interior somewhere inside**. That makes the following CORE
streaming requirements (any of them missing caps the world size):

1. **Chunked lightmaps** (rpg-yfa4 bake + rpg-jro2 manifest). A single global atlas
   (one 109 MB `.flm` for one hall) does not scale and is too low-res spread over a
   zone. The baker emits **one atlas per chunk** (`<prefix>_cNNN.flm`) + a `ZLM1`
   manifest (per-mesh chunk id + atlas rect), baking each chunk's meshes against the
   FULL scene (shared far-field). The in-client bake (client_bake.c) must gain the
   per-chunk loop.
2. **Multi-chunk lightmap streaming** (rpg-zygg). The client pages N lightmap SH
   chunks through `fr_asset_stream` (per-mesh `sh_layer` = the resident layer of the
   mesh's chunk, single-pass — like hall_lit_dynamic's `sh_stream`), gated by the
   dual prepass's **`visible_lm`** channel (already produced, currently unused
   because there is only 1 chunk). uv1 remapped into each mesh's chunk atlas rect
   from the manifest.
3. **Unified residency budget**. One `fr_asset_stream` RAM/VRAM budget shared across
   lightmap + SDF + probe chunks so residency is bounded regardless of world size
   (today the lightmap + SDF are separate streams / budgets — fine for one hall,
   wrong for a world).
4. **World zones** (separate epic rpg-yrnu). The coarse tier above chunks: a world =
   many zones (a castle each); zones admit/evict by proximity + coarse visibility
   before their chunks stream; distant zones fully evict. Each zone owns its chunk
   set + geometry + colliders + far-field handoff at borders.

Chunk streaming (fr_asset_stream + dual prepass + gi_runtime external residency) is
done + default. Items 1–3 make the CHUNK tier scale; item 4 adds the ZONE tier.

## Reference implementations to lift from

- `tests/visual/hall_lit_dynamic.c` — `sh_stream` (lightmap chunk paging, atlas +
  ZLM1 manifest + per-mesh `sh_layer`) and the full `render_world` frame; ad-hoc
  LRU + all-RAM caching (what the streamer replaces).
- `src/renderer/gi/gi_sdf_stream.c` — SDF chunk load + 3D-texture upload + visibility
  paging (also all-RAM today).
- `include/ferrum/renderer/gi/gi_vis_prepass.h` — the MESH/WORLD chunk-id prepass.
- `include/ferrum/asset/asset_stream.h`, `chunk_table.h` — the streamer + chunk table.
- `src/renderer/resource/resource_loader.c` — the decode-on-fiber → `GPU_CMD_*`
  upload pattern.
