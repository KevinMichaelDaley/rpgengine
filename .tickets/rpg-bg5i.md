---
id: rpg-bg5i
status: open
deps: [rpg-2hjw]
links: []
created: 2026-03-02T00:28:15Z
type: chore
priority: 3
assignee: KMD
tags: [rename, refactor, breaking]
---
# Rename fr_/FR_ API symbol prefix to tal_/TAL_

Follow-up to rpg-2hjw (ferrum→talarium path rename). Rename all public API symbol prefixes from fr_/FR_ to tal_/TAL_. This includes types (fr_topic_channel_t, fr_server_net_runtime_t, fr_rudp_stream_t, etc.), functions (fr_server_tick_loop_init, fr_priority_body_sender_create, etc.), and macros (FR_NET_EMULATION, FR_JOB_INSTRUMENTATION, etc.). Hundreds of symbols across ~970 files. Do NOT start until rpg-2hjw is complete.

