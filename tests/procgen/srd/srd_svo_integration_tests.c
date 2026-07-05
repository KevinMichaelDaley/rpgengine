#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/procgen/procgen_svo_builder.h"
#include "ferrum/procgen/procgen_srd_types.h"

/* Forward declaration from C++ bridge — resolved at link time */
extern int srd_generate(const char *ascii, uint32_t seed, double time_budget,
                 fr_room_box_t **rooms_out, uint32_t *n_rooms_out,
                 fr_corridor_seg_t **corridors_out, uint32_t *n_corridors_out);

static int svo_solid(const npc_svo_grid_t *g, int x, int y, int z) {
    uint32_t cells = 1u << g->max_depth;
    if (x<0||y<0||z<0||(uint32_t)x>=cells||(uint32_t)y>=cells||(uint32_t)z>=cells) return 0;
    uint32_t n=0, c=cells;
    for(uint32_t d=0;d<g->max_depth;d++){c>>=1;
        uint32_t ci=(((uint32_t)z/c)&1)<<2|(((uint32_t)y/c)&1)<<1|(((uint32_t)x/c)&1);
        uint32_t ch=g->nodes[n].children[ci];if(ch==NPC_SVO_INVALID_NODE)return 0;n=ch;
        if(g->nodes[n].flags&NPC_SVO_FLAG_SOLID)return 1;}
    return 0;
}

static int g_pass = 0;
static int g_fail = 0;
#define RUN(fn) do { printf("RUN  %s\n", #fn); fn(); printf("OK   %s\n\n", #fn); } while (0)
#define ASSERT_TRUE(expr) do { if (!(expr)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); g_fail++; return; } } while (0)
#define ASSERT_GT(a,b) ASSERT_TRUE((a)>(b))
#define PASS() g_pass++

static const char *GRID =
    "=== FLOOR 0 ===\n"
    "W W W W W W\n"
    "W R R R R W\n"
    "W R R R R W\n"
    "W W W G W W\n"
    "LOSS:\n"
    "  MinimumSize(all, 4)\n"
    "  NonPenetration(all)\n";

static void test_srd_to_svo(void) {
    fr_room_box_t *rooms = NULL; uint32_t nr = 0;
    fr_corridor_seg_t *corrs = NULL; uint32_t nc = 0;

    int rc = srd_generate(GRID, 13, 1.0, &rooms, &nr, &corrs, &nc);
    ASSERT_TRUE(rc == 0);
    ASSERT_GT(nr, 1);

    npc_svo_grid_t svo;
    uint32_t solid = procgen_svo_build_from_srd(&svo, rooms, nr, corrs, nc);
    ASSERT_GT(solid, 0);

    /* Check floor voxel near room center */
    int vx = (int)((rooms[0].center_x + 64.0f) / 1.0f);
    int vy = (int)((rooms[0].floor_z + 64.0f) / 1.0f);
    int vz = (int)((rooms[0].center_z + 64.0f) / 1.0f);
    ASSERT_GT(svo_solid(&svo, vx, vy, vz), 0);

    npc_svo_grid_destroy(&svo);
    free(rooms); free(corrs);
    PASS();
}

int main(void) {
    printf("=== SRD→SVO Integration Test ===\n\n");
    RUN(test_srd_to_svo);
    printf("=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
