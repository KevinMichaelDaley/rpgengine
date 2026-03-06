---
id: rpg-b17x
status: closed
deps: [rpg-qslo]
links: []
created: 2026-03-06T06:14:07Z
type: task
priority: 2
assignee: KMD
parent: rpg-ywes
tags: [animation, format, serialization]
---
# Custom skeletal mesh format (.fskel) and loader

## Summary

Design and implement the .fskel binary format for storing skeletal mesh data with full constraint definitions, plus a loader that creates skeleton_def_t + skeletal_mesh_t from the file. This format extends the existing FVMA pattern (header + typed chunks) with skeleton-specific chunks.

## Motivation

glTF does not export Blender pose constraints. We need a custom format that stores:
- The complete skeleton hierarchy (joint names, parents, rest transforms)
- Per-joint constraint stacks (all 20 constraint types with parameters)
- Inverse bind matrices
- Optional: animation clips referenced by Action constraints

This format is the authoring pipeline output — a Blender exporter addon or converter tool writes .fskel files, the engine loader reads them.

## Deliverables

### 1. .fskel format specification
Binary format with magic number, version, and typed chunks:
```
Header: magic (4 bytes "FSKL"), version (uint32), chunk_count (uint32)
Chunks:
  SKEL: skeleton hierarchy (joint_count, names[], parent_indices[], rest_local_transforms[])
  IBM:  inverse bind matrices (joint_count × mat4)
  CNST: constraint definitions (total_count, per-joint offsets, constraint_def_t array)
  MESH: mesh data reference (FVMA file path or inline vertex/index data)
  CLIP: animation clip references (for Action constraints)
```

### 2. Format writer (for tools/converters)
```c
bool fskel_write(const char *path, const skeleton_def_t *skel, const mat4 *ibms,
                 const skeletal_mesh_t *mesh);
```

### 3. Format loader
```c
bool fskel_load(const char *path, skeleton_def_t *out_skel, mat4 **out_ibms,
                uint32_t *out_ibm_count);
```
Allocates skeleton_def_t with all constraints populated. Caller owns memory and frees via skeleton_def_destroy().

### 4. glTF-to-fskel converter (command-line tool)
Converts humanoid.glb (or any glTF with skeleton) to .fskel:
- Extracts skeleton hierarchy from glTF skin
- Extracts IBMs
- Generates stub constraints (no constraints in glTF, but format supports adding them)
- Later: reads constraint data from Blender custom properties exported to glTF extras

## File Structure
```
include/ferrum/animation/fskel_format.h   — format constants, chunk types
include/ferrum/animation/fskel_loader.h   — load/save API
src/animation/format/fskel_write.c        — writer
src/animation/format/fskel_load.c         — loader
src/animation/format/fskel_validate.c     — validation (magic, version, chunk integrity)
tools/gltf_to_fskel.c                     — converter tool
```

## Acceptance Criteria
- [ ] Format round-trips: write → load produces identical skeleton_def_t
- [ ] Format handles 0 constraints per joint (skeleton-only)
- [ ] Format handles max constraints (e.g., 8 constraints on one joint)
- [ ] Format validates magic number and version on load
- [ ] Format detects and rejects corrupted/truncated files
- [ ] Loader allocates all memory up-front (no per-chunk malloc during load)
- [ ] Writer produces byte-identical output for identical input
- [ ] glTF converter successfully converts humanoid.glb
- [ ] Unit tests: round-trip, validation, corruption detection, empty skeleton, large skeleton (500+ joints)
- [ ] ≤4 non-static functions per source file
- [ ] Clean under -Wall -Wextra -Wpedantic

## Dependencies
- Depends on unified constraint types (rpg-qslo) for constraint_def_t and skeleton_def_t


