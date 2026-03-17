/**
 * @file asset_browser_tests.c
 * @brief Tests for asset browser data model.
 *
 * Verifies init/destroy, built-in entity tree, section collapse,
 * visible entry counting, asset population, and edge cases.
 */

#include "ferrum/editor/panels/asset_browser.h"
#include "ferrum/editor/edit_asset_registry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/* ---- Tests: init/destroy ---- */

static int test_init_has_builtin_entries(void) {
    asset_browser_t browser;
    asset_browser_init(&browser, 256);

    /* Should have some built-in entries (primitives section + items). */
    ASSERT(browser.count > 0);

    /* First entry should be a section header (Primitives). */
    ASSERT(browser.entries[0].type == ASSET_ENTRY_SECTION_HEADER);

    /* Should have at least the 4 primitive types. */
    uint32_t spawn_count = 0;
    for (uint32_t i = 0; i < browser.count; i++) {
        if (browser.entries[i].type == ASSET_ENTRY_SPAWN_ACTION) {
            spawn_count++;
        }
    }
    ASSERT(spawn_count >= 4); /* box, sphere, capsule, halfspace */

    asset_browser_destroy(&browser);
    return 1;
}

static int test_destroy_safe(void) {
    asset_browser_t browser;
    memset(&browser, 0, sizeof(browser));
    asset_browser_destroy(&browser); /* should not crash */

    asset_browser_init(&browser, 16);
    asset_browser_destroy(&browser);
    asset_browser_destroy(&browser); /* double destroy safe */
    return 1;
}

/* ---- Tests: section collapse ---- */

static int test_section_collapse_toggle(void) {
    asset_browser_t browser;
    asset_browser_init(&browser, 256);

    /* Sections should default to expanded (not collapsed). */
    ASSERT(!asset_browser_is_collapsed(&browser, 0));

    /* Toggle to collapsed. */
    asset_browser_toggle_section(&browser, 0);
    ASSERT(asset_browser_is_collapsed(&browser, 0));

    /* Toggle back. */
    asset_browser_toggle_section(&browser, 0);
    ASSERT(!asset_browser_is_collapsed(&browser, 0));

    asset_browser_destroy(&browser);
    return 1;
}

static int test_collapse_reduces_visible(void) {
    asset_browser_t browser;
    asset_browser_init(&browser, 256);

    uint32_t all_visible = asset_browser_visible_count(&browser);

    /* Collapse the first section. */
    asset_browser_toggle_section(&browser, 0);
    uint32_t collapsed_visible = asset_browser_visible_count(&browser);

    /* Should have fewer visible entries. */
    ASSERT(collapsed_visible < all_visible);

    /* The section header should still be visible. */
    ASSERT(collapsed_visible > 0);

    asset_browser_destroy(&browser);
    return 1;
}

/* ---- Tests: visible entries ---- */

static int test_visible_entries_expanded(void) {
    asset_browser_t browser;
    asset_browser_init(&browser, 256);

    /* All entries should be visible when expanded. */
    uint32_t visible = asset_browser_visible_count(&browser);
    ASSERT(visible == browser.count);

    /* Get each visible entry. */
    for (uint32_t i = 0; i < visible; i++) {
        const asset_browser_entry_t *e = asset_browser_get_visible(&browser, i);
        ASSERT(e != NULL);
    }

    /* Out of range returns NULL. */
    ASSERT(asset_browser_get_visible(&browser, visible) == NULL);

    asset_browser_destroy(&browser);
    return 1;
}

static int test_visible_entries_collapsed(void) {
    asset_browser_t browser;
    asset_browser_init(&browser, 256);

    /* Collapse section 0 (Primitives). */
    asset_browser_toggle_section(&browser, 0);

    /* First visible should be the section header. */
    const asset_browser_entry_t *first = asset_browser_get_visible(&browser, 0);
    ASSERT(first != NULL);
    ASSERT(first->type == ASSET_ENTRY_SECTION_HEADER);
    ASSERT(first->section_id == 0);

    /* Second visible should be a different section header (not a child of 0). */
    const asset_browser_entry_t *second = asset_browser_get_visible(&browser, 1);
    if (second) {
        /* Should not be depth > 0 with section_id 0. */
        ASSERT(second->section_id != 0 || second->type == ASSET_ENTRY_SECTION_HEADER);
    }

    asset_browser_destroy(&browser);
    return 1;
}

/* ---- Tests: add entry ---- */

static int test_add_entry(void) {
    asset_browser_t browser;
    asset_browser_init(&browser, 256);

    uint32_t orig_count = browser.count;

    asset_browser_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    strncpy(entry.name, "Test Asset", sizeof(entry.name) - 1);
    strncpy(entry.path, "assets/test.fvma", sizeof(entry.path) - 1);
    entry.type = ASSET_ENTRY_ASSET_FILE;
    entry.depth = 1;
    entry.section_id = 0;

    ASSERT(asset_browser_add_entry(&browser, &entry));
    ASSERT(browser.count == orig_count + 1);

    asset_browser_destroy(&browser);
    return 1;
}

