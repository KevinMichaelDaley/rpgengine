---
id: rpg-sukf
status: open
deps: [rpg-lvgz]
links: []
created: 2026-03-02T18:40:13Z
type: task
priority: 2
assignee: KMD
---
# Phase 6c: SH probes for global illumination

Implement L2 spherical harmonics irradiance probes on a 3D grid. See ref/renderer_spec.md §7.4.

Deliverables:
- SH probe grid: configurable spacing (4-8 meters), L2 SH (9 coefficients × 3 channels = 27 floats per probe)
- Probe storage as 3D texture (4 × RGBA32F texels per probe, 36 floats)
- Per-fragment: trilinear interpolation of 8 nearest probes
- Offline baking: ray-traced or rasterized to cubemap then SH projection
- Runtime update for dynamic GI is explicitly out of scope
- Integration with forward shader: SH ambient term added to direct lighting
- Tests in tests/p004_renderer_sh_probe_tests.c

Depends on: rpg-lvgz (shadow maps needed before GI probes can be evaluated correctly)

