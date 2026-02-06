---
id: phys-010
status: open
deps: [phys-001, phys-002]
links: [phys-000]
created: 2026-02-06T05:20:00.000000000-08:00
type: task
priority: 1
---
# Step 0.10: Constraint and Jacobian Structures

**Parent Epic:** phys-000 (Phase 0: Foundation Data Structures)

## Description

Define constraint and Jacobian structures for the TGS solver. Each contact
generates 1 normal + 2 friction constraint rows.

## Files to create

- `include/ferrum/physics/constraint.h`
- `src/physics/solver/constraint.c`
- `tests/physics/constraint_tests.c`

## Structures

```c
typedef struct phys_jacobian_row_t {
    // Linear Jacobians
    phys_vec3_t J_va;    // linear Jacobian for body A
    phys_vec3_t J_wa;    // angular Jacobian for body A
    phys_vec3_t J_vb;    // linear Jacobian for body B
    phys_vec3_t J_wb;    // angular Jacobian for body B
    
    // Precomputed values
    float effective_mass;  // 1 / (J * M^-1 * J^T)
    float bias;            // Baumgarte + restitution bias
    float lambda;          // accumulated impulse (warmstarted)
    float lambda_min;      // clamp min (-∞ or 0 for normal)
    float lambda_max;      // clamp max (+∞)
} phys_jacobian_row_t;

#define PHYS_MAX_CONSTRAINT_ROWS 3  // 1 normal + 2 friction

typedef struct phys_constraint_t {
    uint32_t body_a;
    uint32_t body_b;
    uint32_t manifold_idx;    // back-reference for warmstart writeback
    uint8_t point_idx;        // which contact point in manifold
    uint8_t row_count;
    uint8_t pad[2];
    phys_jacobian_row_t rows[PHYS_MAX_CONSTRAINT_ROWS];
} phys_constraint_t;
```

## API

```c
// Build contact constraint from manifold point
void phys_constraint_build_contact(
    phys_constraint_t *c,
    const phys_body_t *body_a,
    const phys_body_t *body_b,
    const phys_contact_point_t *contact,
    float friction,
    float restitution,
    float dt,
    float baumgarte,
    float slop);

// Compute tangent directions for friction
void phys_compute_tangent_basis(phys_vec3_t normal, phys_vec3_t *tangent1, phys_vec3_t *tangent2);

// Compute effective mass for a Jacobian row
float phys_compute_effective_mass(
    const phys_jacobian_row_t *row,
    float inv_mass_a, const phys_vec3_t *inv_inertia_a,
    float inv_mass_b, const phys_vec3_t *inv_inertia_b);
```

## Acceptance Criteria

- [ ] Contact constraint generates 1 normal + 2 friction rows
- [ ] Normal row: lambda_min = 0 (push only)
- [ ] Friction rows: lambda_min/max = ±friction_limit (depends on normal impulse)
- [ ] Jacobians computed correctly for point constraints
- [ ] Effective mass precomputed
- [ ] Bias includes Baumgarte stabilization and restitution
- [ ] Tangent basis orthogonal to normal

## Test Cases

```c
// test_tangent_basis_orthogonal
phys_vec3_t normal = {0, 1, 0};
phys_vec3_t t1, t2;
phys_compute_tangent_basis(normal, &t1, &t2);

ASSERT_FLOAT_NEAR(vec3_dot(normal, t1), 0.0f, 0.001f);
ASSERT_FLOAT_NEAR(vec3_dot(normal, t2), 0.0f, 0.001f);
ASSERT_FLOAT_NEAR(vec3_dot(t1, t2), 0.0f, 0.001f);
ASSERT_FLOAT_NEAR(vec3_length(t1), 1.0f, 0.001f);

// test_contact_constraint_rows
phys_body_t a = {0}, b = {0};
phys_body_init(&a);
phys_body_init(&b);
phys_body_set_mass(&a, 1.0f);
phys_body_set_mass(&b, 1.0f);
phys_body_set_sphere_inertia(&a, 1.0f, 0.5f);
phys_body_set_sphere_inertia(&b, 1.0f, 0.5f);

phys_contact_point_t contact = {
    .point_world = {0.5f, 0, 0},
    .normal = {1, 0, 0},
    .penetration = 0.05f
};

phys_constraint_t c;
phys_constraint_build_contact(&c, &a, &b, &contact, 0.5f, 0.3f, 1.0f/60.0f, 0.2f, 0.01f);

ASSERT(c.row_count == 3);

// Normal row
ASSERT(c.rows[0].lambda_min == 0.0f);
ASSERT(c.rows[0].lambda_max > 1e10f);  // effectively infinite

// Normal Jacobian should be along contact normal
ASSERT_VEC3_NEAR(c.rows[0].J_va, (phys_vec3_t){-1, 0, 0}, 0.001f);
ASSERT_VEC3_NEAR(c.rows[0].J_vb, (phys_vec3_t){1, 0, 0}, 0.001f);

// test_effective_mass
float eff = phys_compute_effective_mass(&c.rows[0], 
    a.inv_mass, &a.inv_inertia_diag,
    b.inv_mass, &b.inv_inertia_diag);
ASSERT(eff > 0.0f);
ASSERT(eff < 1.0f);  // should be less than either mass

// test_baumgarte_bias
// With penetration > slop, bias should be non-zero
ASSERT(c.rows[0].bias != 0.0f);
// Bias should push bodies apart (positive)
ASSERT(c.rows[0].bias > 0.0f);
```
