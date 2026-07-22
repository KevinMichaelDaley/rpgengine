---
id: rpg-kbqd
status: open
deps: [rpg-29zj]
links: []
created: 2026-07-22T10:15:50Z
type: feature
priority: 1
assignee: KMD
---
# Renderer: light-space caustics compute (SDF-traced) for translucent shadows

Per KMD's architecture step 2: a GL4.3 compute pass (runtime-gated like gi_probe_gpu) consumes the CSM translucency mask (depth + color/alpha): for each translucent texel it reconstructs the surface position along the light ray, traces several jittered rays within a scattering radius (hash-jittered dirs) through the resident SDF chunks (scene_sdf march), and SPLATS transmitted energy (imageAtomicAdd, r32ui fixed point) into a light-space caustic map at the texel where each ray lands. The map integrates transmission: a receiver behind glass multiplies the sun term by caustic energy instead of flat tint*alpha -- focus patterns emerge where geometry converges rays. Static bake per cascade; energy conservation: map sum == sum of glass alpha regardless of scatter radius.

## Acceptance Criteria

Compute test: flat glass, scatter 0 -> caustic map equals flat alpha (tolerance); scatter > 0 -> total energy conserved. Visual: caustic projected in the forward pass, gated on the mask depth test.

