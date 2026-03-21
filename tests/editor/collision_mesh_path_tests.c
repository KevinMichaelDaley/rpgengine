/**
 * @file collision_mesh_path_tests.c
 * @brief Tests for collision mesh path attribute (Phase C).
 *
 * Validates that SCRIPT_KEY_COLLISION_MESH_PATH is correctly defined
 * and can be stored/retrieved via entity_attrs_t, ensuring the
 * collision geometry separation pipeline has a working attribute key.
 */

#include "ferrum/entity/entity_attrs.h"

#include <stdio.h>
#include <string.h>

static int g_pass, g_fail;

#define ASSERT(cond) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; \
           fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

/* ---- Tests ---- */

/** SCRIPT_KEY_COLLISION_MESH_PATH is defined as 18. */
static void test_key_value(void) {
    ASSERT(SCRIPT_KEY_COLLISION_MESH_PATH == 18);
    /* Must not collide with other keys. */
    ASSERT(SCRIPT_KEY_COLLISION_MESH_PATH != SCRIPT_KEY_MESH_PATH);
    ASSERT(SCRIPT_KEY_COLLISION_MESH_PATH != SCRIPT_KEY_SKEL_PATH);
    ASSERT(SCRIPT_KEY_COLLISION_MESH_PATH < SCRIPT_KEY_ECS_BASE);
}

/** Store and retrieve a collision mesh path string. */
static void test_set_and_get_path(void) {
    entity_attrs_t attrs;
    entity_attrs_init(&attrs);

    const char *path = "collision/cube.fvma";
    bool ok = entity_attrs_set(&attrs, SCRIPT_KEY_COLLISION_MESH_PATH,
                                SCRIPT_ATTR_STR, path,
                                (uint8_t)(strlen(path) + 1));
    ASSERT(ok);

    uint8_t type = 0, size = 0;
    const void *val = entity_attrs_get(&attrs,
                                        SCRIPT_KEY_COLLISION_MESH_PATH,
                                        &type, &size);
    ASSERT(val != NULL);
    ASSERT(type == SCRIPT_ATTR_STR);
    if (val) {
        ASSERT(strcmp((const char *)val, path) == 0);
    }
}

/** Collision mesh path coexists with render mesh path. */
static void test_coexists_with_mesh_path(void) {
    entity_attrs_t attrs;
    entity_attrs_init(&attrs);

    const char *render = "humanoid.fvma";
    const char *collision = "humanoid_col.fvma";

    entity_attrs_set(&attrs, SCRIPT_KEY_MESH_PATH,
                     SCRIPT_ATTR_STR, render,
                     (uint8_t)(strlen(render) + 1));
    entity_attrs_set(&attrs, SCRIPT_KEY_COLLISION_MESH_PATH,
                     SCRIPT_ATTR_STR, collision,
                     (uint8_t)(strlen(collision) + 1));

    uint8_t type = 0, size = 0;
    const void *v1 = entity_attrs_get(&attrs, SCRIPT_KEY_MESH_PATH,
                                       &type, &size);
    ASSERT(v1 != NULL);
    if (v1) ASSERT(strcmp((const char *)v1, render) == 0);

    const void *v2 = entity_attrs_get(&attrs,
                                       SCRIPT_KEY_COLLISION_MESH_PATH,
                                       &type, &size);
    ASSERT(v2 != NULL);
    if (v2) ASSERT(strcmp((const char *)v2, collision) == 0);
}

/** Remove collision mesh path leaves render mesh path intact. */
static void test_remove_collision_path(void) {
    entity_attrs_t attrs;
    entity_attrs_init(&attrs);

    const char *render = "mesh.fvma";
    const char *collision = "col.fvma";

    entity_attrs_set(&attrs, SCRIPT_KEY_MESH_PATH,
                     SCRIPT_ATTR_STR, render,
                     (uint8_t)(strlen(render) + 1));
    entity_attrs_set(&attrs, SCRIPT_KEY_COLLISION_MESH_PATH,
                     SCRIPT_ATTR_STR, collision,
                     (uint8_t)(strlen(collision) + 1));

    entity_attrs_remove(&attrs, SCRIPT_KEY_COLLISION_MESH_PATH);

    uint8_t type = 0, size = 0;
    ASSERT(entity_attrs_get(&attrs, SCRIPT_KEY_COLLISION_MESH_PATH,
                             &type, &size) == NULL);

    const void *v = entity_attrs_get(&attrs, SCRIPT_KEY_MESH_PATH,
                                      &type, &size);
    ASSERT(v != NULL);
    if (v) ASSERT(strcmp((const char *)v, render) == 0);
}

/** Collision mesh path can coexist with skeleton path. */
static void test_coexists_with_skel_path(void) {
    entity_attrs_t attrs;
    entity_attrs_init(&attrs);

    const char *skel = "humanoid.fskel";
    const char *collision = "humanoid_col.fvma";

    entity_attrs_set(&attrs, SCRIPT_KEY_SKEL_PATH,
                     SCRIPT_ATTR_STR, skel,
                     (uint8_t)(strlen(skel) + 1));
    entity_attrs_set(&attrs, SCRIPT_KEY_COLLISION_MESH_PATH,
                     SCRIPT_ATTR_STR, collision,
                     (uint8_t)(strlen(collision) + 1));

    uint8_t type = 0, size = 0;
    const void *v1 = entity_attrs_get(&attrs, SCRIPT_KEY_SKEL_PATH,
                                       &type, &size);
    ASSERT(v1 != NULL);
    if (v1) ASSERT(strcmp((const char *)v1, skel) == 0);

    const void *v2 = entity_attrs_get(&attrs,
                                       SCRIPT_KEY_COLLISION_MESH_PATH,
                                       &type, &size);
    ASSERT(v2 != NULL);
    if (v2) ASSERT(strcmp((const char *)v2, collision) == 0);
}

/* ---- Main ---- */

int main(void) {
    printf("collision_mesh_path_tests:\n");

    test_key_value();
    test_set_and_get_path();
    test_coexists_with_mesh_path();
    test_remove_collision_path();
    test_coexists_with_skel_path();

    printf("collision_mesh_path_tests: %d passed, %d failed\n",
           g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
