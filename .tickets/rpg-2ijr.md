---
id: rpg-2ijr
status: closed
deps: [rpg-t9ga]
links: []
created: 2026-07-05T22:54:51Z
type: task
priority: 1
assignee: KMD
parent: rpg-t9ga
tags: [srd, critic, libtorch]
---
# srd-critic-01: srd_critic.h — abstract interface and C API

Write the public header. Defines ISrdCritic pure virtual class, AnalyticalCritic::Config struct with all weight fields and defaults, TorchScriptCritic constructor, and the C API (srd_critic_t*, create_analytical, create_torchscript, destroy). Header must compile in both C++ (full interface) and C (opaque pointer only via extern C guard).

## Design

AnalyticalCritic::Config: min_room_size=1.0f, min_corridor_w=0.5f, layout_w=20.0f, layout_h=20.0f, w_penetration=1.0f, w_min_size=0.5f, w_separation=0.3f, w_adjacency=0.2f, w_reachability=1.0f, w_bounds=2.0f. C API uses typedef struct srd_critic srd_critic_t with opaque pointer.

## Acceptance Criteria

Header compiles as C11 (opaque pointer only); header compiles as C++17 (full class definitions); Doxygen on all public symbols; no libtorch headers leak into the C-visible portion

