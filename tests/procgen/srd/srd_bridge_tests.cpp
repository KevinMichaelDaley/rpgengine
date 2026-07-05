#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "ferrum/procgen/srd/srd_bridge.h"

static int g_pass = 0;
static int g_fail = 0;
#define RUN(fn) do { printf("RUN  %s\n", #fn); fn(); printf("OK   %s\n\n", #fn); } while (0)
#define ASSERT_TRUE(expr) do { if (!(expr)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); g_fail++; return; } } while (0)
#define ASSERT_INT_EQ(a,b) ASSERT_TRUE((int)(a)==(int)(b))
#define PASS() g_pass++

static const char *TOWER =
    "=== FLOOR 0: GROUND ===\n"
    "W W W W W W\n"
    "W B B R R W\n"
    "W B B R R W\n"
    "W W W G W W\n"
    "=== FLOOR 1: UPPER ===\n"
    "W W W W W W\n"
    "W P P P P W\n"
    "W P P P P W\n"
    "W W W W W W\n"
    "LOSS:\n"
    "  MinimumSize(all, 6)\n"
    "  NonPenetration(all)\n";

static void test_bridge_generates_rooms(void) {
    fr_room_box_t *rooms = NULL;
    uint32_t n_rooms = 0;
    fr_corridor_seg_t *corrs = NULL;
    uint32_t n_corrs = 0;

    int rc = srd_generate(TOWER, 42, 2.0,
                           &rooms, &n_rooms, &corrs, &n_corrs);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_TRUE(n_rooms >= 2);
    /* Each room should have positive extent */
    for (uint32_t i = 0; i < n_rooms; i++)
        ASSERT_TRUE(rooms[i].half_extent_x > 0.0f);

    free(rooms);
    free(corrs);
    PASS();
}

int main(void) {
    printf("=== SRD Bridge Test ===\n\n");
    RUN(test_bridge_generates_rooms);
    printf("=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
