---
id: rpg-88lt
status: open
deps: []
links: []
created: 2026-07-20T06:01:45Z
type: bug
priority: 2
assignee: KMD
---
# Exporter: BAKE_MATERIALS=0 silently emits an untextured level


`scripts/export_scene.py`: with `bake_materials=0` ("reuse the maps already on disk") the reuse check is `os.path.exists(os.path.join(out_dir, rel))` -- it only finds maps already in the **output** dir.

Exporting to a fresh output dir therefore silently emits material records with **no** `albedo`/`normal`/`roughness` paths, and the level renders completely untextured with no warning. This cost a real debugging cycle: it presented as a renderer/GI bug ("walls are just indirect", "can't see materials") when the descriptor simply had no maps to load.

Fix: fall back to the source bake root (`BAKE_ROOT`/prefabs) when the output copy is absent, and/or warn loudly per material when a *tiling* material ends up with zero maps.

## Acceptance Criteria

A no-bake export into a fresh output dir either references the existing maps or fails loudly; it never silently produces an untextured level.
