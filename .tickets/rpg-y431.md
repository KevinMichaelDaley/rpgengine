---
id: rpg-y431
status: open
deps: []
links: []
created: 2026-03-18T07:04:20Z
type: bug
priority: 2
assignee: KMD
tags: [prefab, renderer]
---
# Bones render attached to camera view in fly mode then disappear

In prefab mode, when entering fly mode (arrow keys), bone capsule overlays sometimes render as if attached to the view (following camera movement) and then disappear entirely. This suggests the bone overlay draw pass uses a stale or incorrect view/projection matrix during fly mode, or the bone world positions are computed relative to the camera instead of world space.


## Notes

**2026-03-18T07:24:07Z**

Investigated the rendering code. Bone overlay properly sets u_view/u_projection uniforms on the flat shader and builds model matrix from entity transform. The draw path is identical in fly mode vs orbit mode — fly mode only updates camera position/orientation, which flows through editor_camera_view_matrix() into the same view variable used by the overlay. This may be a GL state ordering issue or a timing issue where the camera update and draw happen out of order in a specific fly mode scenario. Needs in-editor reproduction with logging.