static int test_add_entry_full(void) {
    asset_browser_t browser;
    asset_browser_init(&browser, 4);

    /* Fill to capacity (init already adds some entries). */
    asset_browser_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    entry.type = ASSET_ENTRY_SPAWN_ACTION;

    /* Keep adding until full. */
    while (browser.count < browser.capacity) {
        ASSERT(asset_browser_add_entry(&browser, &entry));
    }

    /* Should fail when full. */
    ASSERT(!asset_browser_add_entry(&browser, &entry));

    asset_browser_destroy(&browser);
    return 1;
}

/* ---- Tests: spawn command ---- */

static int test_spawn_command(void) {
    asset_browser_t browser;
    asset_browser_init(&browser, 256);

    /* Find the first spawn action entry. */
    const asset_browser_entry_t *spawn_entry = NULL;
    for (uint32_t i = 0; i < browser.count; i++) {
        if (browser.entries[i].type == ASSET_ENTRY_SPAWN_ACTION) {
            spawn_entry = &browser.entries[i];
            break;
        }
    }
    ASSERT(spawn_entry != NULL);

    const char *cmd = asset_browser_get_spawn_command(spawn_entry);
    ASSERT(cmd != NULL);
    ASSERT(strlen(cmd) > 0);

    asset_browser_destroy(&browser);
    return 1;
}

static int test_spawn_command_wrong_type(void) {
    asset_browser_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    entry.type = ASSET_ENTRY_SECTION_HEADER;

    /* Should return NULL for non-spawn entries. */
    ASSERT(asset_browser_get_spawn_command(&entry) == NULL);
    return 1;
}

/* ---- Tests: asset registry population ---- */

static int test_populate_from_registry(void) {
    asset_browser_t browser;
    asset_browser_init(&browser, 256);

    uint32_t before = browser.count;

    /* Create mock registry entries. */
    edit_asset_entry_t reg_entries[3];
    memset(reg_entries, 0, sizeof(reg_entries));

    strncpy(reg_entries[0].path, "meshes/pillar.fvma",
            EDIT_ASSET_PATH_MAX - 1);
    reg_entries[0].type = EDIT_ASSET_MESH;

    strncpy(reg_entries[1].path, "meshes/wall.fvma",
            EDIT_ASSET_PATH_MAX - 1);
    reg_entries[1].type = EDIT_ASSET_MESH;

    strncpy(reg_entries[2].path, "textures/brick.png",
            EDIT_ASSET_PATH_MAX - 1);
    reg_entries[2].type = EDIT_ASSET_TEXTURE;

    asset_browser_populate_from_registry(&browser, reg_entries, 3);

    /* Should have added a section header + entries. */
    ASSERT(browser.count > before);

    /* Find the added mesh entries. */
    bool found_pillar = false;
    bool found_wall = false;
    for (uint32_t i = 0; i < browser.count; i++) {
        if (strstr(browser.entries[i].name, "pillar")) found_pillar = true;
        if (strstr(browser.entries[i].name, "wall")) found_wall = true;
    }
    ASSERT(found_pillar);
    ASSERT(found_wall);

    asset_browser_destroy(&browser);
    return 1;
}

/* ---- Tests: type-grouped population ---- */

static int test_populate_groups_by_type(void) {
    asset_browser_t browser;
    asset_browser_init(&browser, 256);

    /* Create entries with multiple types. */
    edit_asset_entry_t reg[5];
    memset(reg, 0, sizeof(reg));

    strncpy(reg[0].path, "meshes/pillar.fvma", EDIT_ASSET_PATH_MAX - 1);
    reg[0].type = EDIT_ASSET_MESH;

    strncpy(reg[1].path, "humanoid.fskel", EDIT_ASSET_PATH_MAX - 1);
    reg[1].type = EDIT_ASSET_SKELETON;

    strncpy(reg[2].path, "stone.fmat", EDIT_ASSET_PATH_MAX - 1);
    reg[2].type = EDIT_ASSET_MATERIAL;

    strncpy(reg[3].path, "brick.png", EDIT_ASSET_PATH_MAX - 1);
    reg[3].type = EDIT_ASSET_TEXTURE;

    strncpy(reg[4].path, "meshes/wall.fvma", EDIT_ASSET_PATH_MAX - 1);
    reg[4].type = EDIT_ASSET_MESH;

    asset_browser_populate_from_registry(&browser, reg, 5);

    /* Should have 4 section headers (Meshes, Skeletons, Materials, Textures). */
    uint32_t section_headers = 0;
    bool found_meshes = false, found_skeletons = false;
    bool found_materials = false, found_textures = false;
    for (uint32_t i = 0; i < browser.count; i++) {
        if (browser.entries[i].type == ASSET_ENTRY_SECTION_HEADER) {
            /* Only count new sections (skip built-in Primitives/Markers). */
            if (strcmp(browser.entries[i].name, "Meshes") == 0) {
                found_meshes = true; section_headers++;
            }
            if (strcmp(browser.entries[i].name, "Skeletons") == 0) {
                found_skeletons = true; section_headers++;
            }
            if (strcmp(browser.entries[i].name, "Materials") == 0) {
                found_materials = true; section_headers++;
            }
            if (strcmp(browser.entries[i].name, "Textures") == 0) {
                found_textures = true; section_headers++;
            }
        }
    }
    ASSERT(found_meshes);
    ASSERT(found_skeletons);
    ASSERT(found_materials);
    ASSERT(found_textures);
    ASSERT(section_headers == 4);

    asset_browser_destroy(&browser);
    return 1;
}

