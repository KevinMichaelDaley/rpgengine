---
id: rust-rpg-o4i
status: closed
deps: [rust-rpg-aqq]
links: []
created: 2026-01-17T18:02:49.235276433-08:00
type: epic
priority: 2
---
# P_004 — Renderer Core (OpenGL Wrappers)

## P_004 — Renderer Core (OpenGL Wrappers)

### Design Intent
Wrap unsafe OpenGL resource lifetimes into explicit, minimal C APIs with clear ownership, enabling predictable cleanup and testability.

This epic intentionally uses **real SDL2 window creation** and a **real OpenGL context** (system OpenGL + GLEW) in tests. We do **not** mock OpenGL calls or other system calls; tests validate behavior by observing real GL state (e.g., current program, uniform values, UBO bindings).

### Specification
- GL loader: function pointer table.
- `shader_program_t`:
  - compile vertex+fragment, link, bind
  - uniform setters: mat4, vec3, int, float
- `vbo_t`, `vao_t`:
  - create/destroy
  - upload data (size + pointer)
  - attribute layout binding

#### Skeletal Animation & GPU Skinning
#### Render Pipeline Graph (Stages & Passes)
- Define a minimal render pipeline graph: nodes for passes (skybox, depth pre-pass optional, main forward pass, post chain entry) with explicit dependencies.
- Stage interfaces: `begin_pass`, resource bindings, draw submission, `end_pass`.
- Resource views: define attachments (color/depth) and transient buffers; avoid hard-coding effects.
- This enables implementing advanced effects elsewhere without changing core wrappers.
- Bone palette buffers: UBO/SSBO/TBO depending on capability.
- Skinning shader: vertex shader applies weighted bone transforms to mesh vertices.
- ECS integration: per-entity `skeleton`/`skin` components refer to bone transforms.
- Job system: CPU-side evaluation jobs compute bone matrices from animation clips and write to per-entity palettes.

### Implementation Steps
8. Define render pipeline graph structs and pass interfaces; implement pass execution order.
9. Provide default pipeline: skybox → forward main → post chain stub.
1. Define GL loader interface (platform provides proc addresses).
2. Implement shader compilation with error logs.
3. Implement program linking and uniform lookup caching.
4. Implement buffers and vertex array setup.
5. Implement skinning shader program(s); define attribute layouts for weights/indices.
6. Implement bone palette buffer creation/update (UBO/SSBO/TBO) and binding per draw.
7. Integrate ECS/job pipeline to evaluate animation clips, produce bone matrices, and stage GPU uploads.

### Architectural Considerations
- Pass ordering deterministic; graph avoids global mutable state.
- Keep pipeline generic; no specific post effects here—only plumbing.
- No global mutable GL state in wrappers; require explicit bind.
- Error handling: return status + log buffer.
- Unit tests use **real** SDL2 + OpenGL + GLEW contexts and validate behavior via real GL state; no mocking.
- Skinning must avoid per-frame mallocs; prefer persistent buffers.
- Capability gating for buffer types; fallback path documented.
- Deterministic CPU evaluation via jobs; GPU upload ordering stable in tests.

### Unit Tests (RED-first)
Tests create a real SDL2 window + GL context and use GLEW; assertions use real GL queries (e.g., `glGetIntegerv`, `glGetUniform*`, `glGetIntegeri_v`).

**Happy Path**
1. **Shader compile + link success path**
   - compile and link succeed; verify program handle stored and bind affects real GL state.
2. **Uniform upload calls correct GL functions**
   - verify uniforms by reading back via `glGetUniform*`.
3. **VBO/VAO create/destroy pairs**
   - create then destroy must delete exactly once per resource (validated by wrapper state + GL handle invalidation patterns).
4. **Attribute layout binding**
   - configure VAO; verify attribute enable/format state via `glGetVertexAttrib*`.
5. **Bone palette upload/bind**
   - verify buffer update and UBO/SSBO binding points via `glGetIntegeri_v`.
6. **Skinning shader uniform setup**
   - verify palette bound prior to draw and shader uses documented attribute semantics.

**Edge Cases**
5. **Uniform location caching**
   - first set queries location; subsequent sets reuse cached location.
6. **Zero-size uploads**
   - uploading 0 bytes must be a no-op or explicit error (document), but must not crash.
7. **Double destroy is safe**
   - destroying an already-destroyed wrapper is a no-op or explicit error; must not delete twice.
8. **Palette size limits**
   - exceeding max bones per draw returns explicit error or splits draw; document behavior.
9. **Fallback buffer path**
   - when SSBO unsupported, UBO/TBO path engages; tests verify capability gating.

**Failure Modes**
8. **Shader compile failure path**
   - compile fails; verify error returned and log captured.
9. **Program link failure path**
   - compile succeeds but link fails; verify error returned and log captured.
10. **Missing GL function pointers**
   - if required GL entry points are NULL, wrapper init/create must return explicit failure.
11. **Skinning shader link failure**
   - skinning program link fails; error captured and renderer falls back or reports failure explicitly.

### Regression Tests (RED-first)
1. **Error log truncation**
   - very long compiler log must be safely truncated and NUL-terminated.
2. **Uniform type mismatch guard**
   - if wrapper tracks uniform types, setting wrong type returns error (otherwise document no checking).
3. **No implicit global binds**
   - wrapper must not assume any prior GL state; tests start from “unknown state” and verify required binds happen.
4. **Bone palette index mapping stability**
   - mapping from bone indices to buffer offsets remains stable across runs.
5. **Attribute semantics lock-in**
   - vertex attribute locations/types for weights/indices documented and tested.

### Cumulative Integration Tests (RED-first, cumulative through P_004)
1. **Render command generation loop (P_000..P_004)**
   - create a small ECS world with transform components; build view/projection matrices; dispatch jobs that generate draw submission.
   - verify deterministic behavior via observable outputs (not mocked GL call tables).
2. **GPU skinning path (P_000..P_004)**
   - create an entity with skeleton/skin; job evaluates bone matrices; renderer uploads palette and issues draw; verify correct GL state and buffer bindings.
3. **Pipeline execution ordering**
   - execute default pipeline (skybox → forward → post stub); verify pass begin/submit/end ordering.

---



