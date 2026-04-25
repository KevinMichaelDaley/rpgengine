---
id: scene-001
status: open
deps: []
links: [scene-003, scene-005, scene-007]
created: 2026-04-02T05:17:00Z
type: task
priority: 1
assignee: kmd
---
# Entity Definition File Format (.fentity)

JSON file format for defining spawnable entity templates. An entity definition specifies mesh, material, physics properties, custom attributes, and scripts that should be applied when spawning an instance.

## File Format

```json
{
  "name": "crate_wood",
  "type": "mesh",
  "mesh": "assets/meshes/crate.fvma",
  "material": "assets/materials/wood_crate.fmat",
  "physics": {
    "static": true,
    "mass": 0,
    "friction": 0.8,
    "restitution": 0.2
  },
  "attrs": {
    "health": 100,
    "destructible": true,
    "pickup_type": "ammo"
  },
  "scripts": ["scripts/destroy_on_damage.lua"]
}
```

## Implementation

- `include/ferrum/editor/def/entity_def.h` — header with entity_def_t struct
- `src/editor/def/entity_def.c` — parse JSON, validate, populate struct
- `src/editor/def/entity_def_cache.c` — cache loaded definitions by path

### entity_def_t Structure

```c
typedef struct entity_def {
    char name[64];
    char mesh_path[256];
    char material_path[256];
    char scripts[8][256];
    uint32_t script_count;
    bool is_static;
    bool is_kinematic;
    float mass;
    float friction;
    float restitution;
    entity_attrs_t attrs;  // Custom attributes
} entity_def_t;
```

## Deliverables

- [ ] Header file with entity_def_t declaration
- [ ] JSON parser for .fentity files
- [ ] Definition cache (hash map: path → entity_def_t)
- [ ] Directory scan for .fentity files (for asset browser)
- [ ] Unit tests for parser

## Acceptance Criteria

- [ ] Can load a .fentity file from disk
- [ ] Parser handles all fields in the schema
- [ ] Missing fields use sensible defaults
- [ ] Invalid JSON returns error without crashing
- [ ] Cache avoids re-loading same file
- [ ] Asset query can list all .fentity files in a directory
