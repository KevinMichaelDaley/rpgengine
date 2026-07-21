---
id: rpg-zygg
status: open
deps: [rpg-oda7, rpg-sazm]
links: [rpg-gky0]
created: 2026-07-19T22:25:58Z
type: task
priority: 1
assignee: KMD
parent: rpg-hjck
tags: [gi, probes, streaming, client]
---
# Fix probe GI in the streamed client (probes as streamed assets)

The dynamic SDF-probe GI does not light the interior in the client yet (baked lightmap works; probe indirect does not). Probes are STREAMED ASSETS (FR_ASSET_PROBE) like the lightmap SH + SDF chunks -- load/generate per chunk/zone via fr_asset_stream gated by the visibility prepass, NOT all-at-once. Investigate why gi_runtime's probe irradiance is absent/black in the client path (probe placement from the descriptor spec, SDF residency it traces against, DYNAMIC_INDIRECT light gather incl the new dynamic lights, static irradiance volume folding the baked lightmap into probes). Route probe residency through the light-data streamer (rpg-oda7) so probes page in by visibility/zone.

## Design

See ref/gi_streaming_design.md (probes per chunk/zone). Compare the working hall_lit_dynamic GI config (build_static_irr_volume, has_sky_ao, static weights, spec gain, probe grid) vs client_scene_load's GI config -- the client omits the static irradiance volume + several GI terms. Probes register with fr_asset_stream as FR_ASSET_PROBE; the streamer + chunk_table drive residency.

## Acceptance Criteria

Interior of the streamed great_hall is lit by probe GI (indirect bounce + the dynamic lantern/sconce indirect); probes stream in by visibility/zone, not all at once; matches hall_lit_dynamic.


## Notes

**2026-07-19T22:55:24Z**

