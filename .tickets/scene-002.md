---
id: scene-002
status: open
deps: []
links: [scene-004, scene-006, scene-008]
created: 2026-04-02T05:17:00Z
type: task
priority: 1
assignee: kmd
---
# Material Definition File Format (.fmat)

JSON file format for defining materials with texture slots and parameters. A material definition specifies which textures to use for each PBR slot and any additional parameters.

## File Format

```json
{
  "name": "wood_crate",
  "slots": {
    "albedo":    "textures/wood_albedo.png",
    "normal":    "textures/wood_normal.png",
    "roughness": "textures/wood_roughness.png",
    "metallic":  null,
    "emissive":  null
  },
  "params": {
    "roughness_factor": 0.8,
    "metallic_factor": 0.0,
    "emissive_strength": 0.0,
    "alpha_cutoff": 0.5
  },
  "flags": {
    "double_sided": false,
    "alpha_blend": false,
    "alpha_test": false
  }
}
```

## Implementation

- `include/ferrum/editor/def/material_def.h` — header with material_def_t struct
- `src/editor/def/material_def.c` — parse JSON, validate, populate struct

### material_def_t Structure

```c
typedef struct material_def {
    char name[64];
    char slot_albedo[256];
    char slot_normal[256];
    char slot_roughness[256];
    char slot_metallic[256];
    char slot_emissive[256];
    float roughness_factor;
    float metallic_factor;
    float emissive_strength;
    float alpha_cutoff;
    bool double_sided;
    bool alpha_blend;
    bool alpha_test;
} material_def_t;
```

## Deliverables

- [ ] Header file with material_def_t declaration
- [ ] JSON parser for .fmat files
- [ ] Integration with existing material slot system

## Acceptance Criteria

- [ ] Can load a .fmat file from disk
- [ ] Parser handles all slot paths
- [ ] Missing slots default to empty string
- [ ] params and flags use sensible defaults
- [ ] Invalid JSON returns error without crashing
