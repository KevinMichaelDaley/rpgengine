/**
 * @file p004_renderer_scene_tests.c
 * @brief Unit tests for the scene submission interface + camera (no GL).
 */
#include <stdio.h>
#include <string.h>

#include "ferrum/renderer/render_camera.h"
#include "ferrum/renderer/render_scene.h"

#define ASSERT_TRUE(cond)                                                     \
    do {                                                                      \
        if (!(cond)) {                                                        \
            fprintf(stderr, "ASSERT failed %s:%d: %s\n", __FILE__, __LINE__,  \
                    #cond);                                                   \
            return 1;                                                         \
        }                                                                     \
    } while (0)

/* Scene add fills to capacity, rejects overflow, records model + material. */
static int test_scene_add_clear(void) {
    render_renderable_t backing[2];
    render_scene_t scene;
    render_scene_init(&scene, backing, 2);
    ASSERT_TRUE(scene.count == 0 && scene.capacity == 2);

    render_material_t mat;
    float model[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 5,6,7,1 };
    ASSERT_TRUE(render_scene_add(&scene, NULL, &mat, model));
    ASSERT_TRUE(render_scene_add(&scene, NULL, &mat, model));
    ASSERT_TRUE(!render_scene_add(&scene, NULL, &mat, model)); /* full */
    ASSERT_TRUE(scene.count == 2);
    ASSERT_TRUE(scene.items[0].material == &mat);
    ASSERT_TRUE(scene.items[0].model[12] == 5.0f && scene.items[0].model[14] == 7.0f);

    render_scene_clear(&scene);
    ASSERT_TRUE(scene.count == 0);
    ASSERT_TRUE(!render_scene_add(NULL, NULL, &mat, model));
    return 0;
}

/* Camera look-at stores the eye and builds non-degenerate matrices. */
static int test_camera_look_at(void) {
    render_camera_t cam;
    float eye[3] = { 0, 0, 5 }, target[3] = { 0, 0, 0 }, up[3] = { 0, 1, 0 };
    render_camera_look_at(&cam, eye, target, up, 1.0f, 1.0f, 0.1f, 100.0f);
    ASSERT_TRUE(cam.eye[2] == 5.0f);
    /* projection has a non-zero perspective term (m[11] == -1 for GL persp). */
    ASSERT_TRUE(cam.proj[11] != 0.0f);
    /* view is a real look-at, not identity: it carries a translation. */
    ASSERT_TRUE(cam.view[14] != 0.0f);
    render_camera_look_at(NULL, eye, target, up, 1.0f, 1.0f, 0.1f, 100.0f); /* NULL-safe */
    return 0;
}

struct tc { const char *name; int (*fn)(void); };
static struct tc TESTS[] = {
    { "scene_add_clear", test_scene_add_clear },
    { "camera_look_at", test_camera_look_at },
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
