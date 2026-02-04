---
id: rust-rpg-6d8
status: closed
deps: []
links: []
created: 2026-02-03T00:44:40.774336314-08:00
type: feature
priority: 1
---
# job: chase-lev work-stealing deques per worker

Replace the current job queue scanning implementation with Chase–Lev work-stealing deques per worker (owner push/pop bottom, thieves steal top). Keep existing public job system API stable. Provide extensive tests: single-thread semantics, multi-thread work stealing, progress, no lost jobs/double exec, shutdown behavior, deterministic mode behavior, and stress randomized dispatch/yield interleavings. Goal: eliminate O(queue_capacity) scanning and improve scaling.


