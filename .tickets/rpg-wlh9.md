---
id: rpg-wlh9
status: open
deps: []
links: []
created: 2026-07-24T19:08:14Z
type: feature
priority: 1
assignee: KMD
---
# Reflection probes: room-scale density + streamed per-chunk atlas residency


25 probes for a 2 km scene is absurd -- reflections need ~room-scale
density (dozens per interior), and at that count they MUST be streamed
assets, not a whole-level sidecar.

## Bake
- Placement per SDF chunk: grid at refl_spacing (default ~3 m) with the
  clearance + near-geometry rules, capped per chunk; probes assigned to
  the chunk containing them.
- Artifact: <prefix>_cNNN.rprobe per chunk (same RFP payload, chunk-local
  probe list), baked chunk-by-chunk in the --bake-probes pass alongside
  .probesh. Whole-level .rprobe goes away.

## Runtime (streamed residency, mirrors the SDF/probesh pattern)
- gi_sdf_stream loads <prefix>_cNNN.rprobe on chunk page-in, frees on
  evict (same fiber as the .sdf/.probesh loads).
- refl_gpu becomes a SLOT POOL: a fixed atlas of N tile slots (knob,
  default 512 x 64 px + mips + depth tiles ~ 34 MB). Chunk in -> its
  probes take free slots (glTexSubImage2D per mip); chunk out -> slots
  freed. Meta TBO rebuilt on residency change.
- Fragment lookup cannot loop 1000+ probes: build a coarse world-grid
  index (e.g. 8 m cells, up to 4 probe ids per cell, TBO) on residency
  change; the shader hashes the fragment cell and tests only those, SG
  fill-in unchanged.

## Depends
- The Phase-A bake-correctness fix (faces currently identical/garbage --
  full-pipeline face render debug in progress, REFL_DUMP).

Related: [[rpg-akwc]] (base feature), [[rpg-th87]].

## Direction update (KMD, 2026-07-24)
- Reflection probes MUST stream through the SAME visibility-gated
  chunking system as the SDF/probesh -- they are simply another per-chunk
  probe grid. No whole-level sidecar, no live-client bake hook.
- Cubemap capture moves into the CLIENT'S BAKE MODE (--bake): render the
  lm mesh set directly (self-contained GL, no live-pipeline state
  hazards -- the live-path attempt burned a day on viewport/clear/stale
  state bugs and still showed nothing on glass).
- Grid must be DENSE (~2-3 m). Good results are impossible with a
  sparse set.
