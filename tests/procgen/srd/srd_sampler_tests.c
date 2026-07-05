#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "ferrum/procgen/procgen_srd_types.h"
#include "ferrum/procgen/srd/srd_sampler.h"

static int g_pass = 0;
static int g_fail = 0;

#define RUN(fn) do { printf("RUN  %s\n", #fn); fn(); printf("OK   %s\n\n", #fn); } while (0)
#define ASSERT_TRUE(expr) do { if (!(expr)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); g_fail++; return; } } while (0)
#define ASSERT_INT_EQ(a, b) ASSERT_TRUE((int)(a) == (int)(b))
#define PASS() g_pass++

static void test_sample_count(void) {
    fr_room_box_t room; fr_room_box_init(&room);
    room.center_x = 10; room.center_z = 5;
    room.half_extent_x = 4; room.half_extent_z = 3;
    room.floor_z = 0; room.ceil_z = 4;

    srd_sample_point_t pts[128];
    uint32_t count = 0;
    int rc = srd_sample_room(&room, 4, pts, 128, &count);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_INT_EQ(count, 64);  /* 4^3 = 64 */
    PASS();
}

static void test_samples_within_bounds(void) {
    fr_room_box_t room; fr_room_box_init(&room);
    room.center_x = 10; room.center_z = 5;
    room.half_extent_x = 4; room.half_extent_z = 3;
    room.floor_z = 0; room.ceil_z = 4;

    srd_sample_point_t pts[128];
    uint32_t count = 0;
    srd_sample_room(&room, 4, pts, 128, &count);

    for (uint32_t i = 0; i < count; i++) {
        ASSERT_TRUE(pts[i].x >= 6.0f && pts[i].x <= 14.0f);
        ASSERT_TRUE(pts[i].z >= 2.0f && pts[i].z <= 8.0f);
        ASSERT_TRUE(pts[i].y >= 0.0f && pts[i].y <= 4.0f);
    }
    PASS();
}

static void test_cap_truncates(void) {
    fr_room_box_t room; fr_room_box_init(&room);
    room.half_extent_x = 4; room.half_extent_z = 3;
    room.floor_z = 0; room.ceil_z = 4;

    srd_sample_point_t pts[10];
    uint32_t count = 0;
    srd_sample_room(&room, 5, pts, 10, &count);  /* 5^3=125 > cap 10 */
    ASSERT_INT_EQ(count, 10);
    PASS();
}

int main(void) {
    printf("=== Sampler Tests ===\n\n");

    RUN(test_sample_count);
    RUN(test_samples_within_bounds);
    RUN(test_cap_truncates);

    printf("=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
