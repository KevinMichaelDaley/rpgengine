---
id: rpg-hy7z
status: open
deps: []
links: [rpg-aiyp]
created: 2026-07-21T01:37:54Z
type: task
priority: 2
assignee: KMD
parent: rpg-k23d
tags: [renderer, perf, streaming]
---
# Single-pass level load: stop reading every mesh from disk twice

Section 5.4. client_scene_load.c:302-340 deserializes every mesh on the GL thread; then client_static_volume_build (client_static_volume.c:80-130) RE-OPENS and re-deserializes every mesh file and re-reads SH planes from the .flm the loader already read -- doubling level-load IO/decode (seconds on Deck cold storage). The engine already has the right machinery (resource_loader.c job-fiber decode + gpu_cmd_queue) unused by the world loader; client_bake.c already demonstrates keeping slots alive (ctx.slots). Fix: single-pass load, keep deserialized slots through volume build; move image decode to job fibers. Also glTF: cgltf_accessor_read_float per component per vertex (gltf_mesh_create.c:129-201) -- memcpy fast path for tightly-packed accessors.

## Acceptance Criteria

Each mesh + SH plane is read/decoded once at load; the static volume build reuses the loader's slots; image decode runs on job fibers.

