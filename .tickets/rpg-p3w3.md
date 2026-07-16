---
id: rpg-p3w3
status: open
deps: [rpg-qthg, rpg-d1ok]
links: []
created: 2026-07-16T06:47:06Z
type: task
priority: 1
assignee: KMD
parent: rpg-fo9r
---
# Probe update kernel: cone-trace combined SDF to dynamic lights -> SH9 (parallel over probes)

One thread per stored probe (compute shader if GL4.3 available, else job-system parallel_for). For each probe, cone-trace the combined dynamic SDF toward each relevant DYNAMIC light (point/spot/directional): visibility/AO from cone occlusion * radiance * falloff (range + spot cone), accumulate incident radiance into the probe SH9. Directional = one cone along -dir; point/spot = cone toward the light. Throttle/round-robin probes per frame if needed. Validate by parity/visual.

