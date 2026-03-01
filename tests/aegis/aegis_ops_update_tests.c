/**
 * @file aegis_ops_update_tests.c
 * @brief Tests for Aegis update set construction instructions:
 *        build_update, target_entity, set_field, add_hint, push_update.
 *
 * Tests exercise both the op handlers directly and via the VM interpreter
 * with IL-compiled programs.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "ferrum/aegis/aegis_vm.h"
#include "ferrum/aegis/aegis_asm.h"
#include "ferrum/aegis/aegis_update.h"
#include "ferrum/aegis/aegis_ops_update.h"
#include "ferrum/entity/entity_attrs.h"

/* ======================================================================= */
/* Test harness                                                             */
/* ======================================================================= */

static int g_pass = 0;
static int g_fail = 0;

#define ASSERT_TRUE(expr) do { \
    if (!(expr)) { \
        printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        return 1; \
    } \
} while (0)

#define ASSERT_UINT_EQ(expected, actual) do { \
    uint32_t _e = (expected), _a = (actual); \
    if (_e != _a) { \
        printf("  FAIL %s:%d: expected %u, got %u\n", \
               __FILE__, __LINE__, _e, _a); \
        return 1; \
    } \
} while (0)

#define ASSERT_FLOAT_EQ(expected, actual) do { \
    float _e = (expected), _a = (actual); \
    if (fabsf(_e - _a) > 0.001f) { \
        printf("  FAIL %s:%d: expected %.4f, got %.4f\n", \
               __FILE__, __LINE__, _e, _a); \
        return 1; \
    } \
} while (0)

