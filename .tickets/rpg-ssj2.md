---
id: rpg-ssj2
status: closed
deps: [rpg-95es]
links: []
created: 2026-02-26T04:26:35Z
type: task
priority: 1
assignee: KMD
parent: rpg-wxom
tags: [editor, foundation, server]
---
# Command dispatch framework (edit_dispatch.c)

Implement the command dispatch table and drain loop that runs on the main tick thread.

READ FIRST: ref/editor_design.md §2.4 for dispatch table and handler signatures, ref/editor_spec.md §2.4 for deferred execution model.

The dispatch framework drains the SPSC command ring in Stage 1 of the server tick, looks up handlers by command name, executes them, records undo entries, and pushes responses into the response ring.

Requirements:
- editor_cmd_handler_t function pointer table (see design §2.4 for full table)
- editor_dispatch_drain(editor_ctx_t *ctx) called once per tick
- Drain loop: pop commands, dispatch to handler, record undo, push response
- All handlers follow the same signature: bool handler(editor_ctx_t*, const json_value_t* args, json_value_t* result)
- Handlers return request_ids (deferred execution model)
- Unknown commands return error response with 'unknown_command'
- Malformed args return error response with 'invalid_args'

Files to create:
- include/ferrum/editor/edit_dispatch.h
- src/editor/dispatch/edit_dispatch.c
- src/editor/dispatch/edit_dispatch_drain.c

Dependencies: edit_cmd_ring, json_parse

