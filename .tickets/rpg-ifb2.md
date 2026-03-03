---
id: rpg-ifb2
status: open
deps: [rpg-yjhf]
links: []
created: 2026-03-02T18:38:59Z
type: task
priority: 2
assignee: KMD
---
# Phase 4b: shader permutation system

Create the shader permutation system for compile-time variants via preprocessor defines. See ref/renderer_spec.md §5.2.

Deliverables:
- include/ferrum/renderer/material/shader_permutation.h: Permutation key type (bitfield of HAS_NORMAL_MAP, HAS_LIGHTMAP, HAS_VERTEX_COLOR, HAS_SKINNING, ALPHA_CLIP, etc.), permutation_cache_t
- src/renderer/material/shader_permutation.c: Build #define strings from permutation key, compile on demand, cache compiled programs
- Canonical vertex shader template with conditional blocks for each feature
- Canonical fragment shader template: simplified surface shader (base_color, roughness_like, spec_like, emissive, normal_map) per ref/architecture.md §5.0.1
- Pre-compile common permutations at startup (opaque-unlit, opaque-textured, opaque-normal-mapped, skinned-textured)
- Permutation cache with LRU eviction for rare combinations
- Tests in tests/p004_renderer_shader_permutation_tests.c

Depends on: rpg-yjhf (material system provides material-to-permutation mapping)

