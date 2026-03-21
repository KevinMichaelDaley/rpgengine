/**
 * @file asset_browser_query.c
 * @brief Asset browser query functions: visible count, get visible, command.
 *
 * Uses a cached visible-index array to avoid O(n) scans per query.
 * The cache is rebuilt once per frame (when collapse/filter state changes).
 *
 * Non-static functions (3 / 4 limit):
 *   asset_browser_visible_count
 *   asset_browser_get_visible
 *   asset_browser_get_spawn_command
 */

#include "ferrum/editor/panels/asset_browser.h"

#include <string.h>

/* ---- Visible index cache ---- */

/** Maximum cached visible entries. */
#define VIS_CACHE_MAX 4096

/** Cached mapping from visible index → raw entry index. */
static uint32_t s_vis_indices[VIS_CACHE_MAX];
static uint32_t s_vis_count;

/** Fingerprint of browser state used to detect invalidation. */
static uint32_t s_cache_entry_count;
static char     s_cache_filter[ASSET_BROWSER_FILTER_MAX];
static bool     s_cache_collapsed[ASSET_BROWSER_MAX_SECTIONS];
static const asset_browser_entry_t *s_cache_entries_ptr;

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

/** Rebuild the visible index cache if browser state has changed. */
static void ensure_cache_(const asset_browser_t *browser) {
    /* Check if cache is still valid. */
    if (s_cache_entries_ptr == browser->entries &&
        s_cache_entry_count == browser->count &&
        memcmp(s_cache_filter, browser->filter,
               ASSET_BROWSER_FILTER_MAX) == 0 &&
        memcmp(s_cache_collapsed, browser->collapsed,
               sizeof(s_cache_collapsed)) == 0) {
        return; /* Cache hit — nothing changed. */
    }

    /* Rebuild cache. */
    s_vis_count = 0;
    for (uint32_t i = 0; i < browser->count && s_vis_count < VIS_CACHE_MAX; i++) {
        if (is_entry_hidden_(browser, &browser->entries[i])) continue;
        if (!passes_filter_(browser, &browser->entries[i])) continue;
        s_vis_indices[s_vis_count++] = i;
    }

    /* Store fingerprint. */
    s_cache_entries_ptr = browser->entries;
    s_cache_entry_count = browser->count;
    memcpy(s_cache_filter, browser->filter, ASSET_BROWSER_FILTER_MAX);
    memcpy(s_cache_collapsed, browser->collapsed, sizeof(s_cache_collapsed));
}

uint32_t asset_browser_visible_count(const asset_browser_t *browser) {
    if (!browser || !browser->entries) return 0;
    ensure_cache_(browser);
    return s_vis_count;
}

const asset_browser_entry_t *asset_browser_get_visible(
    const asset_browser_t *browser, uint32_t visible_index) {
    if (!browser || !browser->entries) return NULL;
    ensure_cache_(browser);
    if (visible_index >= s_vis_count) return NULL;
    return &browser->entries[s_vis_indices[visible_index]];
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
