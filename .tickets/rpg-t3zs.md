---
id: rpg-t3zs
status: closed
deps: []
links: []
created: 2026-03-01T23:15:02Z
type: task
priority: 1
assignee: KMD
---
# Async raycast execution: drain VIS_TEST tasks into batched physics raycasts

Wire the MPSC async task buffer to the physics raycast system. Drain VIS_TEST tasks, convert params to phys_ray_t, call phys_raycast against the world, write packed results (distance + hit_point) to result_ptr, set status COMPLETE. Add phys_static_bvh_raycast for BVH ray traversal. Tests use real phys_world.

