---
id: rpg-gn5m
status: open
deps: []
links: []
created: 2026-03-01T23:01:17Z
type: epic
priority: 2
assignee: KMD
---
# Navigation & Pathfinding System

Implement a navigation mesh and pathfinding system for the game engine. This includes navmesh generation from world geometry, A* or similar pathfinding queries, path smoothing, and dynamic obstacle avoidance. The nav_query async instruction (implemented as a stub in rpg-x7eg) will need to be expanded to actually perform pathfinding queries against the navmesh. Currently nav_query enqueues a task to the async buffer but the world subsystem has no navmesh to query against. This epic covers the full pipeline: navmesh construction, query execution in the world subsystem drain loop, and result delivery back to scripts via the async task buffer.

