/**
 * @file asset_browser_populate.c
 * @brief Asset browser entry addition and registry population.
 *
 * Populates the asset browser from a scanned registry, grouping
 * entries by asset type into separate collapsible sections:
 *   - Meshes (.glb, .obj, .fvma)
 *   - Skeletons (.fskel)
 *   - Materials (.mat, .fmat)
 *   - Textures (.png, .jpg, .ktx2)
 *   - Prefabs (.prefab)
 *   - Scripts (.wren, .ed)
 *
 * Non-static functions (2 / 4 limit):
 *   asset_browser_add_entry
 *   asset_browser_populate_from_registry
 */

#include "ferrum/editor/panels/asset_browser.h"
#include "ferrum/editor/edit_asset_registry.h"

#include <string.h>

bool asset_browser_add_entry(asset_browser_t *browser,
                               const asset_browser_entry_t *entry) {
    if (!browser || !entry) return false;
    if (!browser->entries) return false;
    if (browser->count >= browser->capacity) return false;

    browser->entries[browser->count++] = *entry;
    return true;
}

/** Section definition: display name and matching asset type. */
typedef struct {
    const char       *name;
    edit_asset_type_t type;
} section_def_t;

/** Add entries of a given type under a new section. */
static void add_type_section_(asset_browser_t *browser,
                                const edit_asset_entry_t *entries,
                                uint32_t count,
                                const section_def_t *def) {
    /* Count matching entries first — skip section if none. */
    uint32_t matching = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (entries[i].type == def->type) matching++;
    }
    if (matching == 0) return;

    /* Allocate a section ID. */
    uint16_t sec = browser->section_count;
    if (sec >= ASSET_BROWSER_MAX_SECTIONS) return;

    /* Add section header. */
    asset_browser_entry_t header;
    memset(&header, 0, sizeof(header));
    strncpy(header.name, def->name, ASSET_BROWSER_NAME_MAX - 1);
    header.type = ASSET_ENTRY_SECTION_HEADER;
    header.depth = 0;
    header.section_id = sec;
    asset_browser_add_entry(browser, &header);
    browser->section_count = sec + 1;

    /* Add matching entries. */
    for (uint32_t i = 0; i < count; i++) {
        if (entries[i].type != def->type) continue;

        asset_browser_entry_t e;
        memset(&e, 0, sizeof(e));

        /* Extract filename from path for display name. */
        const char *path = entries[i].path;
        const char *slash = strrchr(path, '/');
        const char *name = slash ? slash + 1 : path;
        strncpy(e.name, name, ASSET_BROWSER_NAME_MAX - 1);
        strncpy(e.path, path, ASSET_BROWSER_PATH_MAX - 1);

        e.type = ASSET_ENTRY_ASSET_FILE;
        e.depth = 1;
        e.section_id = sec;
        e.asset_type = (uint8_t)entries[i].type;

        asset_browser_add_entry(browser, &e);
    }
}

void asset_browser_populate_from_registry(
    asset_browser_t *browser,
    const edit_asset_entry_t *entries,
    uint32_t count) {
    if (!browser || !entries || count == 0) return;

    /* Define sections in display order. */
    static const section_def_t sections[] = {
        {"Meshes",     EDIT_ASSET_MESH},
        {"Skeletons",  EDIT_ASSET_SKELETON},
        {"Materials",  EDIT_ASSET_MATERIAL},
        {"Textures",   EDIT_ASSET_TEXTURE},
        {"Prefabs",    EDIT_ASSET_PREFAB},
        {"Scripts",    EDIT_ASSET_SCRIPT},
    };
    static const uint32_t section_count =
        sizeof(sections) / sizeof(sections[0]);

    for (uint32_t s = 0; s < section_count; s++) {
        add_type_section_(browser, entries, count, &sections[s]);
    }
}
