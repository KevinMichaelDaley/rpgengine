---
id: phys-600
status: open
deps: [phys-300]
links: []
created: 2026-02-06T05:20:00.000000000-08:00
type: epic
priority: 3
---
# Phase 6: Static BVH

**Goal:** Efficient broadphase for static geometry.

## Overview

Build and query BVH for static bodies:
- SAH (Surface Area Heuristic) construction
- Dynamic-vs-static queries
- Incremental updates for added/removed static bodies

## Subtasks

- phys-601: BVH Build (SAH)
- phys-602: BVH Query
- phys-603: Incremental BVH Update
- phys-604: Phase 6 Integration Test + Benchmark

## Performance Targets

- 10000 static bodies BVH query: < 0.5 ms
