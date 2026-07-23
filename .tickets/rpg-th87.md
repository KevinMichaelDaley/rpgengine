---
id: rpg-th87
status: open
deps: []
links: []
created: 2026-07-23T21:47:35Z
type: bug
priority: 1
assignee: KMD
---
# gh_dyn: probe GI contributes zero at shading despite 53917 resident probes (indexing/paging)


RESOLVED-WORKAROUND + remaining bug (was: 'probe GI zero at shading').
CONFIRMED to have hit BOTH scenes: gh_dyn (53,917 probes) AND la_sprawl
(249,358 probes) -- every probesh chunk in both was all-zero from
BAKE_ITERS=12 bakes. Both re-baked clean with rm-first + BAKE_ITERS=4
(la c000 now 17,978 nonzero SH floats, max 0.189; hall 34,646, max 0.043).
la_sprawl's viewer MASKED the zeros via gi_freeze_ticks=200 runtime
re-lighting, so visual checks don't prove the bake worked.

Two traps:
1. STALE-PROBESH GATING (workflow): --bake-probes with leftover _cNNN.probesh
   streams them and stops dispatching updates (bake-and-freeze gate), then
   writes the never-updated zeros back out. Consider: --bake-probes should
   ignore/delete existing probesh itself.
2. BAKE_ITERS ZEROING (open bug): at 53k+ probes gi_runtime_bake_converge
   produces ALL-ZERO SH (exact 0.0) with BAKE_ITERS=12 but VALID SH with 4;
   1,700 probes survive 12. Suspect the recurrent field feedback in
   gi_probe_gpu_dispatch (cbrick neighbor gather / two-pass field path)
   collapsing between iterations 5..12 at high counts.
   Repro: rm probesh; BAKE_ITERS=12 vs 4 on gh_dyn @53,917; parse _c000
   (PSHC: u32 count @4, idx[n] @8, sh[n*24] f32 after).

Diagnosis trail: dynvox full-scan diagnostic (client_scene_dynamic.c, now
scans the whole set -- the old 4096-probe window generated the 'no probes
near dyn[0]' red herring); PBR_DEBUG=7 raw irradiance view; direct PRB1 /
PSHC file parsing.

Current good state: hall 53,917 probes (6m + 2m/L3 shells; L3==L4 count,
saturated -- L4 .bricks also under suspicion), viewer luma 7.62 -> 12.87
with the staged GI gains (static_dyn_w 8, static_baked_w 3, gi_stat_scale
1.5, gi_dyn_gain 24); la_sprawl 249,358 probes with a real baked warm
start. See memory: probesh-bake-gotchas.
