---
id: rust-rpg-vp2
status: closed
deps: [rust-rpg-1a1]
links: []
created: 2026-01-17T18:04:55.227076139-08:00
type: epic
priority: 2
---
# P_016 — Render Pipeline & Surface Shader (Simplified)

## P_016 — Render Pipeline & Surface Shader (Simplified)

### Design Intent
Provide a minimal, customizable surface shader and render pipeline plumbing (no PBR), enabling different looks via parameterized shaders without changing core renderer.

### Specification
- Surface shader parameters: base color, roughness-like factor, spec-like factor, emissive, normal map support.
- Pipeline stages: skybox, forward shading, optional post chain stub.
- Material system: lightweight parameter blocks; shader variants via defines.

### Implementation Steps
1. Define surface shader uniform/layout; compile-time variants via flags.
2. Bind material parameter blocks; texture sampling conventions.
3. Integrate with P_004 pipeline stages and scene materials.

### Architectural Considerations
- AZDO-friendly bindings; no global state.
- Deterministic parameter mapping.

### Unit/Regression/Cumulative Tests
- Shader param mapping stability; variant define behavior locked.
- Cumulative: render with skybox + forward pass using simplified shader.

---




## Notes

**2026-03-13T04:08:43Z**

Closed as exact duplicate (double import). Keeping the copy with downstream blockers wired.
