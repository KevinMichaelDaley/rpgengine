---
id: rpg-kg03
status: closed
deps: [rpg-kg02]
links: [rpg-kg02]
created: 2026-05-03T20:00:00Z
type: bug
priority: 1
assignee: KMD
parent:
tags: [knowledge-graph, bug, astar, priority-queue]
---
# npc_kg_astar PQ_MAX=256 Silently Drops Nodes

`npc_kg_astar` in `src/npc/graph/npc_kg_astar.c:13` uses a fixed-elements priority queue:
```c
#define PQ_MAX 256
```
When the queue fills, `pq_push` silently returns without inserting (line 29-30). For knowledge graphs with more than 256 nodes, this causes A* to fail to find valid paths — producing false negatives with zero diagnostic output.

## Root Cause
The PQ is statically allocated on the stack. For graphs known to be small (<100 nodes) this is fine, but the system is designed for per-NPC KGs that can grow unboundedly.

## Fix
Either:
1. Increase PQ_MAX to the graph's node_count (dynamically allocate)
2. Use the graph's node_count as the PQ capacity
3. Add error/warning when PQ overflow causes dropped nodes

## Acceptance
- [ ] A* succeeds on graphs with 1000+ nodes
- [ ] PQ overflow produces an error return, not silent failure
- [ ] No heap allocation on the hot path (use caller-provided buffer)
