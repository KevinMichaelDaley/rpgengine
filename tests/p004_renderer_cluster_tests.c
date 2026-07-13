/**
 * @file p004_renderer_cluster_tests.c
 * @brief Unit tests for froxel/cluster light culling (headless).
 */
#include <stdio.h>
#include <string.h>

#include "ferrum/renderer/cluster_grid.h"
#include "ferrum/renderer/render_camera.h"

#define ASSERT_TRUE(cond)                                                     \
    do {                                                                      \
        if (!(cond)) {                                                        \
            fprintf(stderr, "ASSERT failed %s:%d: %s\n", __FILE__, __LINE__,  \
                    #cond);                                                   \
            return 1;                                                         \
        }                                                                     \
    } while (0)

#define TX 4
#define TY 4
#define TS 4
#define NCL (TX * TY * TS)

static render_light_t point_at(float x, float y, float z, float range) {
    render_light_t l; memset(&l, 0, sizeof(l));
    l.kind = RENDER_LIGHT_POINT;
    l.position[0]=x; l.position[1]=y; l.position[2]=z; l.range=range;
    l.flags = RENDER_LIGHT_FLAG_REALTIME;
    return l;
}

static void make_camera(render_camera_t *cam) {
    float eye[3]={0,0,0}, tgt[3]={0,0,-1}, up[3]={0,1,0};
    render_camera_look_at(cam, eye, tgt, up, 1.2f, 1.0f, 0.5f, 50.0f);
}

static uint32_t total_counts(const cluster_grid_t *g) {
    uint32_t sum = 0; for (uint32_t i=0;i<g->cluster_total;++i) sum += g->counts[i];
    return sum;
}

/* Directional light lands in every cluster. */
static int test_directional_all(void) {
    uint32_t off[NCL], cnt[NCL], idx[NCL*4];
    cluster_grid_t g;
    cluster_grid_init(&g, (cluster_config_t){ TX,TY,TS, 0.5f,50.0f }, off, cnt, idx, NCL*4);
    render_camera_t cam; make_camera(&cam);
    render_light_t dir; memset(&dir,0,sizeof dir);
    dir.kind = RENDER_LIGHT_DIRECTIONAL; dir.flags = RENDER_LIGHT_FLAG_REALTIME;
    cluster_grid_build(&g, &cam, &dir, 1);
    for (uint32_t i=0;i<g.cluster_total;++i) ASSERT_TRUE(cnt[i] == 1);
    ASSERT_TRUE(g.index_count == g.cluster_total);
    return 0;
}

/* A point light in front of centre hits some (not all) clusters; sum == index_count. */
static int test_point_subset(void) {
    uint32_t off[NCL], cnt[NCL], idx[NCL*4];
    cluster_grid_t g;
    cluster_grid_init(&g, (cluster_config_t){ TX,TY,TS, 0.5f,50.0f }, off, cnt, idx, NCL*4);
    render_camera_t cam; make_camera(&cam);
    render_light_t p = point_at(0,0,-10.0f, 2.0f);
    cluster_grid_build(&g, &cam, &p, 1);
    ASSERT_TRUE(g.index_count > 0);
    ASSERT_TRUE(g.index_count < g.cluster_total); /* not everywhere */
    ASSERT_TRUE(total_counts(&g) == g.index_count);
    return 0;
}

/* A point light far outside the frustum hits no cluster. */
static int test_point_outside(void) {
    uint32_t off[NCL], cnt[NCL], idx[NCL*4];
    cluster_grid_t g;
    cluster_grid_init(&g, (cluster_config_t){ TX,TY,TS, 0.5f,50.0f }, off, cnt, idx, NCL*4);
    render_camera_t cam; make_camera(&cam);
    render_light_t p = point_at(0, 60.0f, -10.0f, 2.0f); /* far above frustum */
    cluster_grid_build(&g, &cam, &p, 1);
    ASSERT_TRUE(g.index_count == 0);
    return 0;
}

struct tc { const char *name; int (*fn)(void); };
static struct tc TESTS[] = {
    { "directional_all", test_directional_all },
    { "point_subset", test_point_subset },
    { "point_outside", test_point_outside },
};

int main(void) {
    int failed = 0;
    for (size_t i = 0; i < sizeof(TESTS) / sizeof(TESTS[0]); ++i) {
        printf("RUN  %s\n", TESTS[i].name);
        int r = TESTS[i].fn();
        printf(r == 0 ? "OK   %s\n" : "FAIL %s\n", TESTS[i].name);
        failed += (r != 0);
    }
    printf("%s (%d failed)\n", failed ? "FAILED" : "PASSED", failed);
    return failed ? 1 : 0;
}
