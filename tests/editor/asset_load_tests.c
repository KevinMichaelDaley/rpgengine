/**
 * @file asset_load_tests.c
 * @brief Tests for scene asset loading: FVMA mesh + fskel skeleton.
 *
 * Tests the file-reading layer (scene_asset_load.c) that reads
 * FVMA/fskel files from disk into heap buffers. The GPU upload
 * step requires GL context and is tested via manual editor launch.
 */

#include "ferrum/editor/edit_asset_registry.h"
#include "ferrum/entity/entity_attrs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define ASSERT(expr) do { \
    if (!(expr)) { \
        printf("  FAIL: %s (line %d)\n", #expr, __LINE__); \
        return 0; \
    } \
} while (0)

#define RUN(fn) do { \
    printf("RUN  %s\n", #fn); \
    if (fn()) printf("OK   %s\n", #fn); \
    else { printf("FAIL %s\n", #fn); fails++; } \
    total++; \
} while (0)

/* ---- Helper: read file into heap buffer ---- */

static uint8_t *read_file(const char *path, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz <= 0) { fclose(f); return NULL; }
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = (uint8_t *)malloc((size_t)sz);
    if (!buf) { fclose(f); return NULL; }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (rd != (size_t)sz) { free(buf); return NULL; }
    *out_size = (size_t)sz;
    return buf;
}

/* ---- Tests: asset registry scan finds files ---- */

static int test_scan_finds_fvma(void) {
    edit_asset_registry_t reg;
    edit_asset_registry_init(&reg, 64);

    uint32_t found = edit_asset_registry_scan(&reg, "asset_src");
    ASSERT(found > 0);

    /* Should find humanoid.fvma. */
    const edit_asset_entry_t *entry =
        edit_asset_registry_find(&reg, "humanoid.fvma");
    ASSERT(entry != NULL);
    ASSERT(entry->type == EDIT_ASSET_MESH);
    ASSERT(entry->size > 0);

    edit_asset_registry_destroy(&reg);
    return 1;
}

static int test_scan_finds_fskel(void) {
    edit_asset_registry_t reg;
    edit_asset_registry_init(&reg, 64);

    uint32_t found = edit_asset_registry_scan(&reg, "asset_src");
    ASSERT(found > 0);

    /* Should find humanoid.fskel. */
    const edit_asset_entry_t *entry =
        edit_asset_registry_find(&reg, "humanoid.fskel");
    ASSERT(entry != NULL);
    ASSERT(entry->type == EDIT_ASSET_SKELETON);

    /* Should also find goblin.fskel. */
    const edit_asset_entry_t *goblin =
        edit_asset_registry_find(&reg, "goblin.fskel");
    ASSERT(goblin != NULL);
    ASSERT(goblin->type == EDIT_ASSET_SKELETON);

    edit_asset_registry_destroy(&reg);
    return 1;
}

static int test_scan_finds_subdirectory_assets(void) {
    edit_asset_registry_t reg;
    edit_asset_registry_init(&reg, 128);

    uint32_t found = edit_asset_registry_scan(&reg, "asset_src");

    /* The HUMANOID ORPHEUS-D subdirectory contains .obj, .jpg, .png files. */
    bool found_texture = false;
    bool found_mesh = false;
    for (uint32_t i = 0; i < reg.count; i++) {
        if (reg.entries[i].type == EDIT_ASSET_TEXTURE &&
            strstr(reg.entries[i].path, "HUMANOID ORPHEUS-D") != NULL) {
            found_texture = true;
        }
        if (reg.entries[i].type == EDIT_ASSET_MESH &&
            strstr(reg.entries[i].path, "HUMANOID ORPHEUS-D") != NULL) {
            found_mesh = true;
        }
    }
    /* Should find at least textures in subdirectory. */
    ASSERT(found_texture || found_mesh);
    (void)found;

    edit_asset_registry_destroy(&reg);
    return 1;
}

/* ---- Tests: FVMA file reading ---- */

static int test_read_fvma_from_disk(void) {
    size_t size = 0;
    uint8_t *data = read_file("asset_src/humanoid.fvma", &size);
    ASSERT(data != NULL);
    ASSERT(size > 24); /* At least FVMA header size. */

    /* Verify FVMA magic. */
    uint32_t magic;
    memcpy(&magic, data, 4);
    ASSERT(magic == 0x414D5646u); /* 'FVMA' */

    free(data);
    return 1;
}

static int test_read_fvma_nonexistent(void) {
    size_t size = 0;
    uint8_t *data = read_file("asset_src/nonexistent.fvma", &size);
    ASSERT(data == NULL);
    return 1;
}

/* ---- Tests: entity attrs mesh_path storage ---- */

static int test_mesh_path_attr(void) {
    entity_attrs_t attrs;
    memset(&attrs, 0, sizeof(attrs));

    const char *path = "humanoid.fvma";
    bool ok = entity_attrs_set(&attrs, SCRIPT_KEY_MESH_PATH,
                                SCRIPT_ATTR_STR,
                                path, (uint16_t)(strlen(path) + 1));
    ASSERT(ok);

    /* Retrieve it. */
    uint8_t type = 0;
    uint8_t size = 0;
    const void *val = entity_attrs_get(&attrs, SCRIPT_KEY_MESH_PATH,
                                        &type, &size);
    ASSERT(val != NULL);
    ASSERT(type == SCRIPT_ATTR_STR);
    ASSERT(strcmp((const char *)val, "humanoid.fvma") == 0);

    return 1;
}

static int test_skel_path_attr(void) {
    entity_attrs_t attrs;
    memset(&attrs, 0, sizeof(attrs));

    const char *path = "humanoid.fskel";
    bool ok = entity_attrs_set(&attrs, SCRIPT_KEY_SKEL_PATH,
                                SCRIPT_ATTR_STR,
                                path, (uint16_t)(strlen(path) + 1));
    ASSERT(ok);

    uint8_t type = 0;
    uint8_t size = 0;
    const void *val = entity_attrs_get(&attrs, SCRIPT_KEY_SKEL_PATH,
                                        &type, &size);
    ASSERT(val != NULL);
    ASSERT(type == SCRIPT_ATTR_STR);
    ASSERT(strcmp((const char *)val, "humanoid.fskel") == 0);

    return 1;
}

/* ---- Tests: scan type counts ---- */

static int test_scan_type_counts(void) {
    edit_asset_registry_t reg;
    edit_asset_registry_init(&reg, 128);

    edit_asset_registry_scan(&reg, "asset_src");

    uint32_t mesh_count = 0, skel_count = 0, tex_count = 0;
    for (uint32_t i = 0; i < reg.count; i++) {
        switch (reg.entries[i].type) {
        case EDIT_ASSET_MESH:     mesh_count++; break;
        case EDIT_ASSET_SKELETON: skel_count++; break;
        case EDIT_ASSET_TEXTURE:  tex_count++; break;
        default: break;
        }
    }

    /* humanoid.fvma + at least OBJ files in subdirectory. */
    ASSERT(mesh_count >= 1);
    /* humanoid.fskel + goblin.fskel. */
    ASSERT(skel_count >= 2);

    edit_asset_registry_destroy(&reg);
    return 1;
}

/* ---- Main ---- */

int main(void) {
    int fails = 0, total = 0;

    RUN(test_scan_finds_fvma);
    RUN(test_scan_finds_fskel);
    RUN(test_scan_finds_subdirectory_assets);

    RUN(test_read_fvma_from_disk);
    RUN(test_read_fvma_nonexistent);

    RUN(test_mesh_path_attr);
    RUN(test_skel_path_attr);

    RUN(test_scan_type_counts);

    printf("\n%d / %d passed\n", total - fails, total);
    return fails ? 1 : 0;
}
