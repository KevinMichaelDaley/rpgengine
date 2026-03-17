/**
 * @file asset_browser_query.c
 * @brief Asset browser query functions: visible count, get visible, command.
 *
 * Non-static functions (3 / 4 limit):
 *   asset_browser_visible_count
 *   asset_browser_get_visible
 *   asset_browser_get_spawn_command
 */

#include "ferrum/editor/panels/asset_browser.h"

#include <string.h>

/** Check if entry should be hidden due to collapsed parent section. */
static bool is_entry_hidden_(const asset_browser_t *browser,
                               const asset_browser_entry_t *entry) {
    /* Section headers are always visible. */
    if (entry->type == ASSET_ENTRY_SECTION_HEADER) return false;

    /* Children of collapsed sections are hidden. */
    if (entry->section_id < ASSET_BROWSER_MAX_SECTIONS &&
        browser->collapsed[entry->section_id]) {
        return true;
    }

    return false;
}

/** Check if entry passes the current filter. */
static bool passes_filter_(const asset_browser_t *browser,
                             const asset_browser_entry_t *entry) {
    /* Empty filter passes everything. */
    if (browser->filter[0] == '\0') return true;

    /* Section headers always pass. */
    if (entry->type == ASSET_ENTRY_SECTION_HEADER) return true;

    /* Case-insensitive substring match on name. */
    const char *filter = browser->filter;
    const char *name = entry->name;
    size_t flen = strlen(filter);
    size_t nlen = strlen(name);

    if (flen > nlen) return false;

    for (size_t i = 0; i <= nlen - flen; i++) {
        bool match = true;
        for (size_t j = 0; j < flen; j++) {
            char c1 = name[i + j];
            char c2 = filter[j];
            /* Lowercase compare. */
            if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
            if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
            if (c1 != c2) { match = false; break; }
        }
        if (match) return true;
    }

    return false;
}

uint32_t asset_browser_visible_count(const asset_browser_t *browser) {
    if (!browser || !browser->entries) return 0;

    uint32_t count = 0;
    for (uint32_t i = 0; i < browser->count; i++) {
        if (is_entry_hidden_(browser, &browser->entries[i])) continue;
        if (!passes_filter_(browser, &browser->entries[i])) continue;
        count++;
    }
    return count;
}

const asset_browser_entry_t *asset_browser_get_visible(
    const asset_browser_t *browser, uint32_t visible_index) {
    if (!browser || !browser->entries) return NULL;

    uint32_t vi = 0;
    for (uint32_t i = 0; i < browser->count; i++) {
        if (is_entry_hidden_(browser, &browser->entries[i])) continue;
        if (!passes_filter_(browser, &browser->entries[i])) continue;

        if (vi == visible_index) {
            return &browser->entries[i];
        }
        vi++;
    }
    return NULL;
}

const char *asset_browser_get_spawn_command(
    const asset_browser_entry_t *entry) {
    if (!entry) return NULL;

    if (entry->type == ASSET_ENTRY_SPAWN_ACTION) {
        return entry->path[0] ? entry->path : NULL;
    }

    /* For asset files, construct a "spawn mesh <path>" command. */
    if (entry->type == ASSET_ENTRY_ASSET_FILE) {
        return entry->path[0] ? entry->path : NULL;
    }

    return NULL;
}
