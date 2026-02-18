#include <glad/glad.h>
#include <SDL2/SDL.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/ecs/world.h"
#include "ferrum/ecs/sparse_set.h"
#include "ferrum/job/system.h"
#include "ferrum/math/mat4.h"
#include "ferrum/renderer/bone_palette.h"
#include "ferrum/renderer/gl_loader.h"
#include "ferrum/renderer/skinning_shader.h"
#include "ferrum/renderer/skinning/components.h"
#include "ferrum/renderer/skinning/skin.h"
#include "ferrum/renderer/skinning/pipeline.h"

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

struct gl_test_context {
    SDL_Window *window;
    SDL_GLContext context;
};

static gl_loader_t g_loader;

static void *sdl_get_proc_address(const char *name, void *user_data) {
    (void)user_data;
    return SDL_GL_GetProcAddress(name);
}

static int gl_test_context_init(struct gl_test_context *ctx) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return -1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);

    ctx->window = SDL_CreateWindow("p004_ecs_skinning_tests",
                                   SDL_WINDOWPOS_UNDEFINED,
                                   SDL_WINDOWPOS_UNDEFINED,
                                   64,
                                   64,
                                   SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (ctx->window == NULL) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return -1;
    }
    ctx->context = SDL_GL_CreateContext(ctx->window);
    if (ctx->context == NULL) {
        fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(ctx->window);
        SDL_Quit();
        return -1;
    }
    if (SDL_GL_MakeCurrent(ctx->window, ctx->context) != 0) {
        fprintf(stderr, "SDL_GL_MakeCurrent failed: %s\n", SDL_GetError());
        SDL_GL_DeleteContext(ctx->context);
        SDL_DestroyWindow(ctx->window);
        SDL_Quit();
        return -1;
    }

    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        fprintf(stderr, "gladLoadGLLoader failed
");
        SDL_GL_DeleteContext(ctx->context);
        SDL_DestroyWindow(ctx->window);
        SDL_Quit();
        return -1;
    }
    (void)glGetError();

    g_loader.get_proc_address = sdl_get_proc_address;
    g_loader.user_data = NULL;
    return 0;
}

static void gl_test_context_shutdown(struct gl_test_context *ctx) {
    if (ctx->context != NULL) {
        SDL_GL_DeleteContext(ctx->context);
    }
    if (ctx->window != NULL) {
        SDL_DestroyWindow(ctx->window);
    }
    SDL_Quit();
}

static job_system_t *create_job_system(void) {
    job_system_t sys_; job_system_t* sys=&sys_;
job_system_create_status_t sys_create_status =  job_system_create(sys,1, 32, 64 * 1024, 2048, 1);
    if (sys == NULL) {
        return NULL;
    }
    if (job_system_start(sys) != 0) {
        job_system_shutdown(sys);
        return NULL;
    }
    return sys;
}

