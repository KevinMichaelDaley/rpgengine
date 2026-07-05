#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/procgen/procgen_srd_types.h"
#include "ferrum/procgen/procgen_srd_grammar.h"

static int g_pass = 0;
static int g_fail = 0;

#define RUN(fn) do { printf("RUN  %s\n", #fn); fn(); printf("OK   %s\n\n", #fn); } while (0)
#define ASSERT_TRUE(expr) do { if (!(expr)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); g_fail++; return; } } while (0)
#define ASSERT_INT_EQ(a, b) ASSERT_TRUE((int)(a) == (int)(b))
#define PASS() g_pass++

static void test_no_split_for_small_room(void) {
    fr_room_box_t room; memset(&room, 0, sizeof(room));
    room.half_extent_x = 2.0f;  /* total 4m — too small to split */
    room.half_extent_z = 2.0f;

    fr_rewrite_proposal_t props[32];
    uint32_t count = 0;
    int rc = procgen_srd_propose_rewrites(&room, NULL, 0,
                                          FR_ROOM_BOX_TYPE, 0,
                                          6.0f /* split_threshold */,
                                          props, 32, &count);
    ASSERT_INT_EQ(rc, 0);
    /* Should NOT propose any split for a small room */
    int has_split = 0;
    for (uint32_t i = 0; i < count; i++)
        if (props[i].type == FR_REWRITE_SPLIT_ROOM) has_split = 1;
    ASSERT_INT_EQ(has_split, 0);
    PASS();
}

static void test_split_proposed_for_large_room(void) {
    fr_room_box_t room; memset(&room, 0, sizeof(room));
    room.half_extent_x = 10.0f;  /* total 20m — large enough */
    room.half_extent_z = 10.0f;
    room.type_char = 'R';

    fr_rewrite_proposal_t props[32];
    uint32_t count = 0;
    int rc = procgen_srd_propose_rewrites(&room, NULL, 0,
                                          FR_ROOM_BOX_TYPE, 0,
                                          6.0f,
                                          props, 32, &count);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_TRUE(count > 0);
    /* Should propose split on at least one axis */
    int has_split = 0;
    for (uint32_t i = 0; i < count; i++)
        if (props[i].type == FR_REWRITE_SPLIT_ROOM) has_split = 1;
    ASSERT_INT_EQ(has_split, 1);
    PASS();
}

static void test_merge_proposed_for_small_adjacent(void) {
    fr_room_box_t rooms[2];
    fr_room_box_init(&rooms[0]); fr_room_box_init(&rooms[1]);
    rooms[0].half_extent_x = 1.0f; rooms[0].half_extent_z = 1.0f;  /* 2m² tiny */
    rooms[1].half_extent_x = 1.0f; rooms[1].half_extent_z = 1.0f;
    rooms[0].center_x = 0.0f; rooms[0].center_z = 0.0f;
    rooms[1].center_x = 4.0f; rooms[1].center_z = 0.0f;  /* adjacent */

    fr_rewrite_proposal_t props[64];
    uint32_t count = 0;
    int rc = procgen_srd_propose_rewrites_multiple(rooms, 2, 2.0f /* min_half */,
                                                   6.0f, props, 64, &count);
    ASSERT_INT_EQ(rc, 0);
    int has_merge = 0;
    for (uint32_t i = 0; i < count; i++)
        if (props[i].type == FR_REWRITE_MERGE_ROOMS) has_merge = 1;
    ASSERT_INT_EQ(has_merge, 1);
    PASS();
}

static void test_connect_proposed_for_adjacent(void) {
    fr_room_box_t rooms[2];
    fr_room_box_init(&rooms[0]); fr_room_box_init(&rooms[1]);
    rooms[0].half_extent_x = 4.0f; rooms[0].half_extent_z = 3.0f;
    rooms[1].half_extent_x = 4.0f; rooms[1].half_extent_z = 3.0f;
    rooms[0].center_x = 0.0f; rooms[0].center_z = 0.0f;
    rooms[1].center_x = 10.0f; rooms[1].center_z = 0.0f;

    fr_rewrite_proposal_t props[64];
    uint32_t count = 0;
    int rc = procgen_srd_propose_rewrites_multiple(rooms, 2, 2.0f, 6.0f,
                                                   props, 64, &count);
    ASSERT_INT_EQ(rc, 0);
    int has_connect = 0;
    for (uint32_t i = 0; i < count; i++)
        if (props[i].type == FR_REWRITE_ADD_CONNECTION) has_connect = 1;
    ASSERT_INT_EQ(has_connect, 1);
    PASS();
}

static void test_proposal_payload_fields(void) {
    fr_room_box_t room; memset(&room, 0, sizeof(room));
    room.half_extent_x = 10.0f; room.half_extent_z = 10.0f;
    room.type_char = 'R'; room.floor_z = 0.0f; room.ceil_z = 4.0f;

    fr_rewrite_proposal_t props[32];
    uint32_t count = 0;
    procgen_srd_propose_rewrites(&room, NULL, 0, FR_ROOM_BOX_TYPE, 0,
                                 6.0f, props, 32, &count);

    for (uint32_t i = 0; i < count; i++) {
        if (props[i].type == FR_REWRITE_SPLIT_ROOM) {
            int axis = (int)props[i].param_float[0];
            ASSERT_TRUE(axis == 0 || axis == 1);  /* 0=X, 1=Z */
            ASSERT_TRUE(props[i].param_float[1] > 0.0f
                     && props[i].param_float[1] < 1.0f);
        }
    }
    PASS();
}

int main(void) {
    printf("=== SRD Grammar Tests ===\n\n");

    RUN(test_no_split_for_small_room);
    RUN(test_split_proposed_for_large_room);
    RUN(test_merge_proposed_for_small_adjacent);
    RUN(test_connect_proposed_for_adjacent);
    RUN(test_proposal_payload_fields);

    printf("=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
