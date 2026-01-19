#include <stdio.h>

#include "ferrum/ecs/sparse_set.h"
#include "ferrum/ecs/world.h"
#include "ferrum/job/system.h"
#include "ferrum/math/mat4.h"
#include "ferrum/renderer/skinning/components.h"
#include "ferrum/renderer/skinning/pipeline.h"
#include "ferrum/renderer/skinning/skin.h"

#define ASSERT_TRUE(cond)                                                                               \
    do {                                                                                                \
        if (!(cond)) {                                                                                  \
            fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
            return 1;                                                                                   \
        }                                                                                               \
    } while (0)

#define ASSERT_INT_EQ(exp, act)                                                                         \
    do {                                                                                                \
        if ((exp) != (act)) {                                                                           \
            fprintf(stderr, "ASSERT_INT_EQ failed: %s:%d: expected %d got %d\n", __FILE__, __LINE__,    \
                    (int)(exp), (int)(act));                                                            \
            return 1;                                                                                   \
        }                                                                                               \
    } while (0)

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

ECS_DEFINE_SPARSE_SET(skinning_skeleton, skinning_skeleton_t)
ECS_DEFINE_SPARSE_SET(skinning_skin, skinning_skin_t)

static job_system_t *create_job_system(void) {
    job_system_t *sys = job_system_create(1, 32, 64 * 1024, 1);
    if (sys == NULL) {
        return NULL;
    }
    if (job_system_start(sys) != 0) {
        job_system_shutdown(sys);
        return NULL;
    }
    return sys;
}

static int test_pipeline_reuses_buffers(void) {
    ecs_world_t world;
    ASSERT_INT_EQ(ECS_OK, ecs_world_init(&world, 4));

    ecs_sparse_set_skinning_skeleton_t skeletons;
    ecs_sparse_set_skinning_skin_t skins;
    ASSERT_INT_EQ(ECS_OK, ecs_sparse_set_skinning_skeleton_init(&skeletons, 4));
    ASSERT_INT_EQ(ECS_OK, ecs_sparse_set_skinning_skin_init(&skins, 4));

    entity_t entities[2];
    mat4_t locals[2] = {mat4_identity(), mat4_identity()};
    uint32_t parents[2] = {SKINNING_SKELETON_NO_PARENT, SKINNING_SKELETON_NO_PARENT};
    for (size_t i = 0; i < ARRAY_SIZE(entities); ++i) {
        ASSERT_INT_EQ(ECS_OK, ecs_world_create_entity(&world, &entities[i]));
        skinning_skeleton_t skeleton = {1u, &locals[i], &parents[i]};
        ASSERT_INT_EQ(ECS_OK, ecs_sparse_set_skinning_skeleton_insert(&skeletons, entities[i], &skeleton));
        skinning_skin_t skin = {entities[i]};
        ASSERT_INT_EQ(ECS_OK, ecs_sparse_set_skinning_skin_insert(&skins, entities[i], &skin));
    }

    skinning_pipeline_t pipeline;
    ASSERT_INT_EQ(SKINNING_PIPELINE_OK, skinning_pipeline_init(&pipeline, 4u, 2u));

    job_system_t *sys = create_job_system();
    ASSERT_TRUE(sys != NULL);
    ASSERT_INT_EQ(SKINNING_PIPELINE_OK,
                  skinning_pipeline_update(&pipeline, sys, &skeletons.base, &skins.base));

    void *job_contexts = pipeline.job_contexts;
    uint32_t *palette_indices = pipeline.draw_list_palette_indices;
    ASSERT_TRUE(job_contexts != NULL);
    ASSERT_TRUE(palette_indices != NULL);

    entity_t storage[2];
    skinning_draw_list_t list = {0};
    for (size_t i = 0; i < 3; ++i) {
        ASSERT_INT_EQ(SKINNING_PIPELINE_OK,
                      skinning_pipeline_update(&pipeline, sys, &skeletons.base, &skins.base));
        ASSERT_INT_EQ(SKINNING_PIPELINE_OK,
                      skinning_pipeline_build_draw_list(&pipeline, &skins.base, &list,
                                                        storage, ARRAY_SIZE(storage)));
        ASSERT_TRUE(pipeline.job_contexts == job_contexts);
        ASSERT_TRUE(pipeline.draw_list_palette_indices == palette_indices);
        ASSERT_INT_EQ(2, (int)list.count);
    }

    job_system_shutdown(sys);
    skinning_pipeline_destroy(&pipeline);
    ecs_sparse_set_skinning_skin_destroy(&skins);
    ecs_sparse_set_skinning_skeleton_destroy(&skeletons);
    ecs_world_destroy(&world);
    return 0;
}

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    {"pipeline_reuses_buffers", test_pipeline_reuses_buffers}
};

int main(void) {
    size_t total = ARRAY_SIZE(TESTS);
    size_t passed = 0;
    for (size_t i = 0; i < total; ++i) {
        printf("RUN %s\n", TESTS[i].name);
        int rc = TESTS[i].fn();
        if (rc == 0) {
            printf("OK %s\n", TESTS[i].name);
            passed++;
        } else {
            fprintf(stderr, "FAIL %s (rc=%d)\n", TESTS[i].name, rc);
            break;
        }
    }
    if (passed == total) {
        printf("All %zu tests passed\n", passed);
        return 0;
    }
    return 1;
}
