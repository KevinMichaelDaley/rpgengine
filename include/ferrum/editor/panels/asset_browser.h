/**
 * @file asset_browser.h
 * @brief Asset browser panel data model: built-in entity tree + asset tree.
 *
 * Provides a two-section tree view:
 *   1. Built-in entities (primitives, lights, cameras, markers)
 *   2. Asset directory tree (scanned from project assets directory)
 *
 * The tree is flat (entries store depth level). Collapsible sections
 * track expand/collapse state by section index.
 *
 * Thread safety: not thread-safe.
 * Ownership: asset_browser_t owns its entry arrays (heap-allocated).
 * Nullability: all pointer params must be non-NULL unless documented.
 *
 * Public types: asset_browser_entry_t, asset_browser_t (2-type rule).
 */
#ifndef FERRUM_EDITOR_PANELS_ASSET_BROWSER_H
#define FERRUM_EDITOR_PANELS_ASSET_BROWSER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/* Forward declaration. */
struct edit_asset_entry;

/** Maximum display name length for a browser entry. */
#define ASSET_BROWSER_NAME_MAX 128

/** Maximum path length for an asset entry. */
#define ASSET_BROWSER_PATH_MAX 256

/** Maximum number of collapsible sections tracked. */
#define ASSET_BROWSER_MAX_SECTIONS 64

/** Maximum visible entries (for static Clay context arrays). */
#define ASSET_BROWSER_MAX_VISIBLE 128

/** Maximum filter text length. */
#define ASSET_BROWSER_FILTER_MAX 128

/* ------------------------------------------------------------------ */
/* Entry types                                                         */
/* ------------------------------------------------------------------ */

/**
 * @brief Type of entry in the asset browser tree.
 */
typedef enum asset_browser_entry_type {
    ASSET_ENTRY_SECTION_HEADER = 0, /**< Collapsible section header. */
    ASSET_ENTRY_SPAWN_ACTION   = 1, /**< Click to spawn (built-in entity). */
    ASSET_ENTRY_DIRECTORY      = 2, /**< Collapsible directory node. */
    ASSET_ENTRY_ASSET_FILE     = 3, /**< Clickable asset file. */
} asset_browser_entry_type_t;

/**
 * @brief A single entry in the asset browser flat tree.
 */
typedef struct asset_browser_entry {
    char     name[ASSET_BROWSER_NAME_MAX]; /**< Display name. */
    char     path[ASSET_BROWSER_PATH_MAX]; /**< Spawn command or asset path. */
    uint8_t  type;       /**< asset_browser_entry_type_t. */
    uint8_t  depth;      /**< Indent level (0 = root section). */
    uint16_t section_id; /**< Section index for collapse tracking. */
    uint8_t  asset_type; /**< edit_asset_type_t for ASSET_FILE entries (0 otherwise). */
} asset_browser_entry_t;

/**
 * @brief Asset browser state.
 *
 * Holds the flat entry list, collapse state, scroll position,
 * and filter text.
 */
typedef struct asset_browser {
    asset_browser_entry_t *entries;    /**< Flat entry array. */
    uint32_t               count;     /**< Number of entries. */
    uint32_t               capacity;  /**< Array capacity. */

    /** Per-section expanded state. True = expanded. */
    bool collapsed[ASSET_BROWSER_MAX_SECTIONS];
    uint16_t section_count;           /**< Number of sections used. */

    int  scroll;                      /**< Scroll offset (rows). */
    char filter[ASSET_BROWSER_FILTER_MAX]; /**< Filter text. */
} asset_browser_t;

/* ------------------------------------------------------------------ */
/* Lifecycle (asset_browser.c)                                         */
/* ------------------------------------------------------------------ */

/**
 * @brief Initialize the asset browser with default built-in entries.
 *
 * Populates the built-in entities tree (primitives, lights, cameras,
 * markers). Asset directory entries must be added separately.
 *
 * @param browser   Browser to initialize. Must not be NULL.
 * @param capacity  Maximum total entries.
 */
void asset_browser_init(asset_browser_t *browser, uint32_t capacity);

/**
 * @brief Destroy the asset browser and free all memory.
 * @param browser  Browser to destroy. Safe to call on zeroed struct.
 */
void asset_browser_destroy(asset_browser_t *browser);

/**
 * @brief Toggle a section's collapsed state.
 * @param browser     Browser.
 * @param section_id  Section index.
 */
void asset_browser_toggle_section(asset_browser_t *browser,
                                    uint16_t section_id);

/**
 * @brief Check if a section is collapsed.
 * @param browser     Browser.
 * @param section_id  Section index.
 * @return true if collapsed.
 */
bool asset_browser_is_collapsed(const asset_browser_t *browser,
                                  uint16_t section_id);

/* ------------------------------------------------------------------ */
/* Population (asset_browser_populate.c)                               */
/* ------------------------------------------------------------------ */

/**
 * @brief Add an entry to the browser.
 *
 * @param browser  Browser.
 * @param entry    Entry to add (copied).
 * @return true on success, false if full.
 */
bool asset_browser_add_entry(asset_browser_t *browser,
                               const asset_browser_entry_t *entry);

/**
 * @brief Populate the asset directory tree from a registry.
 *
 * Scans the registry and adds directory + file entries under
 * the "Project Assets" section.
 *
 * @param browser   Browser.
 * @param entries   Asset registry entries array.
 * @param count     Number of registry entries.
 */
void asset_browser_populate_from_registry(
    asset_browser_t *browser,
    const struct edit_asset_entry *entries,
    uint32_t count);

/* ------------------------------------------------------------------ */
/* Queries (asset_browser_query.c)                                     */
/* ------------------------------------------------------------------ */

/**
 * @brief Count visible entries (excluding collapsed children).
 *
 * @param browser  Browser.
 * @return Number of entries that should be displayed.
 */
uint32_t asset_browser_visible_count(const asset_browser_t *browser);

/**
 * @brief Get the Nth visible entry (skipping collapsed sections).
 *
 * @param browser        Browser.
 * @param visible_index  Index among visible entries.
 * @return Pointer to entry, or NULL if out of range.
 */
const asset_browser_entry_t *asset_browser_get_visible(
    const asset_browser_t *browser, uint32_t visible_index);

/**
 * @brief Get the spawn command for a built-in entity entry.
 *
 * @param entry  Entry of type ASSET_ENTRY_SPAWN_ACTION.
 * @return The TUI command string (stored in entry->path), or NULL.
 */
const char *asset_browser_get_spawn_command(
    const asset_browser_entry_t *entry);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_PANELS_ASSET_BROWSER_H */
