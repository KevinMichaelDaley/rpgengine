---
id: rpg-1gj9
status: in_progress
deps: []
links: [rpg-w1qe, rpg-fg05, rpg-9rzj]
created: 2026-07-13T03:12:49Z
type: feature
priority: 2
assignee: KMD
---
# Offline radiosity lightmap baker (SVO + kd-tree)

CPU offline radiosity/lightmap baker for static geometry. Luxels carry position, normal, diffuse albedo, emissive. Direct lighting from area lights + static emissive materials; indirect (bounced) lighting from point/directional/spot lights. Visibility + distant-reflector queries via the constructed SVO; kd-tree (or similar) for luxel/patch spatial acceleration. Varying lightmap resolution. Diffuse-only for now. Not perf-critical (offline, CPU). Follow project TDD + extreme modularity.

