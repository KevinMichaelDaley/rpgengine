/**
 * @file cmd_pivot_id_tests.c
 * @brief Tests for cmd_pivot_id — set pivot_offset and adjust pos.
 */

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "ferrum/editor/edit_commands.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_entity_pivot.h"
#include "ferrum/editor/edit_selection.h"
#include "ferrum/editor/edit_dispatch.h"
#include "ferrum/editor/json_parse.h"
#include "ferrum/math/quat.h"

static int tests_run = 0;
static int tests_passed = 0;

#define RUN(fn) do { \
    printf("RUN  %s\n", #fn); \
    tests_run++; \
    if (fn()) { tests_passed++; printf("OK   %s\n", #fn); } \
    else { printf("FAIL %s\n", #fn); } \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { printf("  ASSERT FAILED: %s (line %d)\n", #cond, __LINE__); return 0; } \
} while(0)

#define ASSERT_NEAR(a, b, eps) do { \
    if (fabsf((a)-(b)) > (eps)) { \
        printf("  ASSERT_NEAR FAILED: %g != %g (line %d)\n", \
               (double)(a), (double)(b), __LINE__); return 0; } \
} while(0)

static edit_entity_store_t store;
static edit_selection_t sel;
static edit_dispatch_t dispatch;
static edit_cmd_ctx_t ctx;

static void setup(void) {
    edit_entity_store_init(&store, 64);
    edit_selection_init(&sel);
    memset(&dispatch, 0, sizeof(dispatch));
    memset(&ctx, 0, sizeof(ctx));
    ctx.entities = &store;
    ctx.selection = &sel;
    dispatch.user_data = &ctx;
}

static void teardown(void) {
    edit_entity_store_destroy(&store);
    edit_selection_destroy(&sel);
}

/** Set pivot on entity with identity transform; pos adjusts to keep geometry. */
static int test_pivot_adjusts_pos(void) {
    setup();
    uint32_t eid = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_BOX);
    edit_entity_t *e = edit_entity_store_get_mut(&store, eid);
    e->pos[0] = 0; e->pos[1] = 0; e->pos[2] = 0;
    e->scale[0] = 1; e->scale[1] = 1; e->scale[2] = 1;
    e->orientation = (quat_t){0, 0, 0, 1};

    char json[] = "{\"entity_id\":0,\"pivot\":[1,0,0]}";
    char arena_buf[4096];
    json_arena_t arena;
    json_arena_init(&arena, arena_buf, sizeof(arena_buf));
    json_value_t val;
    ASSERT(json_parse(json, strlen(json), &arena, &val));
    json_value_t result = {0};
    ASSERT(cmd_pivot_id(&dispatch, &val, &result, &arena));

    ASSERT_NEAR(e->pivot_offset[0], 1.0f, 1e-5f);
    ASSERT_NEAR(e->pivot_offset[1], 0.0f, 1e-5f);
    ASSERT_NEAR(e->pivot_offset[2], 0.0f, 1e-5f);
    ASSERT_NEAR(e->pos[0], 1.0f, 1e-5f);
    ASSERT_NEAR(e->pos[1], 0.0f, 1e-5f);
    ASSERT_NEAR(e->pos[2], 0.0f, 1e-5f);

    float geo[3];
    edit_entity_geometry_center(e, geo);
    ASSERT_NEAR(geo[0], 0.0f, 1e-5f);
    ASSERT_NEAR(geo[1], 0.0f, 1e-5f);
    ASSERT_NEAR(geo[2], 0.0f, 1e-5f);

    teardown();
    return 1;
}

/** Set pivot on entity with rotation; geometry center stays fixed. */
static int test_pivot_with_rotation(void) {
    setup();
    uint32_t eid = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_BOX);
    edit_entity_t *e = edit_entity_store_get_mut(&store, eid);
    e->pos[0] = 5; e->pos[1] = 0; e->pos[2] = 0;
    e->scale[0] = 1; e->scale[1] = 1; e->scale[2] = 1;
    float half = 3.14159265358979323846f / 4.0f;
    e->orientation = (quat_t){0, sinf(half), 0, cosf(half)};

    float geo_before[3];
    edit_entity_geometry_center(e, geo_before);

    char json[] = "{\"entity_id\":0,\"pivot\":[1,0,0]}";
    char arena_buf[4096];
    json_arena_t arena;
    json_arena_init(&arena, arena_buf, sizeof(arena_buf));
    json_value_t val;
    ASSERT(json_parse(json, strlen(json), &arena, &val));
    json_value_t result = {0};
    ASSERT(cmd_pivot_id(&dispatch, &val, &result, &arena));

    float geo_after[3];
    edit_entity_geometry_center(e, geo_after);
    ASSERT_NEAR(geo_after[0], geo_before[0], 0.01f);
    ASSERT_NEAR(geo_after[1], geo_before[1], 0.01f);
    ASSERT_NEAR(geo_after[2], geo_before[2], 0.01f);

    teardown();
    return 1;
}

/** Invalid entity ID returns false. */
static int test_pivot_invalid_entity(void) {
    setup();
    char json[] = "{\"entity_id\":999,\"pivot\":[1,2,3]}";
    char arena_buf[4096];
    json_arena_t arena;
    json_arena_init(&arena, arena_buf, sizeof(arena_buf));
    json_value_t val;
    ASSERT(json_parse(json, strlen(json), &arena, &val));
    json_value_t result = {0};
    ASSERT(!cmd_pivot_id(&dispatch, &val, &result, &arena));
    teardown();
    return 1;
}

/** Missing pivot arg returns false. */
static int test_pivot_missing_arg(void) {
    setup();
    edit_entity_store_create(&store, EDIT_ENTITY_TYPE_BOX);
    char json[] = "{\"entity_id\":0}";
    char arena_buf[4096];
    json_arena_t arena;
    json_arena_init(&arena, arena_buf, sizeof(arena_buf));
    json_value_t val;
    ASSERT(json_parse(json, strlen(json), &arena, &val));
    json_value_t result = {0};
    ASSERT(!cmd_pivot_id(&dispatch, &val, &result, &arena));
    teardown();
    return 1;
}

int main(void) {
    RUN(test_pivot_adjusts_pos);
    RUN(test_pivot_with_rotation);
    RUN(test_pivot_invalid_entity);
    RUN(test_pivot_missing_arg);

    printf("\n%d passed, %d failed\n", tests_passed, tests_run - tests_passed);
    return (tests_passed == tests_run) ? 0 : 1;
}
