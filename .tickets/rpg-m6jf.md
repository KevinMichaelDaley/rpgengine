---
id: rpg-m6jf
status: closed
deps: [rpg-nulp, rpg-syut]
links: []
created: 2026-02-28T22:22:05Z
type: task
priority: 2
assignee: KMD
parent: rpg-caw8
tags: [editor, mesh, networking]
---
# Asset downloader integration for mesh snapshots

Integrate mesh VAO snapshot transfer with the existing asset download TCP channel.

When the server modifies mesh geometry, it sends a 'mesh_vao_update' notification over the edit protocol with the slot index and a content hash. The client then requests the VAO binary blob via the asset download TCP connection (same channel used for textures/meshes).

Server-side:
- After any mesh-modifying command, serialize the affected slot to FVMA binary
- Store the serialized blob in a server-side cache keyed by slot index
- Send mesh_vao_update notification: {"type":"mesh_vao_update","slot":N,"hash":H,"size":S}
- Asset download server responds to path '@mesh/N' with the cached FVMA blob

Client-side:
- Handle mesh_vao_update notifications from edit protocol
- Request '@mesh/N' from asset download channel
- Deserialize FVMA blob into client-side mesh rendering data

Also send selection state updates as lightweight messages:
- sel_update: {"type":"sel_update","mode":"face","indices":[...]}

Files to create:
- src/editor/mesh/mesh_snapshot.c — serialize slot to cached blob, hash computation
- src/editor/mesh/mesh_notify.c — notification generation after mesh changes
- tests/editor/mesh_snapshot_tests.c

## Acceptance Criteria

- mesh_vao_update notification sent after geometry modification
- Asset download server serves '@mesh/N' paths from cached FVMA blobs
- Content hash changes when mesh geometry changes
- Multiple slots can be served simultaneously
- Client can request and receive mesh snapshots via existing download channel
- Selection update messages correctly encode current selection state
- Tests: snapshot cache, hash computation, notification generation, download integration

