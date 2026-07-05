#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "ferrum/procgen/procgen_srd_types.h"

static int g_pass = 0;
static int g_fail = 0;

#define RUN(fn) do { printf("RUN  %s\n", #fn); fn(); printf("OK   %s\n\n", #fn); } while (0)
#define ASSERT_TRUE(expr) do { if (!(expr)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); g_fail++; return; } } while (0)
#define ASSERT_EQ(a, b) ASSERT_TRUE((a) == (b))
#define ASSERT_INT_EQ(a, b) ASSERT_TRUE((int)(a) == (int)(b))
#define PASS() g_pass++

static void test_room_box_sizeof(void) {
    ASSERT_TRUE(sizeof(fr_room_box_t) > 0);
    PASS();
}

static void test_room_box_init(void) {
    fr_room_box_t box;
    fr_room_box_init(&box);
    ASSERT_TRUE(box.name[0] == '\0');
    ASSERT_TRUE(box.floor_z == 0.0f);
    PASS();
}

static void test_room_box_has_correct_fields(void) {
    fr_room_box_t box;
    memset(&box, 0, sizeof(box));
    box.center_x  = 10.0f;
    box.center_z  = 5.0f;
    box.half_extent_x = 4.0f;
    box.half_extent_z = 3.0f;
    box.floor_z   = 0.0f;
    box.ceil_z    = 4.0f;
    box.material  = 1;
    box.type_char = 'R';
    strcpy(box.name, "test_room");
    ASSERT_EQ(box.center_x, 10.0f);
    ASSERT_EQ(box.half_extent_x, 4.0f);
    ASSERT_EQ(box.type_char, 'R');
    ASSERT_EQ(box.material, 1);
    PASS();
}

static void test_corridor_seg_sizeof(void) {
    ASSERT_TRUE(sizeof(fr_corridor_seg_t) > 0);
    PASS();
}

static void test_corridor_seg_init(void) {
    fr_corridor_seg_t corr;
    memset(&corr, 0, sizeof(corr));
    corr.from_x = 0.0f;
    corr.from_z = 2.0f;
    corr.to_x   = 10.0f;
    corr.to_z   = 2.0f;
    corr.width  = 3.0f;
    corr.floor_z = 0.0f;
    corr.ceil_z  = 4.0f;
    ASSERT_EQ(corr.from_x, 0.0f);
    ASSERT_EQ(corr.width, 3.0f);
    PASS();
}

static void test_stair_def_sizeof(void) {
    ASSERT_TRUE(sizeof(fr_stair_def_t) > 0);
    PASS();
}

static void test_stair_def_fields(void) {
    fr_stair_def_t stair;
    memset(&stair, 0, sizeof(stair));
    stair.anchor_x    = 5.0f;
    stair.anchor_z    = 3.0f;
    stair.direction   = FR_STAIR_UP;
    stair.n_steps     = 20;
    stair.step_h      = 0.25f;
    stair.step_d      = 0.5f;
    stair.floor_from  = 0.0f;
    stair.floor_to    = 5.0f;
    ASSERT_EQ(stair.direction, FR_STAIR_UP);
    ASSERT_INT_EQ(stair.n_steps, 20);
    ASSERT_EQ(stair.step_h, 0.25f);
    PASS();
}

static void test_floor_def_init_and_destroy(void) {
    fr_floor_def_t floor;
    fr_floor_def_init(&floor);

    ASSERT_INT_EQ(floor.room_count, 0);
    ASSERT_TRUE(floor.rooms == NULL);
    ASSERT_INT_EQ(floor.corridor_count, 0);
    ASSERT_TRUE(floor.corridors == NULL);

    fr_floor_def_destroy(&floor);
    PASS();
}

static void test_room_graph_init_and_destroy(void) {
    fr_room_graph_t graph;
    fr_room_graph_init(&graph);

    ASSERT_INT_EQ(graph.node_count, 0);
    ASSERT_INT_EQ(graph.edge_count, 0);
    ASSERT_INT_EQ(graph.stair_pair_count, 0);

    fr_room_graph_destroy(&graph);
    PASS();
}

static void test_loss_primitive_enum_values(void) {
    ASSERT_INT_EQ(FR_LOSS_PATH_DISTANCE, 0);
    ASSERT_INT_EQ(FR_LOSS_LINE_OF_SIGHT, 1);
    ASSERT_INT_EQ(FR_LOSS_NON_PENETRATION, 2);
    ASSERT_INT_EQ(FR_LOSS_MINIMUM_SIZE, 3);
    ASSERT_INT_EQ(FR_LOSS_SEPARATION, 4);
    ASSERT_INT_EQ(FR_LOSS_CONTAINMENT, 5);
    ASSERT_INT_EQ(FR_LOSS_ADJACENCY_COUNT, 6);
    ASSERT_INT_EQ(FR_LOSS_HEIGHT_SPAN, 7);
    ASSERT_INT_EQ(FR_LOSS_STAIR_ALIGNMENT, 8);
    ASSERT_INT_EQ(FR_LOSS_FLOOR_ACCESSIBILITY, 9);
    PASS();
}

int main(void) {
    printf("=== SRD Types Tests ===\n\n");

    RUN(test_room_box_sizeof);
    RUN(test_room_box_init);
    RUN(test_room_box_has_correct_fields);
    RUN(test_corridor_seg_sizeof);
    RUN(test_corridor_seg_init);
    RUN(test_stair_def_sizeof);
    RUN(test_stair_def_fields);
    RUN(test_floor_def_init_and_destroy);
    RUN(test_room_graph_init_and_destroy);
    RUN(test_loss_primitive_enum_values);

    printf("=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
