/**
 * @file p004_renderer_light_store_tests.c
 * @brief Unit tests for the scene light store + pack (pure data, no GL).
 */
#include <stdio.h>
#include <string.h>

#include "ferrum/renderer/light.h"
#include "ferrum/renderer/light_store.h"

#define ASSERT_TRUE(cond)                                                     \
    do {                                                                      \
        if (!(cond)) {                                                        \
            fprintf(stderr, "ASSERT failed %s:%d: %s\n", __FILE__, __LINE__,  \
                    #cond);                                                   \
            return 1;                                                         \
        }                                                                     \
    } while (0)

static render_light_t mk(render_light_kind_t kind, uint32_t flags, float ix) {
    render_light_t l;
    memset(&l, 0, sizeof(l));
    l.kind = kind;
    l.color[0] = l.color[1] = l.color[2] = 1.0f;
    l.intensity = ix;
    l.range = 10.0f;
    l.flags = flags;
    return l;
}

/* add fills to capacity then rejects; clear resets. */
static int test_add_capacity_clear(void) {
    render_light_t backing[3];
    render_light_store_t s;
    render_light_store_init(&s, backing, 3);
    ASSERT_TRUE(s.count == 0 && s.capacity == 3);
    render_light_t l = mk(RENDER_LIGHT_POINT, RENDER_LIGHT_FLAG_REALTIME, 1.0f);
    ASSERT_TRUE(render_light_add(&s, &l));
    ASSERT_TRUE(render_light_add(&s, &l));
    ASSERT_TRUE(render_light_add(&s, &l));
    ASSERT_TRUE(!render_light_add(&s, &l)); /* full */
    ASSERT_TRUE(s.count == 3);
    render_light_store_clear(&s);
    ASSERT_TRUE(s.count == 0);
    ASSERT_TRUE(!render_light_add(NULL, &l));
    return 0;
}

/* pack keeps only realtime punctual lights, premultiplies intensity, honours max. */
static int test_pack_filters(void) {
    render_light_t backing[5];
    render_light_store_t s;
    render_light_store_init(&s, backing, 5);
    render_light_t p = mk(RENDER_LIGHT_POINT, RENDER_LIGHT_FLAG_REALTIME, 2.0f);
    p.color[0] = 0.5f;
    render_light_t baked = mk(RENDER_LIGHT_POINT, RENDER_LIGHT_FLAG_BAKED, 1.0f);
    render_light_t area = mk(RENDER_LIGHT_AREA, RENDER_LIGHT_FLAG_REALTIME, 1.0f);
    render_light_t spot = mk(RENDER_LIGHT_SPOT, RENDER_LIGHT_FLAG_REALTIME, 1.0f);
    render_light_add(&s, &p);      /* keep */
    render_light_add(&s, &baked);  /* skip: not realtime */
    render_light_add(&s, &area);   /* skip: area kind */
    render_light_add(&s, &spot);   /* keep */

    int32_t ty[8]; float pos[24], dir[24], col[24], rng[8], ci[8], co[8];
    uint32_t n = render_light_store_pack(&s, ty, pos, dir, col, rng, ci, co, 8);
    ASSERT_TRUE(n == 2);
    ASSERT_TRUE(ty[0] == RENDER_LIGHT_POINT && ty[1] == RENDER_LIGHT_SPOT);
    ASSERT_TRUE(col[0] == 1.0f); /* 0.5 color * 2.0 intensity */

    /* max clamps output. */
    ASSERT_TRUE(render_light_store_pack(&s, ty, pos, dir, col, rng, ci, co, 1) == 1);
    return 0;
}

struct tc { const char *name; int (*fn)(void); };
static struct tc TESTS[] = {
    { "add_capacity_clear", test_add_capacity_clear },
    { "pack_filters", test_pack_filters },
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
