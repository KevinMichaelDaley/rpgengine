---
id: rpg-obka
status: in_progress
deps: []
links: []
created: 2026-03-18T07:11:32Z
type: feature
priority: 2
assignee: KMD
tags: [bones, editor]
---
# Bone editing should work in regular edit mode, not just prefab mode

Currently bones are only visible, selectable, and transformable when in prefab-edit mode (P key). Bones should also be expandable in the outliner and transformable with gizmos in regular edit mode when a skeletal mesh entity is selected. Bone pose overrides made in regular edit mode should be saved per skeleton instance — i.e., per the specific entity that has the skeleton bound, not globally to the fskel file or to a prefab. This means each entity with a skeleton needs its own bone pose override storage (likely in entity_attrs_t or a parallel per-entity structure), separate from the prefab's bone_rest_local and the registry's skeleton_def_t.

