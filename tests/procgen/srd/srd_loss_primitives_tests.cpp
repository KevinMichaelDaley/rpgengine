#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include "ferrum/procgen/srd/srd_loss_primitives.h"

static int g_pass = 0;
static int g_fail = 0;

#define RUN(fn) do { printf("RUN  %s\n", #fn); fn(); printf("OK   %s\n\n", #fn); } while (0)
#define ASSERT_TRUE(expr) do { if (!(expr)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); g_fail++; return; } } while (0)
#define ASSERT_LT(a, b) ASSERT_TRUE((a) < (b))
#define ASSERT_GT(a, b) ASSERT_TRUE((a) > (b))
#define ASSERT_NEAR(a, b, eps) ASSERT_TRUE(fabs((a)-(b)) <= (eps))
#define PASS() g_pass++

static fr_room_box_t make_room(float cx, float cz, float hx, float hz) {
    fr_room_box_t r; fr_room_box_init(&r);
    r.center_x=cx; r.center_z=cz; r.half_extent_x=hx; r.half_extent_z=hz;
    r.floor_z=0; r.ceil_z=4;
    return r;
}

static void test_minimum_size_passes(void) {
    fr_room_box_t r = make_room(0,0, 6,6);
    ASSERT_NEAR(srd_loss_minimum_size(&r, 4.0), 0.0, 1e-6);
    PASS();
}

static void test_minimum_size_fails(void) {
    fr_room_box_t r = make_room(0,0, 2,2);
    ASSERT_GT(srd_loss_minimum_size(&r, 4.0), 0.0);
    PASS();
}

static void test_separation(void) {
    fr_room_box_t a = make_room(0,0, 2,2);
    fr_room_box_t b = make_room(10,0, 2,2);
    double d = srd_loss_separation(&a, &b, 5.0, 1); /* want < 5 */
    ASSERT_GT(d, 0.0);  /* centers are 10 apart, target < 5 → loss > 0 */
    PASS();
}

static void test_height_span(void) {
    fr_room_box_t r = make_room(0,0, 4,4);
    r.floor_z=0; r.ceil_z=4;
    ASSERT_NEAR(srd_loss_height_span(&r, 3.0, 10.0), 0.0, 1e-6);

    r.ceil_z = 2.0;  /* too short */
    ASSERT_GT(srd_loss_height_span(&r, 3.0, 10.0), 0.0);
    PASS();
}

static void test_stair_alignment(void) {
    ASSERT_NEAR(srd_loss_stair_alignment(5,3, 5,3), 0.0, 1e-6);
    ASSERT_GT(srd_loss_stair_alignment(5,3, 10,8), 0.0);
    PASS();
}

static void test_path_distance_empty(void) {
    fr_room_box_t rooms[2];
    rooms[0] = make_room(2, 3, 4, 4);
    rooms[1] = make_room(10, 3, 4, 4);

    double d = srd_loss_path_distance(rooms, 2, 0, 1, 20, 12);
    ASSERT_GT(d, 0.0);
    ASSERT_LT(d, 20.0);
    PASS();
}

static void test_non_penetration_zero(void) {
    fr_room_box_t rooms[2];
    rooms[0] = make_room(0, 0, 3, 3);
    rooms[1] = make_room(50, 50, 3, 3);
    ASSERT_NEAR(srd_loss_non_penetration(rooms, 2), 0.0, 1e-3);
    PASS();
}

int main(void) {
    printf("=== Loss Primitives Tests ===\n\n");

    RUN(test_minimum_size_passes);
    RUN(test_minimum_size_fails);
    RUN(test_separation);
    RUN(test_height_span);
    RUN(test_stair_alignment);
    RUN(test_path_distance_empty);
    RUN(test_non_penetration_zero);

    printf("=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
