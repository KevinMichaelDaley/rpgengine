#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "ferrum/procgen/srd/srd_bridge.h"
#include "ferrum/procgen/srd/srd_loss_compiler.h"
#include "ferrum/procgen/srd/srd_loss_primitives.h"
#include "ferrum/procgen/procgen_srd_types.h"

static int g_pass=0,g_fail=0;
#define RUN(fn) do{printf("RUN %s\n",#fn);fn();printf("OK   %s\n\n",#fn);}while(0)
#define ASSERT_TRUE(e) do{if(!(e)){printf("FAIL %s:%d: %s\n",__FILE__,__LINE__,#e);g_fail++;return;}}while(0)
#define PASS() g_pass++

/* ── M3: loss composition ────────────────────────────────────── */

static void test_m3_multi_term_loss(void) {
    const char *loss =
        "LOSS:\n"
        "  PathDistance(B, R) < 30\n"
        "  LineOfSight(G, P)\n"
        "  NonPenetration(all)\n"
        "  MinimumSize(all, 6)\n";
    srd_loss_term_t terms[16]; uint32_t nt=0;
    srd_loss_compile(loss, NULL, 8, terms, 16, &nt);
    ASSERT_TRUE(nt >= 4);
    PASS();
}

static void test_m3_loss_composition_on_grid(void) {
    const char *grid =
        "=== FLOOR 0 ===\n"
        "W W W W W W W W\n"
        "W B B R R R R W\n"
        "W B B R R R R W\n"
        "W W W W G W W W\n"
        "LOSS:\n"
        "  MinimumSize(all, 6)\n"
        "  NonPenetration(all)\n";
    fr_room_box_t *rooms=NULL; uint32_t nr=0;
    fr_corridor_seg_t *corrs=NULL; uint32_t nc=0;
    int rc = srd_generate(grid, 7, 1.0, &rooms, &nr, &corrs, &nc);
    ASSERT_TRUE(rc==0);
    ASSERT_TRUE(nr>=3);
    free(rooms); free(corrs);
    PASS();
}

/* ── M4: SRD optimization smoke ──────────────────────────────── */

static void test_m4_srd_converges(void) {
    const char *grid =
        "=== FLOOR 0 ===\n"
        "W W W W W W\n"
        "W B B R R W\n"
        "W B B R R W\n"
        "W W W G W W\n"
        "LOSS:\n"
        "  MinimumSize(all, 4)\n"
        "  NonPenetration(all)\n";
    fr_room_box_t *rooms=NULL; uint32_t nr=0;
    fr_corridor_seg_t *corrs=NULL; uint32_t nc=0;
    int rc = srd_generate(grid, 1, 2.0, &rooms, &nr, &corrs, &nc);
    ASSERT_TRUE(rc==0);
    ASSERT_TRUE(nr>=2);
    free(rooms); free(corrs);
    PASS();
}

int main(void) {
    printf("=== M3+M4 Smoke Tests ===\n\n");
    RUN(test_m3_multi_term_loss);
    RUN(test_m3_loss_composition_on_grid);
    RUN(test_m4_srd_converges);
    printf("=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail>0?1:0;
}
