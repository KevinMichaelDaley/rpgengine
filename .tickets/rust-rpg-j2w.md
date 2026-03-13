---
id: rust-rpg-j2w
status: closed
deps: [rust-rpg-55s]
links: []
created: 2026-01-17T18:04:54.628033178-08:00
type: epic
priority: 2
---
# P_012 — Scene Management & Asset Streaming (GLTF, Lightmaps, Skybox)

## P_012 — Scene Management & Asset Streaming (GLTF, Lightmaps, Skybox)

### Design Intent
Provide deterministic scene graph management and robust asset streaming, integrating static lightmaps with dynamic clustered lighting, glTF 2.0 import, lightmap UV (UV2) generation and baking, support for lightmap volumes/zones, and skybox rendering. Emphasize AZDO-friendly GPU paths and minimal stalls.

### Specification
#### Scene Graph & Entities
- Scene nodes reference ECS entities (transforms, renderables, lights).
- Hierarchical transforms with baked world matrices; avoid per-frame allocations.

#### Asset Streaming
- Background IO thread reads and decompresses assets (glTF, textures) into CPU buffers.
- Staging via PBOs/persistently mapped buffers; render thread issues uploads.
- Streaming priorities: near/visible assets first; LOD-aware.

#### World Streaming & Seamless Scene Loading
- World partitioned into streaming regions/chunks with dependency metadata (references, required assets).
- Seamless transitions: prefetch adjacent regions based on player velocity/heading; unload far regions gradually.
- Cross-scene references: handle entities crossing boundaries; preserve persistent IDs and state.
- Networking integration: interest management aligns with streamed regions; clients receive only in-range entities.

#### glTF 2.0 Import
- Support meshes, materials (mapped to simplified surface shader), skeletons/skins.
- Texture formats: KTX2/PNG; generate mipmaps; sRGB handling documented.
- Import node transforms and attach to ECS components.

   Material mapping to simplified surface shader (P_016):
   - Base color → `base_color`.
   - Metallic-Roughness inputs → derive `roughness_like` and `spec_like` scalars using stable rules (e.g., `roughness_like = roughness`, `spec_like = 1 - metallic`).
   - Normal and emissive textures → map to `normal_map` and `emissive`.
   - Missing inputs → deterministic defaults; mapping is lossy but consistent and documented.

#### Animation Clips & Evaluation
- Import animation clips (channels: translation/rotation/scale) and retarget to skeletons.
- Clip storage streamed lazily; per-clip metadata includes duration, sampling rate.
- CPU-side evaluation jobs read clip keyframes, evaluate at time t (quaternion SLERP, vector LERP), produce per-bone matrices.
- Outputs staged into renderer’s bone palette buffers (see P_004 skinning).

#### Lightmaps: Baking & UV2
- Generate secondary UV (UV2) for lightmap baking; seams minimized; chart packing.
- Baking pipeline (external/offline or in-tool): produces lightmap textures + lightmap index per mesh.
- Materials support additive static lightmap sampling combined with dynamic cluster lights.

#### Lightmap Volumes & Zones
- Define volumes/zones in scene with baked ambient or probes (SH coefficients) for interiors/exteriors.
- Zone transitions: blend probes/lightmap contributions smoothly.

#### Dynamic Lighting Integration
- Clustered forward shading integrates dynamic lights with static lightmaps.
- Per-cluster light lists; materials combine static + dynamic contributions.

#### Skybox Support
- Load cubemaps (KTX2 or 6 images) or equirectangular sky textures; prefilter if needed.
- Skybox rendered first/last depending on pipeline; optional reflection probes.

### Implementation Steps
1. Scene graph data structures and ECS binding.
2. Asset IO thread + staging buffers (PBO/persistent maps) and upload scheduling.
3. glTF importer: mesh/material/skin parsing; texture loading; ECS component creation.
4. UV2 generation for meshes (atlas/pack charts); metadata stored with mesh.
5. Lightmap sampling integrated into material shader; zone/volume data bound to GPU.
6. Clustered lighting combined with lightmap in forward pass.
7. Animation clip import and storage; job scheduling for evaluation; palette staging.
8. Skybox loader and renderer; optional IBL prefilter.
9. Instrumentation: bytes streamed/frame, upload latency, GPU stalls counter; animation evaluation time; region load/unload timings.