static int test_palette_index_mapping_stability(void) {
    ecs_world_t world;
    ASSERT_INT_EQ(ECS_OK, ecs_world_init(&world, 8));

    ecs_sparse_set_skinning_skeleton_t skeletons;
    ecs_sparse_set_skinning_skin_t skins;
    ASSERT_INT_EQ(ECS_OK, ecs_sparse_set_skinning_skeleton_init(&skeletons, 8));
    ASSERT_INT_EQ(ECS_OK, ecs_sparse_set_skinning_skin_init(&skins, 8));

    entity_t first;
    entity_t second;
    ASSERT_INT_EQ(ECS_OK, ecs_world_create_entity(&world, &first));
    ASSERT_INT_EQ(ECS_OK, ecs_world_create_entity(&world, &second));

    mat4_t first_local[1] = {mat4_identity()};
    uint32_t first_parents[1] = {SKINNING_SKELETON_NO_PARENT};
    skinning_skeleton_t first_skeleton = {1u, first_local, first_parents};
    ASSERT_INT_EQ(ECS_OK, ecs_sparse_set_skinning_skeleton_insert(&skeletons, first, &first_skeleton));
    skinning_skin_t first_skin = {first};
    ASSERT_INT_EQ(ECS_OK, ecs_sparse_set_skinning_skin_insert(&skins, first, &first_skin));

    mat4_t second_local[1] = {mat4_identity()};
    uint32_t second_parents[1] = {SKINNING_SKELETON_NO_PARENT};
    skinning_skeleton_t second_skeleton = {1u, second_local, second_parents};
    ASSERT_INT_EQ(ECS_OK, ecs_sparse_set_skinning_skeleton_insert(&skeletons, second, &second_skeleton));
    skinning_skin_t second_skin = {second};
    ASSERT_INT_EQ(ECS_OK, ecs_sparse_set_skinning_skin_insert(&skins, second, &second_skin));

    skinning_pipeline_t pipeline;
    ASSERT_INT_EQ(SKINNING_PIPELINE_OK, skinning_pipeline_init(&pipeline, 8u, 2u));

    job_system_t *sys = create_job_system();
    ASSERT_TRUE(sys != NULL);
    ASSERT_INT_EQ(SKINNING_PIPELINE_OK,
                  skinning_pipeline_update(&pipeline, sys, &skeletons.base, &skins.base));

    uint32_t first_index = 0;
    uint32_t second_index = 0;
    ASSERT_INT_EQ(SKINNING_PIPELINE_OK, skinning_pipeline_palette_index(&pipeline, first, &first_index));
    ASSERT_INT_EQ(SKINNING_PIPELINE_OK, skinning_pipeline_palette_index(&pipeline, second, &second_index));
    ASSERT_TRUE(first_index != second_index);

    ASSERT_INT_EQ(ECS_OK, ecs_sparse_set_skinning_skin_remove(&skins, first));
    ASSERT_INT_EQ(ECS_OK, ecs_sparse_set_skinning_skeleton_remove(&skeletons, first));
    ASSERT_INT_EQ(ECS_OK, ecs_world_destroy_entity(&world, first));

    entity_t third;
    ASSERT_INT_EQ(ECS_OK, ecs_world_create_entity(&world, &third));
    mat4_t third_local[1] = {mat4_identity()};
    uint32_t third_parents[1] = {SKINNING_SKELETON_NO_PARENT};
    skinning_skeleton_t third_skeleton = {1u, third_local, third_parents};
    ASSERT_INT_EQ(ECS_OK, ecs_sparse_set_skinning_skeleton_insert(&skeletons, third, &third_skeleton));
    skinning_skin_t third_skin = {third};
    ASSERT_INT_EQ(ECS_OK, ecs_sparse_set_skinning_skin_insert(&skins, third, &third_skin));

    ASSERT_INT_EQ(SKINNING_PIPELINE_OK,
                  skinning_pipeline_update(&pipeline, sys, &skeletons.base, &skins.base));

    uint32_t second_index_after = 0;
    uint32_t third_index = 0;
    ASSERT_INT_EQ(SKINNING_PIPELINE_OK, skinning_pipeline_palette_index(&pipeline, second, &second_index_after));
    ASSERT_INT_EQ(SKINNING_PIPELINE_OK, skinning_pipeline_palette_index(&pipeline, third, &third_index));
    ASSERT_INT_EQ((int)second_index, (int)second_index_after);
    ASSERT_INT_EQ((int)first_index, (int)third_index);

    job_system_shutdown(sys);
    skinning_pipeline_destroy(&pipeline);
    ecs_sparse_set_skinning_skin_destroy(&skins);
    ecs_sparse_set_skinning_skeleton_destroy(&skeletons);
    ecs_world_destroy(&world);
    return 0;
}

