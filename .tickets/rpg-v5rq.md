---
id: rpg-v5rq
status: open
deps: [rpg-na3h, rpg-8ufi, rpg-9ebf, rpg-xkdz]
links: []
created: 2026-07-13T05:10:49Z
type: task
priority: 2
assignee: KMD
parent: rpg-mvmh
---
# Deferred pass integration with forward+ pipeline + test

Slot the deferred particle-light pass into the pipeline after forward+, share resources (depth/G-buffer), and add a test/example driving thousands of particle lights, verifying cost and correctness vs a forward reference.

## Design

Core renderer. Depends on G-buffer, binning, deferred shader, and the pipeline integration.

