---
id: phys-003
status: open
deps: [phys-001]
links: [phys-000]
created: 2026-02-06T05:20:00.000000000-08:00
type: task
priority: 1
---
# Step 0.3: Collider Structures (Sphere, Box, Capsule)

**Parent Epic:** phys-000 (Phase 0: Foundation Data Structures)

## Description

Define all three primitive collider types and the collider reference structure
that links bodies to shapes.

## Files to create

- `include/ferrum/physics/collider.h`
- `src/physics/collider/collider.c`
- `tests/physics/collider_tests.c`

## Structures

```c
typedef enum phys_shape_type_t {
    PHYS_SHAPE_SPHERE = 0,
    PHYS_SHAPE_BOX,
    PHYS_SHAPE_CAPSULE,
    PHYS_SHAPE_CONVEX,    // future
    PHYS_SHAPE_MESH,      // future
    PHYS_SHAPE_COUNT
} phys_shape_type_t;

typedef struct phys_sphere_t {
    float radius;
} phys_sphere_t;  // 4 bytes

typedef struct phys_box_t {
    phys_vec3_t half_extents;
} phys_box_t;  // 12 bytes

typedef struct phys_capsule_t {
    float radius;
    float half_height;  // height of cylinder segment / 2
} phys_capsule_t;  // 8 bytes

typedef struct phys_collider_t {
    phys_shape_type_t type;       // 4 bytes
    uint32_t shape_index;         // into shape-specific pool
    phys_vec3_t local_offset;     // offset from body origin
    phys_quat_t local_rotation;   // rotation relative to body
} phys_collider_t;  // 36 bytes
```

## API

```c
void phys_collider_init_sphere(phys_collider_t *c, uint32_t sphere_idx, phys_vec3_t offset);
void phys_collider_init_box(phys_collider_t *c, uint32_t box_idx, phys_vec3_t offset, phys_quat_t rotation);
void phys_collider_init_capsule(phys_collider_t *c, uint32_t capsule_idx, phys_vec3_t offset, phys_quat_t rotation);

// Transform helpers
phys_vec3_t phys_collider_world_center(const phys_collider_t *c, phys_vec3_t body_pos, phys_quat_t body_rot);
phys_quat_t phys_collider_world_rotation(const phys_collider_t *c, phys_quat_t body_rot);
```

## Acceptance Criteria

- [ ] All three primitive types defined
- [ ] Local transform (offset + rotation) supported
- [ ] Shape index indirection to shape-specific pools
- [ ] World transform helpers work correctly

## Test Cases

```c
// test_sphere_collider_init
phys_collider_t c;
phys_collider_init_sphere(&c, 42, (phys_vec3_t){1, 0, 0});
ASSERT(c.type == PHYS_SHAPE_SPHERE);
ASSERT(c.shape_index == 42);
ASSERT_VEC3_EQ(c.local_offset, (phys_vec3_t){1, 0, 0});

// test_box_collider_with_rotation
phys_quat_t rot = quat_from_axis_angle((phys_vec3_t){0,1,0}, M_PI/4);
phys_collider_init_box(&c, 10, (phys_vec3_t){0,0,0}, rot);
ASSERT(c.type == PHYS_SHAPE_BOX);
ASSERT(!quat_is_identity(c.local_rotation));

// test_world_center
phys_collider_init_sphere(&c, 0, (phys_vec3_t){1, 0, 0});
phys_vec3_t body_pos = {10, 0, 0};
phys_quat_t body_rot = PHYS_QUAT_IDENTITY;
phys_vec3_t world = phys_collider_world_center(&c, body_pos, body_rot);
ASSERT_VEC3_EQ(world, (phys_vec3_t){11, 0, 0});

// test_world_center_rotated_body
body_rot = quat_from_axis_angle((phys_vec3_t){0,0,1}, M_PI/2);  // 90° around Z
world = phys_collider_world_center(&c, body_pos, body_rot);
// Offset (1,0,0) rotated 90° around Z becomes (0,1,0)
ASSERT_VEC3_NEAR(world, (phys_vec3_t){10, 1, 0}, 0.001f);
```
