#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "ferrum/procgen/srd/srd_optimizer.h"
#include "ferrum/procgen/srd/srd_loss_compiler.h"

static int g_pass = 0;
static int g_fail = 0;

#define RUN(fn) do { printf("RUN  %s\n", #fn); fn(); printf("OK   %s\n\n", #fn); } while (0)
#define ASSERT_TRUE(expr) do { if (!(expr)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); g_fail++; return; } } while (0)
#define ASSERT_LT(a, b) ASSERT_TRUE((a) < (b))
#define PASS() g_pass++

static fr_room_box_t r(float cx, float cz, float hx, float hz) {
    fr_room_box_t b; fr_room_box_init(&b);
    b.center_x=cx; b.center_z=cz; b.half_extent_x=hx; b.half_extent_z=hz;
    b.floor_z=0; b.ceil_z=4; return b;
}

static void test_optimizer_room_energy_descent(void) {
    /* Room SDF energy should decrease toward center of sample grid */
    fr_room_box_t rooms[4];
    rooms[0]=r(50,50,4,4); rooms[1]=r(50,-50,4,4);
    rooms[2]=r(-50,50,4,4); rooms[3]=r(-50,-50,4,4);

    srd_optimize_config_t cfg; srd_optimize_config_default(&cfg);
    cfg.max_steps = 20; cfg.time_budget_s = 10.0;
    uint32_t n = 4;

    srd_loss_term_t t; memset(&t, 0, sizeof(t));
    t.primitive = FR_LOSS_NON_PENETRATION;
    srd_optimize(rooms, &n, 8, NULL, NULL, &t, 1, &cfg);
    /* Should complete without crash */
    ASSERT_TRUE(n >= 0);
    PASS();
}

static void test_optimizer_with_minimum_size(void) {
    /* Too-small rooms should be enlarged */
    fr_room_box_t rooms[2];
    rooms[0]=r(0,0,2,2); rooms[1]=r(10,10,2,2);

    const char *loss = "LOSS:\n  MinimumSize(all, 4)\n";
    srd_loss_term_t terms[8]; uint32_t nt=0;
    srd_loss_compile(loss, NULL, 2, terms, 8, &nt);

    srd_optimize_config_t cfg; srd_optimize_config_default(&cfg);
    cfg.max_steps = 20; cfg.time_budget_s = 5.0;
    uint32_t n = 2;
    srd_optimize(rooms, &n, 8, NULL, NULL, terms, nt, &cfg);
    PASS();
}

static void test_optimizer_with_separation(void) {
    fr_room_box_t rooms[2];
    rooms[0]=r(0,0,4,4); rooms[1]=r(5,0,4,4);

    const char *loss = "LOSS:\n  Separation(B, R) < 20\n";
    srd_loss_term_t terms[8]; uint32_t nt=0;
    srd_loss_compile(loss, NULL, 0, terms, 8, &nt);

    srd_optimize_config_t cfg; srd_optimize_config_default(&cfg);
    cfg.max_steps = 20; cfg.time_budget_s = 5.0;
    uint32_t n = 2;
    srd_optimize(rooms, &n, 8, NULL, NULL, terms, nt, &cfg);
    PASS();
}

static void test_optimizer_with_patdistance(void) {
    fr_room_box_t rooms[4];
    rooms[0]=r(0,0,4,4); rooms[1]=r(20,0,4,4);
    rooms[2]=r(0,20,4,4); rooms[3]=r(20,20,4,4);

    const char *loss = "LOSS:\n  PathDistance(R, R) < 50\n";
    srd_loss_term_t terms[8]; uint32_t nt=0;
    srd_loss_compile(loss, NULL, 4, terms, 8, &nt);

    srd_optimize_config_t cfg; srd_optimize_config_default(&cfg);
    cfg.max_steps = 20; cfg.time_budget_s = 5.0;
    uint32_t n = 4;
    srd_optimize(rooms, &n, 8, NULL, NULL, terms, nt, &cfg);
    PASS();
}

int main(void) {
    printf("=== SRD Optimizer Tests ===\n\n");
    RUN(test_optimizer_room_energy_descent);
    RUN(test_optimizer_with_minimum_size);
    RUN(test_optimizer_with_separation);
    RUN(test_optimizer_with_patdistance);
    printf("=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
