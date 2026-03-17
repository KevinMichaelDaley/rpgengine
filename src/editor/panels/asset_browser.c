/**
 * @file asset_browser.c
 * @brief Asset browser lifecycle: init, destroy, toggle, is_collapsed.
 *
 * Populates the built-in entity tree on init with primitives,
 * lights, cameras, and markers sections.
 *
 * Non-static functions (4 / 4 limit):
 *   asset_browser_init
 *   asset_browser_destroy
 *   asset_browser_toggle_section
 *   asset_browser_is_collapsed
 */

#include "ferrum/editor/panels/asset_browser.h"

#include <stdlib.h>
#include <string.h>

/* ---- Helpers ---- */

/** Add a section header to the browser. */
static void add_section_(asset_browser_t *b, const char *name,
                          uint16_t section_id) {
    if (b->count >= b->capacity) return;

    asset_browser_entry_t *e = &b->entries[b->count++];
    memset(e, 0, sizeof(*e));
    strncpy(e->name, name, ASSET_BROWSER_NAME_MAX - 1);
    e->type = ASSET_ENTRY_SECTION_HEADER;
    e->depth = 0;
    e->section_id = section_id;
}

/** Add a spawn action entry to the browser. */
static void add_spawn_(asset_browser_t *b, const char *name,
                         const char *command, uint16_t section_id) {
    if (b->count >= b->capacity) return;

    asset_browser_entry_t *e = &b->entries[b->count++];
    memset(e, 0, sizeof(*e));
    strncpy(e->name, name, ASSET_BROWSER_NAME_MAX - 1);
    strncpy(e->path, command, ASSET_BROWSER_PATH_MAX - 1);
    e->type = ASSET_ENTRY_SPAWN_ACTION;
    e->depth = 1;
    e->section_id = section_id;
}

/* ---- Public API ---- */

void asset_browser_init(asset_browser_t *browser, uint32_t capacity) {
    if (!browser) return;

    memset(browser, 0, sizeof(*browser));
    browser->capacity = capacity;

    if (capacity == 0) {
        browser->entries = NULL;
        return;
    }

    browser->entries = (asset_browser_entry_t *)calloc(
        capacity, sizeof(asset_browser_entry_t));
    if (!browser->entries) {
        browser->capacity = 0;
        return;
    }

    /* Populate built-in entities tree. */

    /* Section 0: Primitives */
    uint16_t sec = 0;
    add_section_(browser, "Primitives", sec);
    add_spawn_(browser, "Box",       "spawn box",       sec);
    add_spawn_(browser, "Sphere",    "spawn sphere",    sec);
    add_spawn_(browser, "Capsule",   "spawn capsule",   sec);
    add_spawn_(browser, "Halfspace", "spawn halfspace", sec);
    browser->section_count = sec + 1;

    /* Section 1: Markers */
    sec = 1;
    add_section_(browser, "Markers", sec);
    add_spawn_(browser, "Marker", "spawn marker", sec);
    browser->section_count = sec + 1;
}

void asset_browser_destroy(asset_browser_t *browser) {
    if (!browser) return;
    free(browser->entries);
    memset(browser, 0, sizeof(*browser));
}

void asset_browser_toggle_section(asset_browser_t *browser,
                                    uint16_t section_id) {
    if (!browser) return;
    if (section_id >= ASSET_BROWSER_MAX_SECTIONS) return;

    browser->collapsed[section_id] = !browser->collapsed[section_id];
}

bool asset_browser_is_collapsed(const asset_browser_t *browser,
                                  uint16_t section_id) {
    if (!browser) return false;
    if (section_id >= ASSET_BROWSER_MAX_SECTIONS) return false;

    return browser->collapsed[section_id];
}
