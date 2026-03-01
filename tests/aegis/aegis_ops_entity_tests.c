/**
 * @file aegis_ops_entity_tests.c
 * @brief Tests for Aegis entity query instructions: query_entity, get_attr,
 *        entity_count, entity_at.
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
#include "ferrum/aegis/aegis_ops_entity.h"
#include "ferrum/editor/edit_script_env.h"
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

#define ASSERT_INT_EQ(expected, actual) do { \
    int _e = (expected), _a = (actual); \
    if (_e != _a) { \
        printf("  FAIL %s:%d: expected %d, got %d\n", \
               __FILE__, __LINE__, _e, _a); \
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

/** Build a test snapshot with N entities. */
static void build_test_view(script_entity_snapshot_t *snaps,
                            script_entity_view_t *view,
                            uint32_t count) {
    memset(snaps, 0, count * sizeof(*snaps));
    for (uint32_t i = 0; i < count; i++) {
        snaps[i].entity_id = 100 + i;
        snaps[i].active    = 1;
        snaps[i].type      = 3;
        snaps[i].pos[0]    = 1.0f + (float)i;
        snaps[i].pos[1]    = 2.0f + (float)i;
        snaps[i].pos[2]    = 3.0f + (float)i;
        snaps[i].rot[0]    = 10.0f * (float)i;
        snaps[i].rot[1]    = 20.0f * (float)i;
        snaps[i].rot[2]    = 30.0f * (float)i;
        snaps[i].scale[0]  = 1.0f;
        snaps[i].scale[1]  = 1.0f;
        snaps[i].scale[2]  = 1.0f;
        snaps[i].body_index = 50 + i;
    }
    view->entities = snaps;
    view->count    = count;
    view->capacity = count;
}

/** Compile IL source into bytecode. Caller must free bc->instructions. */
static bool compile_il(const char *src, aegis_bytecode_t *bc) {
    aegis_asm_t as;
    memset(&as, 0, sizeof(as));
    uint32_t len = (uint32_t)strlen(src);
    return aegis_asm_compile(&as, src, len, bc);
}

/** Create a VM with entity view attached. */
static bool setup_vm(aegis_vm_t *vm, const aegis_bytecode_t *bc,
                     uint8_t *arena, uint32_t arena_sz,
                     const script_entity_view_t *view) {
    aegis_config_t cfg = aegis_config_default();
    cfg.fuel_budget = 10000;
    if (!aegis_vm_init(vm, bc, &cfg, arena, arena_sz)) {
        return false;
    }
    vm->entity_view = view;
    return true;
}

/* ======================================================================= */
/* Direct handler tests                                                     */
/* ======================================================================= */

/* Test: query_entity finds entity by ID */
static int test_query_entity_found(void) {
    script_entity_snapshot_t snaps[4];
    script_entity_view_t view;
    build_test_view(snaps, &view, 4);

    aegis_register_t dst;
    /* Query for entity_id 102 (index 2). */
    aegis_register_t id_reg;
    memset(&id_reg, 0, sizeof(id_reg));
    id_reg.u32 = 102;

    ASSERT_TRUE(aegis_op_query_entity(&dst, &id_reg, &view));
    /* Should return handle (snapshot index). */
    ASSERT_INT_EQ(2, dst.i32);
    return 0;
}

/* Test: query_entity returns -1 for missing entity */
static int test_query_entity_not_found(void) {
    script_entity_snapshot_t snaps[4];
    script_entity_view_t view;
    build_test_view(snaps, &view, 4);

    aegis_register_t dst;
    aegis_register_t id_reg;
    memset(&id_reg, 0, sizeof(id_reg));
    id_reg.u32 = 999;

    ASSERT_TRUE(aegis_op_query_entity(&dst, &id_reg, &view));
    ASSERT_INT_EQ(-1, dst.i32);
    return 0;
}

/* Test: query_entity with NULL view returns false */
static int test_query_entity_null_view(void) {
    aegis_register_t dst, id_reg;
    memset(&id_reg, 0, sizeof(id_reg));
    id_reg.u32 = 1;

    ASSERT_TRUE(!aegis_op_query_entity(&dst, &id_reg, NULL));
    return 0;
}

/* Test: get_attr reads well-known KEY_POS as vec3 */
static int test_get_attr_pos(void) {
    script_entity_snapshot_t snaps[2];
    script_entity_view_t view;
    build_test_view(snaps, &view, 2);

    aegis_register_t dst;
    /* Handle = 1 (second entity), key = KEY_POS. */
    ASSERT_TRUE(aegis_op_get_attr(&dst, 1, SCRIPT_KEY_POS, &view));
    ASSERT_FLOAT_EQ(2.0f, dst.vec3[0]);
    ASSERT_FLOAT_EQ(3.0f, dst.vec3[1]);
    ASSERT_FLOAT_EQ(4.0f, dst.vec3[2]);
    return 0;
}

