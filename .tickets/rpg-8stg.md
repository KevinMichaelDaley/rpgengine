---
id: rpg-8stg
status: open
deps: []
links: []
created: 2026-07-15T08:23:06Z
type: task
priority: 3
assignee: KMD
parent: rpg-h553
tags: [deferred, editor, renderer]
---
# Route editor asset creation through the resource paradigm


## Notes

**2026-07-15T08:23:31Z**

DEFERRED. Same resource-paradigm retrofit (rpg-h553) for the editor's asset creation (entity mesh cache, material textures, thumbnails). Lower priority than the demo-client path (rpg-oxt1); do after the client migration settles. Pattern: loader fibers -> gpu_cmd_queue -> gpu_executor with GPU_CMD_CUSTOM finalisers; pool/arena-backed, no loop allocation.
