---
id: phys-004
status: open
deps: [phys-001, phys-003]
links: [phys-000]
created: 2026-02-06T05:20:00.000000000-08:00
type: task
priority: 1
---
# Step 0.4: AABB Structure and Computation

**Parent Epic:** phys-000 (Phase 0: Foundation Data Structures)

## Description

Define AABB structure and implement computation for all three primitive types,
including rotated boxes and capsules.

## Files to create

- `include/ferrum/physics/aabb.h`
- `src/physics/collision/aabb.c`
- `tests/physics/aabb_tests.c`

## Structure

```c
typedef struct phys_aabb_t {
    phys_vec3_t min;
    phys_vec3_t max;
} phys_aabb_t;  // 24 bytes
```

## API

```c
void phys_aabb_from_sphere(phys_aabb_t *aabb, phys_vec3_t center, float radius);
void phys_aabb_from_box(phys_aabb_t *aabb, phys_vec3_t center, phys_quat_t rotation, phys_vec3_t half_extents);
void phys_aabb_from_capsule(phys_aabb_t *aabb, phys_vec3_t center, phys_quat_t rotation, float radius, float half_height);

bool phys_aabb_overlap(const phys_aabb_t *a, const phys_aabb_t *b);
void phys_aabb_merge(phys_aabb_t *out, const phys_aabb_t *a, const phys_aabb_t *b);
void phys_aabb_expand(phys_aabb_t *aabb, float margin);
phys_vec3_t phys_aabb_center(const phys_aabb_t *aabb);
phys_vec3_t phys_aabb_extents(const phys_aabb_t *aabb);
float phys_aabb_surface_area(const phys_aabb_t *aabb);
```

## Acceptance Criteria

- [ ] Correct AABB computation for sphere (trivial)
- [ ] Correct AABB for axis-aligned box
- [ ] Correct AABB for rotated box (world-aligned bounding box)
- [ ] Correct AABB for axis-aligned capsule
- [ ] Correct AABB for rotated capsule
- [ ] Overlap test works for all cases
- [ ] Merge produces bounding AABB

## Test Cases

```c
// test_aabb_sphere
phys_aabb_t aabb;
phys_aabb_from_sphere(&aabb, (phys_vec3_t){5, 10, 15}, 2.0f);
ASSERT_VEC3_EQ(aabb.min, (phys_vec3_t){3, 8, 13});
ASSERT_VEC3_EQ(aabb.max, (phys_vec3_t){7, 12, 17});

// test_aabb_box_axis_aligned
phys_aabb_from_box(&aabb, (phys_vec3_t){0, 0, 0}, PHYS_QUAT_IDENTITY, (phys_vec3_t){1, 2, 3});
ASSERT_VEC3_EQ(aabb.min, (phys_vec3_t){-1, -2, -3});
ASSERT_VEC3_EQ(aabb.max, (phys_vec3_t){1, 2, 3});

// test_aabb_box_rotated_45
// 45° rotation around Y axis for a 2x1x1 box
phys_quat_t rot45 = quat_from_axis_angle((phys_vec3_t){0,1,0}, M_PI/4);
phys_aabb_from_box(&aabb, (phys_vec3_t){0,0,0}, rot45, (phys_vec3_t){2, 0.5f, 0.5f});
// X extent should be larger than 2 due to rotation
ASSERT(aabb.max.x > 2.0f);
ASSERT(aabb.max.z > 0.5f);
// Y should be unchanged
ASSERT_FLOAT_NEAR(aabb.max.y, 0.5f, 0.001f);

// test_aabb_capsule_vertical
phys_aabb_from_capsule(&aabb, (phys_vec3_t){0,0,0}, PHYS_QUAT_IDENTITY, 0.5f, 1.0f);
// Capsule aligned with Y: total height = 2*half_height + 2*radius = 2 + 1 = 3
ASSERT_FLOAT_NEAR(aabb.min.y, -1.5f, 0.001f);
ASSERT_FLOAT_NEAR(aabb.max.y, 1.5f, 0.001f);
ASSERT_FLOAT_NEAR(aabb.min.x, -0.5f, 0.001f);

// test_aabb_capsule_horizontal
phys_quat_t rot_x = quat_from_axis_angle((phys_vec3_t){0,0,1}, M_PI/2);
phys_aabb_from_capsule(&aabb, (phys_vec3_t){0,0,0}, rot_x, 0.5f, 1.0f);
// Now aligned with X
ASSERT_FLOAT_NEAR(aabb.min.x, -1.5f, 0.001f);
ASSERT_FLOAT_NEAR(aabb.max.x, 1.5f, 0.001f);

// test_aabb_overlap
phys_aabb_t a = {{0,0,0}, {2,2,2}};
phys_aabb_t b = {{1,1,1}, {3,3,3}};
ASSERT(phys_aabb_overlap(&a, &b));

phys_aabb_t c = {{5,5,5}, {6,6,6}};
ASSERT(!phys_aabb_overlap(&a, &c));

// test_aabb_touching_is_overlap
phys_aabb_t d = {{2,0,0}, {4,2,2}};  // touching at x=2
ASSERT(phys_aabb_overlap(&a, &d));

// test_aabb_merge
phys_aabb_t merged;
phys_aabb_merge(&merged, &a, &c);
ASSERT_VEC3_EQ(merged.min, (phys_vec3_t){0, 0, 0});
ASSERT_VEC3_EQ(merged.max, (phys_vec3_t){6, 6, 6});

// test_aabb_expand
phys_aabb_expand(&a, 0.5f);
ASSERT_VEC3_EQ(a.min, (phys_vec3_t){-0.5f, -0.5f, -0.5f});
ASSERT_VEC3_EQ(a.max, (phys_vec3_t){2.5f, 2.5f, 2.5f});
```
