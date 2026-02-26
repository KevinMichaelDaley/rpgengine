---
id: rpg-fu56
status: closed
deps: [rpg-95es, rpg-ssj2, rpg-nkhw, rpg-3p7n]
links: []
created: 2026-02-26T04:26:35Z
type: task
priority: 1
assignee: KMD
parent: rpg-wxom
tags: [editor, foundation, server]
---
# Editor context initialization and tick integration

Implement editor_ctx_t and integrate the editor subsystem into the server tick loop.

READ FIRST: ref/editor_spec.md §7.1 for server-side integration table, ref/editor_design.md §11 for threading model.

This is the glue that ties all Phase 1 server-side components together.

Requirements:
- editor_ctx_t struct: holds I/O thread handle, command/response rings, undo stack, selection, script runtime pointer, configuration
- editor_ctx_init(server_ctx, config) → starts I/O thread, allocates rings, initializes undo stack
- editor_ctx_shutdown() → stops I/O thread, frees resources
- Integrate drain call into server tick Stage 1: call editor_dispatch_drain() between RUDP drain and physics tick
- Gated behind EDITOR_ENABLE compile flag
- Configuration: edit_port, asset_port, max_entities, undo_capacity

Files to create:
- include/ferrum/editor/editor_ctx.h
- src/editor/editor_ctx.c
- src/editor/editor_tick_drain.c

Dependencies: all Phase 1 server-side tasks

