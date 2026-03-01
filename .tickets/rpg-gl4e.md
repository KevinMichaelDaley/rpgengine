---
id: rpg-gl4e
status: open
deps: []
links: []
created: 2026-03-01T01:34:21Z
type: bug
priority: 2
assignee: KMD
---
# Debug: editor-spawned entities not reaching connected clients

When entities are created via editor commands (mesh_create + mesh_commit or spawn), the physics bodies are created server-side but never sent to connected clients. Client shows 0 bodies despite server having 8+ entities. Investigate: (1) Is FR_SERVER_EVT_PLAYER_JOIN firing? (2) Are physics bodies in the world? (3) Is send_body_spawns_to_client entering its loop? (4) Is the reliable channel delivering? (5) Is client RUDP processing reliable messages?

