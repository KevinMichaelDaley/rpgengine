---
id: rpg-npl4
status: open
deps: []
links: []
created: 2026-02-13T06:49:08Z
type: task
priority: 4
assignee: KMD
---
# STATE_CUBE variant for skeletal mesh pose replication

Add a new replication message type (e.g. STATE_SKEL) for skeletal meshes. Should encode bone transforms (or a compact subset like root + IK targets) in addition to entity position/rotation/velocity. Consider smallest-3 quat per bone, delta compression vs keyframe, and bandwidth budget. Downstream of current rigid-body-only STATE_CUBE. Blocked by: skeletal animation system existing.