/* Test: get_attr reads KEY_ROT as vec3 */
static int test_get_attr_rot(void) {
    script_entity_snapshot_t snaps[2];
    script_entity_view_t view;
    build_test_view(snaps, &view, 2);

    aegis_register_t dst;
    ASSERT_TRUE(aegis_op_get_attr(&dst, 0, SCRIPT_KEY_ROT, &view));
    ASSERT_FLOAT_EQ(0.0f, dst.vec3[0]);
    ASSERT_FLOAT_EQ(0.0f, dst.vec3[1]);
    ASSERT_FLOAT_EQ(0.0f, dst.vec3[2]);
    return 0;
}

/* Test: get_attr reads KEY_SCALE as vec3 */
static int test_get_attr_scale(void) {
    script_entity_snapshot_t snaps[2];
    script_entity_view_t view;
    build_test_view(snaps, &view, 2);

    aegis_register_t dst;
    ASSERT_TRUE(aegis_op_get_attr(&dst, 0, SCRIPT_KEY_SCALE, &view));
    ASSERT_FLOAT_EQ(1.0f, dst.vec3[0]);
    ASSERT_FLOAT_EQ(1.0f, dst.vec3[1]);
    ASSERT_FLOAT_EQ(1.0f, dst.vec3[2]);
    return 0;
}

/* Test: get_attr reads KEY_TYPE as u32 */
static int test_get_attr_type(void) {
    script_entity_snapshot_t snaps[2];
    script_entity_view_t view;
    build_test_view(snaps, &view, 2);

    aegis_register_t dst;
    ASSERT_TRUE(aegis_op_get_attr(&dst, 0, SCRIPT_KEY_TYPE, &view));
    ASSERT_UINT_EQ(3, dst.u32);
    return 0;
}

/* Test: get_attr reads KEY_BODY_IDX as u32 */
static int test_get_attr_body_idx(void) {
    script_entity_snapshot_t snaps[2];
    script_entity_view_t view;
    build_test_view(snaps, &view, 2);

    aegis_register_t dst;
    ASSERT_TRUE(aegis_op_get_attr(&dst, 1, SCRIPT_KEY_BODY_IDX, &view));
    ASSERT_UINT_EQ(51, dst.u32);
    return 0;
}

/* Test: get_attr with out-of-range handle returns false */
static int test_get_attr_bad_handle(void) {
    script_entity_snapshot_t snaps[2];
    script_entity_view_t view;
    build_test_view(snaps, &view, 2);

    aegis_register_t dst;
    ASSERT_TRUE(!aegis_op_get_attr(&dst, 99, SCRIPT_KEY_POS, &view));
    return 0;
}

/* Test: get_attr with dynamic key via entity_attrs */
static int test_get_attr_dynamic_key(void) {
    script_entity_snapshot_t snaps[1];
    script_entity_view_t view;
    build_test_view(snaps, &view, 1);

    /* Set a dynamic attribute on the snapshot. */
    float val = 42.5f;
    entity_attrs_set(&snaps[0].attrs, SCRIPT_KEY_USER + 1,
                     SCRIPT_ATTR_F32, &val, sizeof(val));

    aegis_register_t dst;
    ASSERT_TRUE(aegis_op_get_attr(&dst, 0, SCRIPT_KEY_USER + 1, &view));
    ASSERT_FLOAT_EQ(42.5f, dst.f32);
    return 0;
}

/* Test: entity_count returns correct count */
static int test_entity_count(void) {
    script_entity_snapshot_t snaps[5];
    script_entity_view_t view;
    build_test_view(snaps, &view, 5);

    aegis_register_t dst;
    ASSERT_TRUE(aegis_op_entity_count(&dst, &view));
    ASSERT_UINT_EQ(5, dst.u32);
    return 0;
}

/* Test: entity_count with NULL view returns false */
static int test_entity_count_null_view(void) {
    aegis_register_t dst;
    ASSERT_TRUE(!aegis_op_entity_count(&dst, NULL));
    return 0;
}

/* Test: entity_at returns valid handle */
static int test_entity_at_valid(void) {
    script_entity_snapshot_t snaps[3];
    script_entity_view_t view;
    build_test_view(snaps, &view, 3);

    aegis_register_t dst;
    aegis_register_t idx;
    memset(&idx, 0, sizeof(idx));
    idx.u32 = 2;

    ASSERT_TRUE(aegis_op_entity_at(&dst, &idx, &view));
    /* Handle is the snapshot index itself. */
    ASSERT_UINT_EQ(2, dst.u32);
    return 0;
}

/* Test: entity_at with out-of-range index returns false */
static int test_entity_at_out_of_range(void) {
    script_entity_snapshot_t snaps[3];
    script_entity_view_t view;
    build_test_view(snaps, &view, 3);

    aegis_register_t dst;
    aegis_register_t idx;
    memset(&idx, 0, sizeof(idx));
    idx.u32 = 10;

    ASSERT_TRUE(!aegis_op_entity_at(&dst, &idx, &view));
    return 0;
}

/* ======================================================================= */
/* Integration tests (via VM interpreter)                                   */
/* ======================================================================= */