### Architectural Considerations
- No per-frame mallocs in render path; streaming uses fixed buffers.
- Capability-based features (bindless textures, persistent maps) gated by checks.
- sRGB/linear handling documented to avoid banding.
- Deterministic scene updates for tests; avoid nondeterministic hashes.

### Unit Tests (RED-first)
**Happy Path**
1. **glTF mesh/material import**
   - parsing produces expected ECS components; materials map to shader parameters.
2. **UV2 generation validity**
   - generated UV2 in [0,1]; charts packed without overlap beyond epsilon.
3. **Lightmap sampling**
   - material combines static lightmap and dynamic light correctly in mock shader (numeric invariants).
4. **Skybox load/render setup**
   - cubemap loads; render commands issued in correct order.
5. **Streaming upload scheduling**
   - IO thread stages asset; render thread uploads without stalls; counters updated.
6. **Animation clip import + evaluation**
   - clip parsed; CPU job evaluates bones at time t; bone palette buffers receive correct matrices.
7. **Seamless region transition**
   - entering a boundary triggers prefetch; assets present before crossing; no visible pop; counters reflect expected timing.

**Edge Cases**
6. **Missing textures**
   - importer handles missing optional textures with defaults.
7. **Degenerate UVs**
   - UV2 generator handles tiny/degenerate charts without NaNs.
8. **Zone transitions**
   - probe blend clamps and transitions smoothly; no abrupt changes.
9. **Large scenes**
   - streaming prioritization respects near-visible assets; avoids starving distant assets completely.
10. **Missing animation channels**
   - clips with missing TRS channels use defaults; evaluation remains finite.
11. **Cross-scene entity handoff**
   - entity crossing region keeps persistent ID/state; duplicates not created; ownership transferred deterministically.

**Failure Modes**
10. **Malformed glTF**
   - invalid JSON/binary chunks rejected; importer returns explicit error.
11. **Upload failure**
   - missing GL entry points or buffer failures return error; renderer remains stable.
12. **Malformed animation clip**
   - invalid keyframe data rejected; evaluation returns explicit failure.
13. **Region load failure**
   - failed region load triggers retry/backoff; renderer avoids dereferencing missing resources.

### Regression Tests (RED-first)
1. **Material parameter mapping stability**
   - mapping from glTF material fields to shader uniforms remains consistent.
2. **UV2 packing determinism**
   - given fixed seed/settings, UV2 atlases identical across runs.
3. **Streaming counters**
   - bytes/frame and latency statistics remain within expected bounds under scripted loads.
4. **Animation evaluation determinism**
   - given fixed clips and timestamps, bone matrices are identical across runs.
5. **Region prefetch policy stability**
   - scripted movement yields identical region load/unload sequences.

### Cumulative Integration Tests (RED-first, cumulative through P_012)
1. **Scene load + render pipeline (P_000..P_012)**
   - import a small scene; stream assets; render with static lightmaps + dynamic clustered lights; verify deterministic command sequence via mocked GL.
2. **Zone lighting blend**
   - move camera across zone boundary; probe blend results follow expected curve.
3. **Skybox + IBL**
   - skybox renders; optional reflection probe affects materials deterministically.
4. **Animation + skinning end-to-end**
   - glTF skeleton + clip imported; job evaluates bones; renderer skins mesh; mocked GL sequence includes palette upload and draw; numeric invariants hold.
5. **World streaming + networking cohesion**
   - streamed regions align with networking interest sets; entities appear/disappear deterministically as regions load/unload.

---




## Notes

**2026-03-13T04:08:43Z**

Closed as exact duplicate (double import). Keeping the copy with downstream blockers wired.