static int test_gpu_skinning_integration(void) {
    ecs_world_t world;
    ASSERT_INT_EQ(ECS_OK, ecs_world_init(&world, 4));

    ecs_sparse_set_skinning_skeleton_t skeletons;
    ecs_sparse_set_skinning_skin_t skins;
    ASSERT_INT_EQ(ECS_OK, ecs_sparse_set_skinning_skeleton_init(&skeletons, 4));
    ASSERT_INT_EQ(ECS_OK, ecs_sparse_set_skinning_skin_init(&skins, 4));

    entity_t entity;
    ASSERT_INT_EQ(ECS_OK, ecs_world_create_entity(&world, &entity));

    mat4_t local[2] = {mat4_identity(), mat4_translation(1.0f, 0.0f, 0.0f)};
    uint32_t parents[2] = {SKINNING_SKELETON_NO_PARENT, 0u};
    skinning_skeleton_t skeleton = {2u, local, parents};
    ASSERT_INT_EQ(ECS_OK, ecs_sparse_set_skinning_skeleton_insert(&skeletons, entity, &skeleton));
    skinning_skin_t skin = {entity};
    ASSERT_INT_EQ(ECS_OK, ecs_sparse_set_skinning_skin_insert(&skins, entity, &skin));

    skinning_pipeline_t pipeline;
    ASSERT_INT_EQ(SKINNING_PIPELINE_OK, skinning_pipeline_init(&pipeline, 4u, 2u));

    job_system_t *sys = create_job_system();
    ASSERT_TRUE(sys != NULL);
    ASSERT_INT_EQ(SKINNING_PIPELINE_OK,
                  skinning_pipeline_update(&pipeline, sys, &skeletons.base, &skins.base));

    uint32_t palette_index = 0;
    ASSERT_INT_EQ(SKINNING_PIPELINE_OK, skinning_pipeline_palette_index(&pipeline, entity, &palette_index));

    bone_palette_buffer_t palette;
    ASSERT_INT_EQ(BONE_PALETTE_OK,
                  bone_palette_buffer_init(&palette, &g_loader, 2u, 0u, 0, 0));

    ASSERT_INT_EQ(SKINNING_PIPELINE_OK,
                  skinning_pipeline_upload_palette(&pipeline, &palette, palette_index));

    glBindBuffer(GL_UNIFORM_BUFFER, bone_palette_buffer_handle(&palette));
    mat4_t gpu_data[2];
    glGetBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(gpu_data), gpu_data);
    ASSERT_TRUE(memcmp(gpu_data[0].m, local[0].m, sizeof(gpu_data[0].m)) == 0);
    ASSERT_TRUE(memcmp(gpu_data[1].m, local[1].m, sizeof(gpu_data[1].m)) == 0);

    skinning_shader_t shader;
    char log_buffer[128] = {0};
    ASSERT_INT_EQ(SKINNING_SHADER_OK,
                  skinning_shader_create(&shader, &g_loader, log_buffer, sizeof(log_buffer)));
    ASSERT_INT_EQ(SKINNING_SHADER_OK, skinning_shader_bind(&shader, &palette));
    skinning_shader_destroy(&shader);

    GLint bound_palette = 0;
    glGetIntegeri_v(GL_UNIFORM_BUFFER_BINDING, 0, &bound_palette);
    ASSERT_INT_EQ((int)bone_palette_buffer_handle(&palette), bound_palette);

    bone_palette_buffer_destroy(&palette);
    job_system_shutdown(sys);
    skinning_pipeline_destroy(&pipeline);
    ecs_sparse_set_skinning_skin_destroy(&skins);
    ecs_sparse_set_skinning_skeleton_destroy(&skeletons);
    ecs_world_destroy(&world);
    return 0;
}

