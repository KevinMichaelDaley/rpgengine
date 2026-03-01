---
id: rpg-ktq6
status: closed
deps: [rpg-8hc1]
links: []
created: 2026-03-01T09:58:49Z
type: epic
priority: 1
assignee: KMD
parent: rpg-p9zq
tags: [aegis, vm, async]
---
# Aegis VM Phase 3: Async Operations

Implements async world queries (vis_test, nav_query) with MPSC task buffer, poll/wait instructions with fiber yield integration. See ref/aegis_bytecode_spec.md §3.3, §3.5, and §10 Phase 3.