/* Test: query_entity + get_attr via IL program */
static int test_vm_query_and_get_attr(void) {
    /* Program: query entity 101, get its pos, store x in r10, exit. */
    const char *src =
        ".topic !test\n"
        "load_imm r1 101\n"       /* r1 = entity_id 101 */
        "query_entity r2 r1\n"    /* r2 = handle */
        "get_attr r3 r2 0\n"      /* r3 = pos (key=0) */
        "mov r10 r3\n"            /* r10 = pos copy */
        "exit 0\n";

    aegis_bytecode_t bc;
    ASSERT_TRUE(compile_il(src, &bc));

    script_entity_snapshot_t snaps[4];
    script_entity_view_t view;
    build_test_view(snaps, &view, 4);

    uint8_t arena[4096];
    aegis_vm_t vm;
    ASSERT_TRUE(setup_vm(&vm, &bc, arena, sizeof(arena), &view));

    /* Provide a dummy event so the VM can run. */
    aegis_event_t ev = {0};
    vm.event = &ev;

    aegis_vm_status_t status = aegis_vm_run(&vm);
    ASSERT_TRUE(status == AEGIS_VM_EXITED);

    /* Entity 101 is at index 1 → pos = (2, 3, 4). */
    ASSERT_FLOAT_EQ(2.0f, vm.regs[10].vec3[0]);
    ASSERT_FLOAT_EQ(3.0f, vm.regs[10].vec3[1]);
    ASSERT_FLOAT_EQ(4.0f, vm.regs[10].vec3[2]);

    free(bc.instructions);
    return 0;
}

/* Test: entity_count + entity_at iteration via IL */
static int test_vm_entity_iteration(void) {
    /* Program: count entities, iterate with entity_at, exit with count. */
    const char *src =
        ".topic !test\n"
        "entity_count r1\n"       /* r1 = count */
        "load_imm r2 0\n"         /* r2 = index */
        "loop:\n"
        "lt r3 r2 r1\n"           /* r3 = (index < count) */
        "jmp_if_not r3 done\n"
        "entity_at r4 r2\n"       /* r4 = handle */
        "get_attr r5 r4 4\n"      /* r5 = type (key=4=KEY_TYPE) */
        "add r2 r2 r5\n"          /* dummy accumulate (type=3, so r2 += 3) */
        "jmp loop\n"
        "done:\n"
        "mov r10 r2\n"            /* r10 = final accumulated value */
        "exit 0\n";

    aegis_bytecode_t bc;
    ASSERT_TRUE(compile_il(src, &bc));

    script_entity_snapshot_t snaps[3];
    script_entity_view_t view;
    build_test_view(snaps, &view, 3);

    uint8_t arena[4096];
    aegis_vm_t vm;
    ASSERT_TRUE(setup_vm(&vm, &bc, arena, sizeof(arena), &view));

    aegis_event_t ev = {0};
    vm.event = &ev;

    aegis_vm_status_t status = aegis_vm_run(&vm);
    ASSERT_TRUE(status == AEGIS_VM_EXITED);

    /* First iteration: index=0 < 3, type=3, r2 = 0+3 = 3.
     * Second iteration: 3 < 3 is false. Done. r10 = 3. */
    ASSERT_UINT_EQ(3, vm.regs[10].u32);

    free(bc.instructions);
    return 0;
}

/* Test: query_entity for missing entity returns -1 via IL */
static int test_vm_query_missing_entity(void) {
    const char *src =
        ".topic !test\n"
        "load_imm r1 999\n"       /* r1 = non-existent entity_id */
        "query_entity r2 r1\n"    /* r2 = -1 */
        "mov r10 r2\n"
        "exit 0\n";

    aegis_bytecode_t bc;
    ASSERT_TRUE(compile_il(src, &bc));

    script_entity_snapshot_t snaps[2];
    script_entity_view_t view;
    build_test_view(snaps, &view, 2);

    uint8_t arena[4096];
    aegis_vm_t vm;
    ASSERT_TRUE(setup_vm(&vm, &bc, arena, sizeof(arena), &view));

    aegis_event_t ev = {0};
    vm.event = &ev;

    aegis_vm_status_t status = aegis_vm_run(&vm);
    ASSERT_TRUE(status == AEGIS_VM_EXITED);
    ASSERT_INT_EQ(-1, vm.regs[10].i32);

    free(bc.instructions);
    return 0;
}

/* ======================================================================= */
/* Main                                                                     */
/* ======================================================================= */

int main(void) {
    printf("=== Aegis Entity Query Ops Tests ===\n\n");

    /* Direct handler tests */
    RUN(test_query_entity_found);
    RUN(test_query_entity_not_found);
    RUN(test_query_entity_null_view);
    RUN(test_get_attr_pos);
    RUN(test_get_attr_rot);
    RUN(test_get_attr_scale);
    RUN(test_get_attr_type);
    RUN(test_get_attr_body_idx);
    RUN(test_get_attr_bad_handle);
    RUN(test_get_attr_dynamic_key);
    RUN(test_entity_count);
    RUN(test_entity_count_null_view);
    RUN(test_entity_at_valid);
    RUN(test_entity_at_out_of_range);

    /* Integration tests */
    RUN(test_vm_query_and_get_attr);
    RUN(test_vm_entity_iteration);
    RUN(test_vm_query_missing_entity);

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