static int test_render_command_order_deterministic(void) {
    ecs_world_t world;
    ASSERT_INT_EQ(ECS_OK, ecs_world_init(&world, 8));

    ecs_sparse_set_skinning_skeleton_t skeletons;
    ecs_sparse_set_skinning_skin_t skins;
    ASSERT_INT_EQ(ECS_OK, ecs_sparse_set_skinning_skeleton_init(&skeletons, 8));
    ASSERT_INT_EQ(ECS_OK, ecs_sparse_set_skinning_skin_init(&skins, 8));

    entity_t entities[3];
    mat4_t locals[ARRAY_SIZE(entities)];
    uint32_t parents[ARRAY_SIZE(entities)];
    for (size_t i = 0; i < ARRAY_SIZE(entities); ++i) {
        locals[i] = mat4_identity();
        parents[i] = SKINNING_SKELETON_NO_PARENT;
        ASSERT_INT_EQ(ECS_OK, ecs_world_create_entity(&world, &entities[i]));
        skinning_skeleton_t skeleton = {1u, &locals[i], &parents[i]};
        ASSERT_INT_EQ(ECS_OK, ecs_sparse_set_skinning_skeleton_insert(&skeletons, entities[i], &skeleton));
        skinning_skin_t skin = {entities[i]};
        ASSERT_INT_EQ(ECS_OK, ecs_sparse_set_skinning_skin_insert(&skins, entities[i], &skin));
    }

    skinning_pipeline_t pipeline;
    ASSERT_INT_EQ(SKINNING_PIPELINE_OK, skinning_pipeline_init(&pipeline, 8u, 1u));

    job_system_t *sys = create_job_system();
    ASSERT_TRUE(sys != NULL);
    ASSERT_INT_EQ(SKINNING_PIPELINE_OK,
                  skinning_pipeline_update(&pipeline, sys, &skeletons.base, &skins.base));

    entity_t storage_a[3];
    entity_t storage_b[3];
    skinning_draw_list_t list_a = {0};
    skinning_draw_list_t list_b = {0};

    ASSERT_INT_EQ(SKINNING_PIPELINE_OK,
                  skinning_pipeline_build_draw_list(&pipeline, &skins.base, &list_a,
                                                    storage_a, ARRAY_SIZE(storage_a)));
    ASSERT_INT_EQ(SKINNING_PIPELINE_OK,
                  skinning_pipeline_build_draw_list(&pipeline, &skins.base, &list_b,
                                                    storage_b, ARRAY_SIZE(storage_b)));
    ASSERT_INT_EQ((int)list_a.count, (int)list_b.count);
    ASSERT_TRUE(memcmp(list_a.entities, list_b.entities, list_a.count * sizeof(entity_t)) == 0);

    for (uint32_t i = 1; i < list_a.count; ++i) {
        uint32_t prev = 0;
        uint32_t next = 0;
        skinning_skin_t *prev_skin = ecs_sparse_set_skinning_skin_get(&skins, list_a.entities[i - 1]);
        skinning_skin_t *next_skin = ecs_sparse_set_skinning_skin_get(&skins, list_a.entities[i]);
        ASSERT_TRUE(prev_skin != NULL);
        ASSERT_TRUE(next_skin != NULL);
        ASSERT_INT_EQ(SKINNING_PIPELINE_OK,
                      skinning_pipeline_palette_index(&pipeline, prev_skin->skeleton_entity, &prev));
        ASSERT_INT_EQ(SKINNING_PIPELINE_OK,
                      skinning_pipeline_palette_index(&pipeline, next_skin->skeleton_entity, &next));
        ASSERT_TRUE(prev < next);
    }

    ASSERT_INT_EQ(ECS_OK, ecs_sparse_set_skinning_skin_remove(&skins, entities[1]));
    ASSERT_INT_EQ(ECS_OK, ecs_sparse_set_skinning_skeleton_remove(&skeletons, entities[1]));
    ASSERT_INT_EQ(ECS_OK, ecs_world_destroy_entity(&world, entities[1]));

    entity_t new_entity;
    ASSERT_INT_EQ(ECS_OK, ecs_world_create_entity(&world, &new_entity));
    mat4_t local = mat4_identity();
    uint32_t parent = SKINNING_SKELETON_NO_PARENT;
    skinning_skeleton_t skeleton = {1u, &local, &parent};
    ASSERT_INT_EQ(ECS_OK, ecs_sparse_set_skinning_skeleton_insert(&skeletons, new_entity, &skeleton));
    skinning_skin_t skin = {new_entity};
    ASSERT_INT_EQ(ECS_OK, ecs_sparse_set_skinning_skin_insert(&skins, new_entity, &skin));

    ASSERT_INT_EQ(SKINNING_PIPELINE_OK,
                  skinning_pipeline_update(&pipeline, sys, &skeletons.base, &skins.base));

    entity_t storage_c[3];
    skinning_draw_list_t list_c = {0};
    ASSERT_INT_EQ(SKINNING_PIPELINE_OK,
                  skinning_pipeline_build_draw_list(&pipeline, &skins.base, &list_c,
                                                    storage_c, ARRAY_SIZE(storage_c)));
    for (uint32_t i = 1; i < list_c.count; ++i) {
        uint32_t prev = 0;
        uint32_t next = 0;
        skinning_skin_t *prev_skin = ecs_sparse_set_skinning_skin_get(&skins, list_c.entities[i - 1]);
        skinning_skin_t *next_skin = ecs_sparse_set_skinning_skin_get(&skins, list_c.entities[i]);
        ASSERT_TRUE(prev_skin != NULL);
        ASSERT_TRUE(next_skin != NULL);
        ASSERT_INT_EQ(SKINNING_PIPELINE_OK,
                      skinning_pipeline_palette_index(&pipeline, prev_skin->skeleton_entity, &prev));
        ASSERT_INT_EQ(SKINNING_PIPELINE_OK,
                      skinning_pipeline_palette_index(&pipeline, next_skin->skeleton_entity, &next));
        ASSERT_TRUE(prev < next);
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
    {"palette_index_mapping_stability", test_palette_index_mapping_stability},
    {"gpu_skinning_integration", test_gpu_skinning_integration},
    {"render_command_order_deterministic", test_render_command_order_deterministic}
};

int main(void) {
    struct gl_test_context ctx = {0};
    if (gl_test_context_init(&ctx) != 0) {
        return 1;
    }

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

    gl_test_context_shutdown(&ctx);

    if (passed == total) {
        printf("All %zu tests passed\n", passed);
        return 0;
    }
    return 1;
}
