---
id: rpg-fo9r
status: open
deps: []
links: []
created: 2026-07-16T06:45:33Z
type: epic
priority: 1
assignee: KMD
---
# Dynamic-light GI via SDF cone-traced irradiance probes

Dynamic lights (point/spot/directional) contribute INDIRECT lighting (GI) at runtime by cone-tracing the scene SDF from a grid of irradiance probes; the forward+ material samples the nearest probes' SH. Static GI stays the baked SH lightmap -- this adds the DYNAMIC-light indirect term only, so moving lights get soft one-bounce bounce/AO without a rebake.

## Design

Pipeline per frame (or throttled):
1. COMBINED SDF: start from the baked scene SDF (the per-chunk 128^3 .sdf fields from rpg-iudw, sampled/streamed as a 3D texture or grid) and MIN-COMBINE it with cheap ANALYTIC SDFs of most dynamic colliders (sphere/box/capsule from physics) so moving bodies occlude/shadow the dynamic GI. Min of signed distances = union of solids. Keep a bound on how many dynamic colliders fold in (nearest/largest).
2. IRRADIANCE PROBES: a coarse grid (or placed set) of probes over the play space, each holding SH9 (per-colour) irradiance. Storage = probe buffer (SSBO / texture3D array of SH coeffs).
3. PROBE UPDATE KERNEL (parallelize OVER PROBES -- compute shader if GL4.3, else job-system CPU kernel): for each probe, cone-trace through the combined SDF toward each relevant DYNAMIC light (point/spot/directional), computing visibility (soft shadow/AO from the cone occlusion) * light radiance * falloff, and accumulate the incident radiance into the probe's SH9. Directional = one cone along -dir; point/spot = cone toward the light with range/cone-angle falloff. One thread per probe (or probe x light tile).
4. FORWARD+ PROBE SAMPLER: in the pbr/forward+ material shader, find the probe cell the fragment is in, TRILINEARLY interpolate the 8 corner probes' SH9, evaluate against the surface normal, and add as a dynamic indirect term ON TOP of the baked static lightmap + direct dynamic lights.

Parallelism: the kernel is embarrassingly parallel over probes; design the probe buffer + light buffer so a compute dispatch (or a parallel_for job over probe indices) fills it. Throttle/stagger updates (e.g. round-robin a fraction of probes per frame) if needed.

## Acceptance Criteria

Moving dynamic lights cast a soft, bounced indirect glow + AO that updates every frame with no rebake, visible on nearby static geometry; the term is sampled from interpolated probe SH in the forward+ shader and combines correctly with the baked static lightmap and direct lighting. Kernel parallelizes over probes. Demo: a swarm of tiny moving particle point-lights traveling around the ceiling of the hall lights the vaults/walls below with a soft moving indirect wash + contact darkening from the combined dynamic-collider SDF. Clean under -Wpedantic; TDD for the host-side probe/SDF/buffer logic (the trace kernel validated by parity/visual).


## Notes

**2026-07-16T06:46:40Z**

DESIGN CORRECTION (supersedes the 'grid' wording in the design): probes are NOT on a uniform grid -- they are ADAPTIVE with explicitly STORED POSITIONS. 
- Probe storage = an array of { vec3 position, SH9 } (placement adaptive: concentrated near surfaces/lights/play space; how they're seeded is its own sub-task).
- Probe update kernel still parallelizes over the probe ARRAY (one thread per stored probe), cone-tracing the combined SDF to the dynamic lights -> that probe's SH9.
- Forward+ sampler finds the NEAREST probes to the fragment (not trilinear grid corners) and blends their SH by distance weight (e.g. inverse-distance / k-nearest). Needs a spatial lookup: bin the adaptive probes into a coarse uniform ACCELERATION grid (cell -> probe index list) so the shader gathers candidates from the fragment's cell + neighbours and distance-weights them. The accel grid is just for lookup; probe positions stay adaptive.
