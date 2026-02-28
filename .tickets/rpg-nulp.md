---
id: rpg-nulp
status: closed
deps: [rpg-ma1t]
links: []
created: 2026-02-28T22:21:00Z
type: task
priority: 2
assignee: KMD
parent: rpg-caw8
tags: [editor, mesh, serialization]
---
# VAO binary format (serialize/deserialize)

Implement the FVMA (Ferrum VMesh Asset) binary format for transferring mesh data from server to client.

Wire format:
  [4 bytes] magic: 'FVMA'
  [4 bytes] version: 1
  [4 bytes] vertex_count
  [4 bytes] index_count
  [4 bytes] flags (has_normals, has_tangents, has_uv0, has_uv1, has_colors)
  [4 bytes] polygroup_count
  [vertex_count * 12] positions (vec3, always present)
  [vertex_count * 12] normals (if flag)
  [vertex_count * 16] tangents (if flag)
  [vertex_count * 8]  uv0 (if flag)
  [vertex_count * 8]  uv1 (if flag)
  [vertex_count * 16] colors (if flag)
  [index_count * 4]   indices (u32)
  [face_count * 2]    polygroup IDs (u16, face_count = index_count/3)

All values little-endian. The serializer writes a mesh_slot_t to a byte buffer; the deserializer reads a byte buffer into a mesh_slot_t.

Files to create:
- include/ferrum/editor/mesh/mesh_vao_format.h — constants, flag bits, serialize/deserialize API
- src/editor/mesh/mesh_vao_serialize.c — mesh_vao_serialize() writes slot → buffer
- src/editor/mesh/mesh_vao_deserialize.c — mesh_vao_deserialize() reads buffer → slot
- tests/editor/mesh_vao_format_tests.c

## Acceptance Criteria

- Round-trip: serialize then deserialize produces identical mesh data
- Handles meshes with all optional attributes (normals, tangents, uv0, uv1, colors)
- Handles meshes with only positions + indices (minimum)
- Correctly validates magic and version on deserialize
- Rejects truncated or corrupted buffers
- Size calculation matches actual serialized output
- Tests: round-trip, minimal, full attributes, bad magic, truncated, size calc

