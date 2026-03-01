---
id: rpg-a0h6
status: closed
deps: []
links: []
created: 2026-03-01T03:08:26Z
type: task
priority: 2
assignee: KMD
---
# Noclip editor camera: move in look direction (unconstrained vertical)

The editor noclip camera currently moves on a horizontal plane regardless of pitch. When looking up or down, W/S should move in the actual look direction (forward vector from pitch+yaw), not projected onto the XZ plane. This makes navigating vertical spaces (tall rooms, looking at ceilings/floors) much more intuitive. The camera should behave like a true 6DOF noclip camera where movement is always along the camera's forward/right/up vectors.

