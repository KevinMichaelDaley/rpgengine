---
id: phys-001
status: open
deps: []
links: [phys-000]
created: 2026-02-06T05:20:00.000000000-08:00
type: task
priority: 1
---
# Step 0.1: Core Math Types

**Parent Epic:** phys-000 (Phase 0: Foundation Data Structures)

## Description

Define or re-export core math types for physics use. Ensure predictable layout
and conversion to/from engine math types.

## Files to create

- `include/ferrum/physics/phys_types.h`

## Structures

```c
// Re-export or alias engine math types for physics use
typedef struct phys_vec3_t { float x, y, z; } phys_vec3_t;
typedef struct phys_quat_t { float x, y, z, w; } phys_quat_t;
typedef struct phys_mat3_t { float m[9]; } phys_mat3_t;  // 3x3 for inertia tensor

// Conversion macros
#define PHYS_VEC3_FROM_VEC3(v) ((phys_vec3_t){(v).x, (v).y, (v).z})
#define VEC3_FROM_PHYS_VEC3(v) ((vec3_t){(v).x, (v).y, (v).z})
// Similar for quat
```

## Acceptance Criteria

- [ ] Types defined with predictable layout (no padding surprises)
- [ ] Conversion macros to/from engine math types
- [ ] Size assertions: phys_vec3_t == 12 bytes, phys_quat_t == 16 bytes

## Test Cases

```c
// test_type_sizes
ASSERT(sizeof(phys_vec3_t) == 12);
ASSERT(sizeof(phys_quat_t) == 16);
ASSERT(sizeof(phys_mat3_t) == 36);

// test_conversion_round_trip
vec3_t v = {1.0f, 2.0f, 3.0f};
phys_vec3_t pv = PHYS_VEC3_FROM_VEC3(v);
vec3_t v2 = VEC3_FROM_PHYS_VEC3(pv);
ASSERT_VEC3_EQ(v, v2);
```