#define RUN(fn) do { \
    printf("RUN  %s\n", #fn); \
    int _r = fn(); \
    if (_r == 0) { printf("OK   %s\n", #fn); g_pass++; } \
    else { g_fail++; } \
} while (0)

/* ======================================================================= */
/* Helpers                                                                  */
/* ======================================================================= */

/** Compile IL source into bytecode. Caller must free bc->instructions. */
static bool compile_il(const char *src, aegis_bytecode_t *bc) {
    aegis_asm_t as;
    memset(&as, 0, sizeof(as));
    uint32_t len = (uint32_t)strlen(src);
    return aegis_asm_compile(&as, src, len, bc);
}

/** Create a pre-allocated update set. Caller must free set->updates. */
static bool make_update_set(aegis_update_set_t *set, uint32_t capacity) {
    memset(set, 0, sizeof(*set));
    set->capacity = capacity;
    set->updates = (aegis_state_update_t *)calloc(
        capacity, sizeof(aegis_state_update_t));
    return set->updates != NULL;
}

/* ======================================================================= */
/* Direct handler tests                                                     */
/* ======================================================================= */

/* Test: build_update initializes empty staging */
static int test_build_update(void) {
    aegis_register_t dst;
    aegis_state_update_t staging;

    ASSERT_TRUE(aegis_op_build_update(&dst, &staging));
    ASSERT_UINT_EQ(0, dst.u32); /* Builder handle is 0. */
    ASSERT_UINT_EQ(0, staging.target);
    ASSERT_UINT_EQ(0, staging.key);
    ASSERT_UINT_EQ(0, staging.hints);
    return 0;
}

/* Test: target_entity sets entity ID */
static int test_target_entity(void) {
    aegis_state_update_t staging;
    memset(&staging, 0, sizeof(staging));

    aegis_register_t entity_reg;
    memset(&entity_reg, 0, sizeof(entity_reg));
    entity_reg.u32 = 42;

    ASSERT_TRUE(aegis_op_target_entity(&staging, &entity_reg));
    ASSERT_UINT_EQ(42, staging.target);
    return 0;
}

/* Test: set_field with KEY_POS writes vec3 */
static int test_set_field_pos(void) {
    aegis_state_update_t staging;
    memset(&staging, 0, sizeof(staging));

    aegis_register_t val;
    memset(&val, 0, sizeof(val));
    val.vec3[0] = 1.0f;
    val.vec3[1] = 2.0f;
    val.vec3[2] = 3.0f;

    ASSERT_TRUE(aegis_op_set_field(&staging, SCRIPT_KEY_POS, &val));
    ASSERT_UINT_EQ(SCRIPT_KEY_POS, staging.key);
    ASSERT_UINT_EQ(SCRIPT_ATTR_VEC3, staging.type);
    ASSERT_UINT_EQ(12, staging.size);

    float *v = (float *)staging.value;
    ASSERT_FLOAT_EQ(1.0f, v[0]);
    ASSERT_FLOAT_EQ(2.0f, v[1]);
    ASSERT_FLOAT_EQ(3.0f, v[2]);
    return 0;
}

/* Test: set_field with KEY_TYPE writes u32 */
static int test_set_field_type(void) {
    aegis_state_update_t staging;
    memset(&staging, 0, sizeof(staging));

    aegis_register_t val;
    memset(&val, 0, sizeof(val));
    val.u32 = 7;

    ASSERT_TRUE(aegis_op_set_field(&staging, SCRIPT_KEY_TYPE, &val));
    ASSERT_UINT_EQ(SCRIPT_KEY_TYPE, staging.key);
    ASSERT_UINT_EQ(SCRIPT_ATTR_U32, staging.type);
    ASSERT_UINT_EQ(4, staging.size);

    uint32_t *p = (uint32_t *)staging.value;
    ASSERT_UINT_EQ(7, *p);
    return 0;
}

/* Test: set_field with KEY_BODY_IDX writes u32 */
static int test_set_field_body_idx(void) {
    aegis_state_update_t staging;
    memset(&staging, 0, sizeof(staging));

    aegis_register_t val;
    memset(&val, 0, sizeof(val));
    val.u32 = 99;

    ASSERT_TRUE(aegis_op_set_field(&staging, SCRIPT_KEY_BODY_IDX, &val));
    ASSERT_UINT_EQ(SCRIPT_ATTR_U32, staging.type);
    ASSERT_UINT_EQ(4, staging.size);
    return 0;
}

/* Test: set_field with user key defaults to full register copy */
static int test_set_field_user_key(void) {
    aegis_state_update_t staging;
    memset(&staging, 0, sizeof(staging));

    aegis_register_t val;
    memset(&val, 0, sizeof(val));
    val.f32 = 3.14f;

    ASSERT_TRUE(aegis_op_set_field(&staging, SCRIPT_KEY_USER + 5, &val));
    ASSERT_UINT_EQ(SCRIPT_KEY_USER + 5, staging.key);
    /* User keys default to BLOB type, full 16-byte copy. */
    ASSERT_UINT_EQ(16, staging.size);
    return 0;
}

/* Test: add_hint sets flags */
static int test_add_hint_single(void) {
    aegis_state_update_t staging;
    memset(&staging, 0, sizeof(staging));

    ASSERT_TRUE(aegis_op_add_hint(&staging, AEGIS_HINT_MOVEMENT));
    ASSERT_UINT_EQ(AEGIS_HINT_MOVEMENT, staging.hints);
    return 0;
}

/* Test: add_hint accumulates multiple flags */
static int test_add_hint_multiple(void) {
    aegis_state_update_t staging;
    memset(&staging, 0, sizeof(staging));

    aegis_op_add_hint(&staging, AEGIS_HINT_MOVEMENT);
    aegis_op_add_hint(&staging, AEGIS_HINT_AUTHORITY);

    ASSERT_UINT_EQ(AEGIS_HINT_MOVEMENT | AEGIS_HINT_AUTHORITY, staging.hints);
    return 0;
}

/* Test: push_update appends to set */
static int test_push_update(void) {
    aegis_update_set_t set;
    ASSERT_TRUE(make_update_set(&set, 16));

    aegis_state_update_t staging;
    memset(&staging, 0, sizeof(staging));
    staging.target = 10;
    staging.key = SCRIPT_KEY_POS;
    staging.type = SCRIPT_ATTR_VEC3;
    staging.size = 12;
    float pos[3] = {1.0f, 2.0f, 3.0f};
    memcpy(staging.value, pos, 12);

    ASSERT_TRUE(aegis_op_push_update(&set, &staging));
    ASSERT_UINT_EQ(1, set.count);
    ASSERT_UINT_EQ(10, set.updates[0].target);
    ASSERT_UINT_EQ(SCRIPT_KEY_POS, set.updates[0].key);

    float *v = (float *)set.updates[0].value;
    ASSERT_FLOAT_EQ(1.0f, v[0]);
    ASSERT_FLOAT_EQ(2.0f, v[1]);
    ASSERT_FLOAT_EQ(3.0f, v[2]);

    /* Staging should be cleared after push. */
    ASSERT_UINT_EQ(0, staging.target);

    free(set.updates);
    return 0;
}

/* Test: push_update enforces capacity limit */
static int test_push_update_at_capacity(void) {
    aegis_update_set_t set;
    ASSERT_TRUE(make_update_set(&set, 2));

    aegis_state_update_t staging;
    memset(&staging, 0, sizeof(staging));
    staging.target = 1;

    ASSERT_TRUE(aegis_op_push_update(&set, &staging));
    staging.target = 2;
    ASSERT_TRUE(aegis_op_push_update(&set, &staging));

    /* Third push should fail. */
    staging.target = 3;
    ASSERT_TRUE(!aegis_op_push_update(&set, &staging));
    ASSERT_UINT_EQ(2, set.count);

    free(set.updates);
    return 0;
}

/* Test: multiple set_field calls overwrite staging (last wins per push) */
static int test_set_field_overwrite(void) {
    aegis_state_update_t staging;
    memset(&staging, 0, sizeof(staging));

    aegis_register_t val;
    memset(&val, 0, sizeof(val));
    val.vec3[0] = 1.0f; val.vec3[1] = 2.0f; val.vec3[2] = 3.0f;
    aegis_op_set_field(&staging, SCRIPT_KEY_POS, &val);

    /* Overwrite with ROT. */
    val.vec3[0] = 90.0f; val.vec3[1] = 0.0f; val.vec3[2] = 45.0f;
    aegis_op_set_field(&staging, SCRIPT_KEY_ROT, &val);

    /* Staging now has ROT, not POS. */
    ASSERT_UINT_EQ(SCRIPT_KEY_ROT, staging.key);
    float *v = (float *)staging.value;
    ASSERT_FLOAT_EQ(90.0f, v[0]);
    return 0;
}

/* Test: full pipeline: build → target → set_field → add_hint → push */
static int test_full_pipeline(void) {
    aegis_update_set_t set;
    ASSERT_TRUE(make_update_set(&set, 8));

    aegis_register_t dst;
    aegis_state_update_t staging;

    /* Build update. */
    aegis_op_build_update(&dst, &staging);

    /* Target entity 42. */
    aegis_register_t entity_reg;
    memset(&entity_reg, 0, sizeof(entity_reg));
    entity_reg.u32 = 42;
    aegis_op_target_entity(&staging, &entity_reg);

    /* Set position. */
    aegis_register_t pos;
    memset(&pos, 0, sizeof(pos));
    pos.vec3[0] = 10.0f; pos.vec3[1] = 20.0f; pos.vec3[2] = 30.0f;
    aegis_op_set_field(&staging, SCRIPT_KEY_POS, &pos);

    /* Add hints. */
    aegis_op_add_hint(&staging, AEGIS_HINT_MOVEMENT);
    aegis_op_add_hint(&staging, AEGIS_HINT_PREDICTION);

    /* Push. */
    ASSERT_TRUE(aegis_op_push_update(&set, &staging));

    /* Verify. */
    ASSERT_UINT_EQ(1, set.count);
    ASSERT_UINT_EQ(42, set.updates[0].target);
    ASSERT_UINT_EQ(SCRIPT_KEY_POS, set.updates[0].key);
    ASSERT_UINT_EQ(AEGIS_HINT_MOVEMENT | AEGIS_HINT_PREDICTION,
                   set.updates[0].hints);

    float *v = (float *)set.updates[0].value;
    ASSERT_FLOAT_EQ(10.0f, v[0]);
    ASSERT_FLOAT_EQ(20.0f, v[1]);
    ASSERT_FLOAT_EQ(30.0f, v[2]);

    free(set.updates);
    return 0;
}

/* Test: multiple entities in one update set */
static int test_multiple_entities(void) {
    aegis_update_set_t set;
    ASSERT_TRUE(make_update_set(&set, 8));

    aegis_register_t dst;
    aegis_state_update_t staging;

    /* First entity. */
    aegis_op_build_update(&dst, &staging);
    aegis_register_t ent1;
    memset(&ent1, 0, sizeof(ent1));
    ent1.u32 = 10;
    aegis_op_target_entity(&staging, &ent1);
    aegis_register_t pos1;
    memset(&pos1, 0, sizeof(pos1));
    pos1.vec3[0] = 1.0f;
    aegis_op_set_field(&staging, SCRIPT_KEY_POS, &pos1);
    ASSERT_TRUE(aegis_op_push_update(&set, &staging));

    /* Second entity. */
    aegis_op_build_update(&dst, &staging);
    aegis_register_t ent2;
    memset(&ent2, 0, sizeof(ent2));
    ent2.u32 = 20;
    aegis_op_target_entity(&staging, &ent2);
    aegis_register_t pos2;
    memset(&pos2, 0, sizeof(pos2));
    pos2.vec3[0] = 5.0f;
    aegis_op_set_field(&staging, SCRIPT_KEY_POS, &pos2);
    ASSERT_TRUE(aegis_op_push_update(&set, &staging));

    ASSERT_UINT_EQ(2, set.count);
    ASSERT_UINT_EQ(10, set.updates[0].target);
    ASSERT_UINT_EQ(20, set.updates[1].target);

    free(set.updates);
    return 0;
}

/* ======================================================================= */
/* VM integration tests                                                     */
/* ======================================================================= */

/* Test: build_update → target → set_field → push via IL */
static int test_vm_update_pipeline(void) {
    const char *src =
        ".topic !test\n"
        "load_imm r1 42\n"           /* entity id */
        "build_update r0\n"
        "target_entity r0 r1\n"
        "load_imm r2 0\n"            /* prepare vec3 pos */
        "i32_to_f32 r2 r2\n"
        "set_field r0 0 r2\n"        /* key=0 = KEY_POS */
        "add_hint r0 1\n"            /* AEGIS_HINT_MOVEMENT = 1 */
        "push_update r0\n"
        "exit 0\n";

    aegis_bytecode_t bc;
    ASSERT_TRUE(compile_il(src, &bc));

    uint8_t arena[4096];
    aegis_config_t cfg = aegis_config_default();
    cfg.fuel_budget = 10000;
    aegis_vm_t vm;
    ASSERT_TRUE(aegis_vm_init(&vm, &bc, &cfg, arena, sizeof(arena)));

    /* Attach update set. */
    aegis_update_set_t set;
    ASSERT_TRUE(make_update_set(&set, 16));
    vm.update_set = &set;

    aegis_event_t ev = {0};
    vm.event = &ev;

    aegis_vm_status_t status = aegis_vm_run(&vm);
    ASSERT_TRUE(status == AEGIS_VM_EXITED);
    ASSERT_UINT_EQ(1, set.count);
    ASSERT_UINT_EQ(42, set.updates[0].target);
    ASSERT_UINT_EQ(SCRIPT_KEY_POS, set.updates[0].key);
    ASSERT_UINT_EQ(AEGIS_HINT_MOVEMENT, set.updates[0].hints);

    free(set.updates);
    free(bc.instructions);
    return 0;
}

/* Test: push_update overflow via IL produces VM error */
static int test_vm_push_update_overflow(void) {
    /* Build and push 3 updates into a set of capacity 2 → error on 3rd. */
    const char *src =
        ".topic !test\n"
        "load_imm r1 1\n"
        "build_update r0\n"
        "target_entity r0 r1\n"
        "set_field r0 4 r1\n"        /* key=4=KEY_TYPE */
        "push_update r0\n"
        "load_imm r1 2\n"
        "build_update r0\n"
        "target_entity r0 r1\n"
        "set_field r0 4 r1\n"
        "push_update r0\n"
        "load_imm r1 3\n"
        "build_update r0\n"
        "target_entity r0 r1\n"
        "set_field r0 4 r1\n"
        "push_update r0\n"           /* Should fail: capacity 2 */
        "exit 0\n";

    aegis_bytecode_t bc;
    ASSERT_TRUE(compile_il(src, &bc));

    uint8_t arena[4096];
    aegis_config_t cfg = aegis_config_default();
    cfg.fuel_budget = 10000;
    aegis_vm_t vm;
    ASSERT_TRUE(aegis_vm_init(&vm, &bc, &cfg, arena, sizeof(arena)));

    aegis_update_set_t set;
    ASSERT_TRUE(make_update_set(&set, 2));
    vm.update_set = &set;

    aegis_event_t ev = {0};
    vm.event = &ev;

    aegis_vm_status_t status = aegis_vm_run(&vm);
    ASSERT_TRUE(status == AEGIS_VM_ERROR);
    ASSERT_UINT_EQ(2, set.count); /* Only 2 succeeded. */

    free(set.updates);
    free(bc.instructions);
    return 0;
}

/* ======================================================================= */
/* Main                                                                     */
/* ======================================================================= */

int main(void) {
    printf("=== Aegis Update Ops Tests ===\n\n");

    /* Direct handler tests */
    RUN(test_build_update);
    RUN(test_target_entity);
    RUN(test_set_field_pos);
    RUN(test_set_field_type);
    RUN(test_set_field_body_idx);
    RUN(test_set_field_user_key);
    RUN(test_add_hint_single);
    RUN(test_add_hint_multiple);
    RUN(test_push_update);
    RUN(test_push_update_at_capacity);
    RUN(test_set_field_overwrite);
    RUN(test_full_pipeline);
    RUN(test_multiple_entities);

    /* VM integration tests */
    RUN(test_vm_update_pipeline);
    RUN(test_vm_push_update_overflow);

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
