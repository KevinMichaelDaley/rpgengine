#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "ferrum/procgen/procgen_srd_types.h"
#include "ferrum/procgen/procgen_srd_grammar.h"

static int g_pass = 0;
static int g_fail = 0;

#define RUN(fn) do { printf("RUN  %s\n", #fn); fn(); printf("OK   %s\n\n", #fn); } while (0)
#define ASSERT_TRUE(expr) do { if (!(expr)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); g_fail++; return; } } while (0)
#define ASSERT_INT_EQ(a, b) ASSERT_TRUE((int)(a) == (int)(b))
#define ASSERT_FLOAT_EQ(a, b, eps) ASSERT_TRUE(fabsf((a)-(b)) <= (eps))
#define PASS() g_pass++

/* ── Test helper: create room array ── */

static fr_room_box_t *make_room(float cx, float cz, float hx, float hz, char type) {
    fr_room_box_t *r = (fr_room_box_t *)calloc(1, sizeof(fr_room_box_t));
    fr_room_box_init(r);
    r->center_x = cx; r->center_z = cz;
    r->half_extent_x = hx; r->half_extent_z = hz;
    r->floor_z = 0; r->ceil_z = 4;
    r->type_char = type;
    return r;
}

static void test_split_room_increases_count(void) {
    fr_room_box_t *rooms[8];
    rooms[0] = make_room(10, 5, 8, 6, 'R');

    fr_rewrite_proposal_t prop;
    memset(&prop, 0, sizeof(prop));
    prop.type = FR_REWRITE_SPLIT_ROOM;
    prop.element_indices[0] = 0;
    prop.param_float[0] = 0.0f;  /* X axis */
    prop.param_float[1] = 0.5f;  /* half */

    uint32_t count = 1;
    int rc = procgen_srd_apply_rewrite((void **)rooms, &count, 8, &prop);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_INT_EQ(count, 2);
    /* Both halves should still be type R */
    ASSERT_INT_EQ(rooms[0]->type_char, 'R');
    ASSERT_INT_EQ(rooms[1]->type_char, 'R');
    /* X half-extents should be ~4 (half of 8) */
    ASSERT_FLOAT_EQ(rooms[0]->half_extent_x, 4.0f, 0.01f);
    ASSERT_FLOAT_EQ(rooms[1]->half_extent_x, 4.0f, 0.01f);

    for (int i = 0; i < 2; i++) { free(rooms[i]); rooms[i] = NULL; }
    PASS();
}

static void test_split_z_axis(void) {
    fr_room_box_t *rooms[8];
    rooms[0] = make_room(10, 5, 4, 10, 'B');

    fr_rewrite_proposal_t prop;
    memset(&prop, 0, sizeof(prop));
    prop.type = FR_REWRITE_SPLIT_ROOM;
    prop.element_indices[0] = 0;
    prop.param_float[0] = 1.0f;  /* Z axis */
    prop.param_float[1] = 0.5f;

    uint32_t count = 1;
    int rc = procgen_srd_apply_rewrite((void **)rooms, &count, 8, &prop);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_INT_EQ(count, 2);
    ASSERT_FLOAT_EQ(rooms[0]->half_extent_z, 5.0f, 0.01f);
    ASSERT_FLOAT_EQ(rooms[1]->half_extent_z, 5.0f, 0.01f);

    for (int i = 0; i < 2; i++) { free(rooms[i]); rooms[i] = NULL; }
    PASS();
}

static void test_merge_rooms(void) {
    fr_room_box_t *rooms[8];
    rooms[0] = make_room(0, 0, 2, 2, 'R');
    rooms[1] = make_room(6, 0, 2, 2, 'R');

    fr_rewrite_proposal_t prop;
    memset(&prop, 0, sizeof(prop));
    prop.type = FR_REWRITE_MERGE_ROOMS;
    prop.element_indices[0] = 0;
    prop.element_indices[1] = 1;

    uint32_t count = 2;
    int rc = procgen_srd_apply_rewrite((void **)rooms, &count, 8, &prop);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_INT_EQ(count, 1);
    /* Merged room should cover bounding box of both */
    ASSERT_FLOAT_EQ(rooms[0]->center_x, 3.0f, 0.01f);  /* midpoint of 0 and 6 */
    ASSERT_FLOAT_EQ(rooms[0]->half_extent_x, 5.0f, 0.01f);  /* 3 to left edge 0, 3 to right edge 6 */

    free(rooms[0]); rooms[0] = NULL;
    PASS();
}

static void test_remove_room(void) {
    fr_room_box_t *rooms[8];
    rooms[0] = make_room(0, 0, 2, 2, 'R');
    rooms[1] = make_room(10, 10, 2, 2, 'P');
    rooms[2] = make_room(20, 20, 2, 2, 'R');

    fr_rewrite_proposal_t prop;
    memset(&prop, 0, sizeof(prop));
    prop.type = FR_REWRITE_REMOVE_ROOM;
    prop.element_indices[0] = 1;  /* remove rooms[1] */

    uint32_t count = 3;
    int rc = procgen_srd_apply_rewrite((void **)rooms, &count, 8, &prop);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_INT_EQ(count, 2);
    /* rooms[1] is gone; rooms[0] and rooms[2] shifted */
    ASSERT_INT_EQ(rooms[0]->type_char, 'R');
    ASSERT_INT_EQ(rooms[1]->type_char, 'R');  /* was rooms[2] */

    free(rooms[0]); free(rooms[1]); rooms[0] = rooms[1] = NULL;
    PASS();
}

int main(void) {
    printf("=== Rewrite Engine Tests ===\n\n");

    RUN(test_split_room_increases_count);
    RUN(test_split_z_axis);
    RUN(test_merge_rooms);
    RUN(test_remove_room);

    printf("=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
