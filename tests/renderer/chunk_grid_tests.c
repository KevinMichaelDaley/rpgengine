/**
 * @file chunk_grid_tests.c
 * @brief Unit tests for chunk_grid: partition a scene AABB into a uniform grid
 *        of cubic chunks with an overlap margin (rpg-fzht). Pure geometry, no GL.
 */
#include <stdio.h>
#include <stdint.h>

#include "ferrum/renderer/chunk/chunk_grid.h"

static int g_fail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); ++g_fail; } \
                              else printf("ok: %s\n", msg); } while (0)
static int feq(float a, float b) { float d = a - b; return d > -1e-4f && d < 1e-4f; }

static phys_aabb_t aabb(float x0,float y0,float z0,float x1,float y1,float z1) {
    return (phys_aabb_t){ { x0,y0,z0 }, { x1,y1,z1 } };
}

int main(void)
{
    chunk_grid_t g;
    /* 10x10x10 scene, 4m cubic chunks, 1m overlap margin -> 3x3x3 chunks. */
    bool ok = chunk_grid_init(&g, aabb(0,0,0, 10,10,10), 4.0f, 1.0f);
    CHECK(ok, "init succeeds");
    CHECK(g.dims[0]==3 && g.dims[1]==3 && g.dims[2]==3, "dims are 3x3x3 (ceil 10/4)");
    CHECK(chunk_grid_count(&g)==27u, "count is 27");

    /* index <-> ijk round trip. */
    int ijk[3];
    uint32_t idx = chunk_grid_index(&g, 1, 2, 0);
    CHECK(idx == 1u + 2u*3u, "index(1,2,0) == 7");
    chunk_grid_ijk(&g, idx, ijk);
    CHECK(ijk[0]==1 && ijk[1]==2 && ijk[2]==0, "ijk(7) == (1,2,0)");

    /* bounds: chunk 0 inner [0,4]^3, outer [-1,5]^3. */
    phys_aabb_t inr, out;
    chunk_grid_bounds(&g, 0, &inr, &out);
    CHECK(feq(inr.min.x,0)&&feq(inr.max.x,4), "chunk0 inner x [0,4]");
    CHECK(feq(out.min.x,-1)&&feq(out.max.x,5), "chunk0 outer x [-1,5] (margin)");

    /* of_point: which chunk contains a world point. */
    CHECK(chunk_grid_of_point(&g, 2,2,2) == 0u, "point (2,2,2) -> chunk 0");
    CHECK(chunk_grid_of_point(&g, 5,5,5) == chunk_grid_index(&g,1,1,1), "point (5,5,5) -> chunk (1,1,1)");
    CHECK(chunk_grid_of_point(&g, 9.9f,9.9f,9.9f) == chunk_grid_index(&g,2,2,2), "point (9.9) -> chunk (2,2,2)");
    CHECK(chunk_grid_of_point(&g, -1,0,0) == UINT32_MAX, "point outside -> UINT32_MAX");

    /* overlaps_aabb: box vs a chunk's OUTER box. */
    CHECK( chunk_grid_overlaps_aabb(&g, 0, aabb(3,3,3, 3.5f,3.5f,3.5f)), "box inside chunk0 outer -> overlaps");
    CHECK(!chunk_grid_overlaps_aabb(&g, 0, aabb(6,6,6, 7,7,7)),           "box beyond chunk0 outer -> no overlap");
    CHECK( chunk_grid_overlaps_aabb(&g, 0, aabb(4.5f,0,0, 5.5f,1,1)),     "box straddling outer edge -> overlaps");

    /* edge: scene smaller than a chunk -> single chunk. */
    chunk_grid_t s;
    CHECK(chunk_grid_init(&s, aabb(0,0,0, 1,1,1), 4.0f, 0.5f), "small scene inits");
    CHECK(chunk_grid_count(&s)==1u, "small scene -> 1 chunk");

    /* failure modes. */
    CHECK(!chunk_grid_init(NULL, aabb(0,0,0,1,1,1), 1,0), "NULL grid -> false");
    CHECK(!chunk_grid_init(&g, aabb(0,0,0,1,1,1), 0.0f, 0), "chunk_size 0 -> false");
    CHECK(!chunk_grid_init(&g, aabb(1,1,1, 0,0,0), 1.0f, 0), "inverted bounds -> false");

    printf(g_fail ? "\n%d FAILURES\n" : "\nALL PASS\n", g_fail);
    return g_fail ? 1 : 0;
}
