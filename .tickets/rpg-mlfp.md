---
id: rpg-mlfp
status: closed
deps: []
links: []
created: 2026-03-18T07:04:27Z
type: bug
priority: 1
assignee: KMD
tags: [prefab, performance]
---
# Bone transforms have ~1 second latency before mesh responds

Dragging bone gizmos in prefab mode has nearly 1 second of latency before the skinned mesh visually updates. This makes bone editing unusable. Likely causes: (1) skeleton dirty flag triggers a full re-upload of the bone palette SSBO/UBO every frame instead of incremental update, (2) the skinning pipeline re-reads the skeleton from registry each frame instead of caching, (3) the bone gizmo apply path recomputes the full rest_world hierarchy on every drag delta instead of only updating the affected subtree, or (4) GPU readback stalls from PBO video capture blocking the main thread.

