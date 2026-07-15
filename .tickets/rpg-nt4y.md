---
id: rpg-nt4y
status: open
deps: [rpg-w1qe, rpg-zket, rpg-2ejn]
links: []
created: 2026-07-13T06:41:00Z
type: epic
priority: 2
assignee: KMD
---
# Cone-step parallax occlusion mapping (shallow parallax, full-depth self-shadow)

Cone-step-mapping raymarched parallax occlusion mapping for the PBR material. Key design catch: keep the PARALLAX DISPLACEMENT artificially SHALLOW (to minimise silhouette stretch/texture-swim at grazing angles) while using the FULL height range for the SELF-SHADOW raymarch (so shadows still read deep). Preprocess accepts a heightmap and bakes an RGBA cone/SDF map: R = depth (height), G = cone angle (cone-step acceleration), B = signed distance (relaxed/sphere-trace refinement), A = max step count / surface-complexity hint (per-texel adaptive iteration cap).

## Design

Core renderer only; nothing in demo_client. Offline heightmap->cone/SDF-map generator (like the existing bakes). Shader: tangent-space view-ray cone-step march (accelerated by G cone angle + B SDF, capped by A) to a parallax-displaced UV using a SHALLOW displacement scale; a SEPARATE self-shadow march toward the light uses the FULL depth. Displaced UV drives all material maps (albedo/normal/rough/etc.); the shadow term modulates direct + punctual lighting. Decoupling display-depth from shadow-depth is the crux. TDD + extreme modularity.

## Acceptance Criteria

A heightmap converts to the depth+cone+SDF+complexity RGBA map; the PBR shader shows convincing parallax relief with minimal stretch/swim at grazing angles (shallow displacement) yet deep, correct self-shadows (full-depth march). Cone-step + SDF keeps step counts low. Entirely in core renderer. Visual test demonstrates shallow-parallax/deep-shadow on a real heightmap. -Wpedantic clean.

