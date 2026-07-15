---
id: rpg-7efr
status: open
deps: []
links: [rpg-1gj9]
created: 2026-07-15T03:13:32Z
type: task
priority: 3
assignee: KMD
tags: [deferred, lightmap]
---
# Make offline lightmap bake FP-portable across machines

DEFERRED. The offline lightmap gather is a chaotic path tracer and is NOT
bit-reproducible across machines: the identical binary (md5-verified) with
identical inputs (dmeshes, textures, seed) produces different bakes on the local
box vs chimera, and on chimera the divergence manifests as WRONG output
(black/garbage vault shells) while local is clean.

## Diagnosis (already done -- do not repeat)
- NOT a data race: local 1t==2t and chimera 1t==56t are byte-identical (the
  gather writes only per-luxel accumulator slots).
- NOT the binary/inputs: md5 of binary + dmeshes + textures match both machines.
- NOT UB: valgrind memcheck --track-origins reports 0 uninitialised reads.
- Same glibc VERSION (2.39) on both machines.
- ROOT CAUSE: glibc libm IFUNC dispatch selects a CPU-microarchitecture-specific
  sinf/cosf/powf at load time, so the same machine code yields ULP-different trig
  on different CPUs. The chaotic path tracer amplifies a sub-ULP ray-direction
  difference into a flipped grazing hit/miss at the thin curved vault shells ->
  self-occlusion flips -> structurally different / wrong bake.

## Already shipped (commit 5cc44aa1, partial)
- Machine-independent polynomial sin/cos in the ray generation (lm_gi_gather.c).
- Self-hit tmin in lm_visibility_trace so a ray skips its own surface within one
  voxel. Made the LOCAL bake clean and robust, but local vs chimera STILL differ.

## Remaining work
- Make the remaining libm calls in the bake hot path deterministic: powf in the
  lm_image sRGB->linear decode (voxel material) and any sqrtf that falls back to
  the libm errno path (force sqrtss). Consider a portable powf or a precomputed
  sRGB LUT.
- OR statically link libm into the bake tool so one implementation runs
  everywhere.
- Verify: bake the hall on local (1 core) and chimera, cmp the .flm -> must be
  byte-identical, and both render clean. Then chimera (56 cores) is usable again
  for fast production bakes.

See memory: reference_lightmap_bake_fp_nonportable, feedback_lightmap_bake_workflow.
