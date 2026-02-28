---
id: rpg-caw8
status: closed
deps: []
links: []
created: 2026-02-28T22:19:47Z
type: epic
priority: 1
assignee: KMD
tags: [editor, mesh, modeling]
---
# Phase 1: Mesh Modeling Foundation

Foundation for the mesh modeling mode. Establishes server-side mesh data structures (mesh_slot_t, mesh_edit_t), VAO binary format for client transfer, primitive mesh creation, selection mode system (vertex/edge/face/polygroup/object), mesh element selection commands, asset downloader integration for mesh snapshots, and basic client-side mesh rendering.

READ FIRST: ref/mesh_modeling_spec.md — full specification for the mesh modeling system.

This phase delivers the minimum viable mesh editing loop: create a box primitive, switch selection modes, select faces/vertices/edges, and see the mesh rendered on the client. All subsequent phases build on these foundations.

Key design decisions:
- Mesh data lives server-side; server is authoritative for all geometry operations
- Client receives complete mesh snapshots over the existing asset download TCP channel
- mesh_slot_t provides 16 slots: @active (primary), @scratch (clipboard), @temp (previews)
- Selection state uses dynamic bitsets for vertices, edges, faces, and polygroup IDs
- VAO binary format: magic 'FVMA', versioned, with optional attribute flags

