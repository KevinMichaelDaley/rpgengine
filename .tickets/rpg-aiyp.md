---
id: rpg-aiyp
status: open
deps: []
links: [rpg-hy7z]
created: 2026-07-21T01:37:54Z
type: task
priority: 2
assignee: KMD
parent: rpg-k23d
tags: [renderer, perf]
---
# Interleaved vertex VBO + u16 indices (and fix OOB default-attr fetch)

Section 5.5. static_mesh_create.c:109-166 builds 7 buffer objects/mesh (pos/nrm/tan4/uv0/uv1/col4 + ibo), all GL_FLOAT = 72 B/vertex. Six fetch streams defeat the vertex-fetch cache on GCN/older Intel; indices always GL_UNSIGNED_INT (static_mesh_draw.c:26-31, gltf_mesh_create.c:106). Fix (callers unchanged): one interleaved VBO inside static_mesh_create; u16 index path for <65k-vertex meshes; later half-float uv/color + 10_10_10_2 normals/tangents.
Correctness hazard bundled here (section 8 #4): missing attributes upload a ONE-vertex default buffer (upload_vbo_, :43-49) but bind it as a per-vertex array with nonzero stride (:153-166) -- vertices >0 fetch past the buffer end (works only via driver robustness). Use glDisableVertexAttribArray + glVertexAttrib4f constants instead (also saves 3 VBOs on typical meshes).

## Acceptance Criteria

Static meshes use one interleaved VBO with u16 indices under 65k verts; missing attributes use constant glVertexAttrib4f (no per-vertex OOB fetch); callers are unchanged.

