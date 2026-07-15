---
id: rpg-na3h
status: open
deps: [rpg-nvw0]
links: []
created: 2026-07-13T05:10:49Z
type: task
priority: 2
assignee: KMD
parent: rpg-mvmh
---
# Thin G-buffer for the deferred pass

Capture the minimal attributes the deferred particle-light BRDF needs: reuse depth (reconstruct world position), plus normal and albedo/roughness/metalness as required. Sized to blend onto the forward+ result.

## Design

Core renderer. Depends on the forward+ pipeline. Keep the G-buffer thin; reconstruct position from depth.