static int test_populate_asset_type_field(void) {
    asset_browser_t browser;
    asset_browser_init(&browser, 256);

    edit_asset_entry_t reg[2];
    memset(reg, 0, sizeof(reg));

    strncpy(reg[0].path, "pillar.fvma", EDIT_ASSET_PATH_MAX - 1);
    reg[0].type = EDIT_ASSET_MESH;

    strncpy(reg[1].path, "humanoid.fskel", EDIT_ASSET_PATH_MAX - 1);
    reg[1].type = EDIT_ASSET_SKELETON;

    asset_browser_populate_from_registry(&browser, reg, 2);

    /* Find the mesh entry and verify asset_type is set. */
    bool found_mesh = false, found_skel = false;
    for (uint32_t i = 0; i < browser.count; i++) {
        if (browser.entries[i].type == ASSET_ENTRY_ASSET_FILE) {
            if (strcmp(browser.entries[i].name, "pillar.fvma") == 0) {
                ASSERT(browser.entries[i].asset_type == EDIT_ASSET_MESH);
                found_mesh = true;
            }
            if (strcmp(browser.entries[i].name, "humanoid.fskel") == 0) {
                ASSERT(browser.entries[i].asset_type == EDIT_ASSET_SKELETON);
                found_skel = true;
            }
        }
    }
    ASSERT(found_mesh);
    ASSERT(found_skel);

    asset_browser_destroy(&browser);
    return 1;
}

static int test_populate_skips_empty_types(void) {
    asset_browser_t browser;
    asset_browser_init(&browser, 256);

    /* Only meshes — no Skeletons/Materials/etc sections. */
    edit_asset_entry_t reg[1];
    memset(reg, 0, sizeof(reg));
    strncpy(reg[0].path, "pillar.fvma", EDIT_ASSET_PATH_MAX - 1);
    reg[0].type = EDIT_ASSET_MESH;

    asset_browser_populate_from_registry(&browser, reg, 1);

    bool found_skeletons = false;
    for (uint32_t i = 0; i < browser.count; i++) {
        if (browser.entries[i].type == ASSET_ENTRY_SECTION_HEADER &&
            strcmp(browser.entries[i].name, "Skeletons") == 0) {
            found_skeletons = true;
        }
    }
    ASSERT(!found_skeletons);

    asset_browser_destroy(&browser);
    return 1;
}

static int test_populate_null_inputs(void) {
    asset_browser_t browser;
    asset_browser_init(&browser, 256);
    uint32_t before = browser.count;

    /* NULL entries should be a no-op. */
    asset_browser_populate_from_registry(&browser, NULL, 5);
    ASSERT(browser.count == before);

    /* Zero count should be a no-op. */
    edit_asset_entry_t reg[1];
    memset(reg, 0, sizeof(reg));
    asset_browser_populate_from_registry(&browser, reg, 0);
    ASSERT(browser.count == before);

    asset_browser_destroy(&browser);
    return 1;
}

/* ---- Tests: edge cases ---- */

static int test_out_of_range_section(void) {
    asset_browser_t browser;
    asset_browser_init(&browser, 256);

    /* Out-of-range section toggle should not crash. */
    asset_browser_toggle_section(&browser, ASSET_BROWSER_MAX_SECTIONS);
    ASSERT(!asset_browser_is_collapsed(&browser, ASSET_BROWSER_MAX_SECTIONS));

    asset_browser_destroy(&browser);
    return 1;
}

static int test_empty_filter(void) {
    asset_browser_t browser;
    asset_browser_init(&browser, 256);

    /* Empty filter shows all. */
    browser.filter[0] = '\0';
    uint32_t visible = asset_browser_visible_count(&browser);
    ASSERT(visible == browser.count);

    asset_browser_destroy(&browser);
    return 1;
}

/* ---- Main ---- */

int main(void) {
    int fails = 0, total = 0;

    RUN(test_init_has_builtin_entries);
    RUN(test_destroy_safe);

    RUN(test_section_collapse_toggle);
    RUN(test_collapse_reduces_visible);

    RUN(test_visible_entries_expanded);
    RUN(test_visible_entries_collapsed);

    RUN(test_add_entry);
    RUN(test_add_entry_full);

    RUN(test_spawn_command);
    RUN(test_spawn_command_wrong_type);

    RUN(test_populate_from_registry);
    RUN(test_populate_groups_by_type);
    RUN(test_populate_asset_type_field);
    RUN(test_populate_skips_empty_types);
    RUN(test_populate_null_inputs);

    RUN(test_out_of_range_section);
    RUN(test_empty_filter);

    printf("\n%d / %d passed\n", total - fails, total);
    return fails ? 1 : 0;
}
