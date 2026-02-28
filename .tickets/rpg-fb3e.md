---
id: rpg-fb3e
status: closed
deps: [rpg-d6j3]
links: []
created: 2026-02-26T04:27:43Z
type: task
priority: 2
assignee: KMD
parent: rpg-tiet
tags: [editor, assets, server, client]
---
# Asset downloader (TCP transfer via I/O thread)

Implement the TCP-based asset download protocol between server and client.

READ FIRST: ref/editor_design.md §3 for the full asset download protocol (wire format, server-side implementation via I/O thread), ref/editor_spec.md §2.3 for spec.

The server's editor I/O thread handles asset download connections inline (see design §3.4). The client connects to the asset port and requests files by path.

Requirements:
- Server side: accept asset download TCP connections on I/O thread, serve files inline
- Wire format: request (u16 path_len + utf8 path), response (u8 status + u32 total_len + raw data)
- Client side: connect to server asset port, request assets needed for rendering
- Sequential requests per connection (request → response → request → ...)
- Status codes: 0=OK, 1=not found, 2=error
- Max path length 1024 bytes
- Trigger: client downloads assets when it receives entity spawn events referencing unknown assets

Files to create:
- src/editor/assets/edit_asset_serve.c (server-side, called from I/O thread)
- src/editor/client/client_asset_download.c (client-side TCP client)
- tests/editor/edit_asset_download_tests.c

