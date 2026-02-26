---
id: rpg-wxom
status: closed
deps: []
links: []
created: 2026-02-26T04:24:50Z
type: epic
priority: 1
assignee: KMD
tags: [editor, foundation]
---
# Phase 1: Editor Foundation

Epic for the core editor foundation. This phase establishes the fundamental architecture: the server-side editor I/O thread, the JSON edit protocol, basic entity commands, undo/redo, the controller TUI, and the client-side 3D cursor and state socket. All subsequent phases depend on Phase 1.

Before starting any subtask, read:
- ref/editor_spec.md (architecture specification)
- ref/editor_design.md (implementation details)
- ref/editor_ux.md (UX specification)

Key architectural constraints:
- Dedicated editor I/O thread (epoll), NOT fibers for TCP
- SPSC command ring bridges I/O thread → main tick thread
- Commands are deferred: enqueued by I/O thread, drained in Stage 1 of server tick
- All entity mutations happen during drain (between physics ticks)
- Client state socket is bidirectional (push events for mouse/click)
- All processes communicate over TCP (can run on separate machines)