ORDER: this comes before rpg-da8c (JSON render config). Two parts: (1) make the probe GI actually light the client (parity with hall_lit_dynamic -- the client's GI config is missing the static irradiance volume [build_static_irr_volume, folds the baked lightmap into the probes] + sky AO, and the dynamic-light indirect); (2) probes become STREAMED/GENERATED assets (FR_ASSET_PROBE) paged per chunk/zone via fr_asset_stream, gated by the visibility prepass -- generated from a resolution by default, with optional manual probes + importance volumes (rpg-ft0g spec already parsed into the descriptor).

**2026-07-19T22:56:56Z**

INVESTIGATION (precise): the client's probe GI runs (gi_runtime enabled, probe grid placed) but produces near-nothing because client_scene_load's GI config OMITS the STATIC IRRADIANCE VOLUME that hall_lit_dynamic builds (build_static_irr_volume, hall_lit_dynamic.c:430-515) -- it reads .flm SH layers 0-3, samples each mesh vertex's atlas irradiance (SH0..3 . normal), splats to a 3D grid over the AABB, dilates, and uploads (gi_static_volume_upload) -> rwcfg.has_static_volume/static_vol_tex/origin/dim/voxel/static_k. Without it the probes carry no baked ambience, only the (dim, overpowered) dynamic-light SDF trace. Also missing: has_sky_ao (sky-openness AO). PART 1 fix: port build_static_irr_volume into the client -- needs per-mesh WORLD positions+normals+atlas-uv1 (reuse the client_bake fvma-CPU+TRS loader) + the .flm path (already known); set the has_static_volume/sky_ao rwcfg fields. PART 2: probes become FR_ASSET_PROBE streamed assets (generate-by-resolution default + manual/importance from the descriptor spec) paged per chunk/zone via fr_asset_stream + the visibility prepass. NOTE: this belongs in the JSON render config too (rpg-da8c) so the volume/AO/probe params aren't hardcoded.

**2026-07-19T23:09:45Z**

PART 1 DONE (committed b140d79d): probe GI now lights the client interior via the static-irradiance volume (client_static_volume_build folds the baked SH lightmap into a 3D volume the probes gather) + sky AO. Dynamic lights placed AT the architecture (fireplace opening + arched lamp niches), shadow-casting, energies raised to the engine radiance scale (~400-600) so they read like the demo -- the 1:1 Blender-energy->intensity export was ~100x too dim. User confirmed 'looks right'. Also fixed: directional sun was polluting the punctual cluster pack (now excluded). REMAINING (pt.2): probes as STREAMED FR_ASSET_PROBE assets, generated per chunk/zone + paged by the visibility prepass (currently a one-shot 1024-probe grid loaded up front).

**2026-07-19T23:13:28Z**

MERGED SCOPE (user): do this together with rpg-sazm (dual-index vis prepass) + rpg-c7fk (external SDF residency) -- 'get ALL the gi stuff going through the right pipeline'. Target: client_light_stream owns residency for lightmap SH chunks (done) + SDF/voxel chunks + probes, all via fr_asset_stream; ONE dual-index visibility prepass emits SDF-chunk-id + lightmap-chunk-id to separate channels; gi_runtime CONSUMES the streamer-resident SDF chunk set + probes instead of self-loading gi_sdf_stream. Approach: committable increments that don't break the currently-working GI.

**2026-07-19T23:18:42Z**

IMPLEMENTATION PLAN (external SDF residency + probe streaming, all additive so the working GI is preserved via fallback):
INCR 2a - gi_runtime takes an OPTIONAL external gi_sdf_stream_t* (cfg.ext_sdf). Internally use gi->sdf_ptr (=ext_sdf or &gi->sdf); skip self gi_sdf_stream_load when external; destroy only if self-owned. All gi->sdf sites (init boxes L81, sample L101, page L257, dispatch L289) -> gi->sdf_ptr. If ext NULL, unchanged (safe).
INCR 2b - client_light_stream OWNS the gi_sdf_stream + registers each <sdf_prefix>_cNNN.sdf as FR_ASSET_SDF_CHUNK in fr_asset_stream: load cb (fiber) = lm_sdf_load into the chunk RAM slot; upload cb (render thread) = the existing 3D-texture upload; residency gated by the dual prepass visible_sdf. gi_runtime consumes it via ext_sdf. Removes the load-all-to-RAM.
INCR 2c - swap gi_runtime's internal single-index world prepass for the shared DUAL prepass (gi_vis_prepass_run_dual) so ONE pass drives BOTH the SDF page set (gi_runtime) and the lightmap page set (client_light_stream).
INCR 3 - probes as FR_ASSET_PROBE: generate-by-resolution per chunk/zone (default) + manual/importance from the descriptor; gi_runtime consumes an external probe set paged by visibility. 
NOTE: great_hall's SDF is already GPU-paged (8 resident slots by visibility) + RAM-cached; the fr_asset_stream routing is the RAM-budget/zone scalability win for huge levels, not a correctness fix -- the GI is visually correct now.

**2026-07-19T23:25:58Z**

DONE THIS ROUND (committed): dual-index vis prepass built (747f4b83, rpg-sazm) -- one pass, SDF id + lightmap id to separate bit-fields, two visible sets. gi_runtime accepts an external SDF stream (434e2c6e). Light-data streamer now OWNS the SDF stream + lightmap chunks; gi_runtime borrows the SDF via ext_sdf (fa9fa710) -- SDF loaded ONCE by the streamer, GI visually unchanged (fireplace+niches+bounce+specular verified). ARCHITECTURE now: all GI residency lives in client_light_stream; gi_runtime consumes external SDF; dual prepass ready to wire.
REMAINING (scoped, scalability/consolidation -- GI is visually correct now): (i) register SDF chunks as FR_ASSET_SDF_CHUNK for fr_asset_stream RAM-budget on-demand residency (refactor gi_sdf_stream disk->RAM to per-chunk load cb; it already GPU-pages by visibility); (ii) INCR 2c: feed gi_runtime an external visible-SDF mask + run the shared dual prepass in client_light_stream so ONE pass pages both SDF + lightmap (retire gi_runtime's internal world prepass); (iii) probes as FR_ASSET_PROBE generated per chunk/zone.

**2026-07-19T23:30:21Z**

PROBE REQUIREMENT (user, restated): probes are BOTH -- (a) GENERATED from a probe-volume spec in the scene (spacing/resolution + box importance density overrides, the scene_desc_probes spec / rpg-ft0g), AND (b) MANUAL probes STREAMED via fr_asset_stream as FR_ASSET_PROBE (loaded per chunk/zone from .probes). The descriptor's probes section already carries spacing + manual(.probes) + importance boxes. So the probe pipeline must: generate a grid from spacing (importance boxes raise density in their AABB) when no manual set, AND page manual probe sets as streamed assets when present -- both feeding gi_runtime's probe set, gated by visibility/zone. (Current client GENERATES from spacing via probe_place_grid + passes as manual to gi_runtime -- works but not streamed.)

**2026-07-19T23:53:21Z**

MILESTONE: the full GI data set now streams through fr_asset_stream. This round: gi_runtime_set_probes dynamic-probe refactor (15139041); client honors probe-volume importance boxes (987c770a); probes stream to resident SDF chunks via render_world_set_probes -> gi_runtime_set_probes, gated by gi_sdf_stream_resident_boxes (a781844c). Combined with the SDF streaming (scan + per-chunk load/evict + fr_asset_stream, 444f72d1/2b53e7fa) + gi_runtime external SDF (434e2c6e/fa9fa710) + dual-index prepass (747f4b83): lightmap SH chunks + SDF/voxel chunks + probes ALL page through fr_asset_stream, gated by residency, gi_runtime consuming external SDF + dynamic probe sets. Verified SDF_STREAM=1 PROBE_STREAM=1 renders great_hall correctly. Env-gated (default = static all-resident) so the working path is preserved. REMAINING refinements: drive residency by the dual-prepass VISIBILITY (not just RAM residency) + retire gi_runtime's internal world prepass; zones (coarse residency above chunks); manual .probes streaming (needs data); unify the lightmap+SDF+probe budgets; chunked lightmaps (currently 1 atlas).

**2026-07-20T00:00:45Z**

STRUCTURAL COMPLETENESS (committed c2fa12de): ONE shared dual visibility prepass now drives ALL GI chunk residency. gi_runtime accepts an external visible-SDF mask (gi_runtime_set_visible) and, when set, pages the SDF from it instead of running its own world prepass; client_scene owns the dual prepass (client_scene_gi_visibility) which feeds render_world_set_visible + gates probes to the VISIBLE SDF boxes. VIS_STREAM path in demo_client. So: lightmap SH chunks + SDF/voxel chunks + probes all page through fr_asset_stream, driven by ONE on-screen visibility prepass, gi_runtime consuming external SDF + dynamic probes + external visibility. Verified VIS_STREAM=1 SDF_STREAM=1 renders great_hall correctly. Defaults unchanged (flags off = internal prepass / static). REMAINING (polish, not structural): multi-chunk lightmaps consume the visible_lm channel (currently 1 atlas -> unused); zones (coarse residency above chunks); unify lightmap+SDF+probe budgets into one fr_asset_stream; manual .probes streaming; make the streamed paths the DEFAULT (retire env flags) once validated.

**2026-07-20T00:08:18Z**

SCOPE: world/zone streaming is NOT part of this ticket -- it is its own CORE epic (rpg-yrnu). rpg-zygg = per-CHUNK GI streaming (done). Zones = the coarse two-level tier above chunks, tracked separately.

**2026-07-20T00:11:27Z**

REFRAME (user): the 'remaining' items are CORE to the streaming system, NOT polish -- required to scale to MASSIVE levels (open zones/worlds with MULTIPLE castles, each with its own great-hall-like interior). Specifically CORE: (1) MULTI-CHUNK LIGHTMAPS -- the lightmap is currently ONE atlas for the whole level (109MB for one hall); a world of castles needs the lightmap CHUNKED + streamed by visibility (the visible_lm dual-prepass channel is built but unused because there's only 1 chunk). Requires the baker to emit per-chunk atlases (rpg-yfa4) + a ZLM1 chunk manifest (rpg-jro2) + the client to stream N lightmap chunks (per-mesh sh_layer from the resident chunk, like hall_lit_dynamic's sh_stream but via fr_asset_stream, gated by visible_lm). (2) UNIFIED BUDGET -- one fr_asset_stream RAM/VRAM budget across lightmap+SDF+probes so residency is bounded regardless of world size. These + world zones (rpg-yrnu) are the path to massive worlds.
