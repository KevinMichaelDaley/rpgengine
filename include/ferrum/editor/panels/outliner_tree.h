/**
 * @file outliner_tree.h
 * @brief Outliner tree model — flat list of entities with filtering.
 *
 * Builds a display list from the entity store, supports text filtering
 * and scroll offset for rendering.
 *
 * Ownership: init() allocates internal arrays; destroy() frees them.
 * Nullability: all pointer params must be non-NULL.
 * Error semantics: get() returns NULL on out-of-bounds.
 * Side effects: rebuild() reads entity store.
 *
 * Public types: outliner_tree_t, outliner_entry_t (2-type rule).
 */
#ifndef FERRUM_EDITOR_PANELS_OUTLINER_TREE_H
#define FERRUM_EDITOR_PANELS_OUTLINER_TREE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* Forward declarations. */
struct edit_entity_store;

/** @brief Maximum length of the display name. */
#define OUTLINER_NAME_MAX 256
/** @brief Maximum filter string length. */
#define OUTLINER_FILTER_MAX 128
/** @brief Maximum number of visible entries. */
#define OUTLINER_MAX_ENTRIES 4096

/**
 * @brief A single outliner display entry.
 */
typedef struct outliner_entry {
    uint32_t entity_id;                       /**< Entity slot ID. */
    uint32_t entity_type;                     /**< Entity type ID. */
    char     display_name[OUTLINER_NAME_MAX]; /**< Display name. */
} outliner_entry_t;

/**
 * @brief Outliner tree model state.
 */
typedef struct outliner_tree {
    outliner_entry_t *entries;      /**< Visible entries after filtering. */
    uint32_t          entry_count;  /**< Number of visible entries. */
    uint32_t          entry_capacity; /**< Allocated capacity. */

    outliner_entry_t *all_entries;  /**< All entries (pre-filter). */
    uint32_t          all_count;    /**< Total entry count. */

    char filter[OUTLINER_FILTER_MAX]; /**< Current filter string. */
    int  scroll_offset;               /**< Scroll offset (rows). */
} outliner_tree_t;

/* ---- Lifecycle ---- */

/**
 * @brief Initialize the outliner tree.
 * @param tree  Tree to initialize (non-NULL).
 */
void outliner_tree_init(outliner_tree_t *tree);

/**
 * @brief Free outliner tree memory.
 * @param tree  Tree to destroy (non-NULL).
 */
void outliner_tree_destroy(outliner_tree_t *tree);

/**
 * @brief Rebuild the tree from an entity store.
 *
 * Scans all active entities and populates the entry list.
 * Reapplies the current filter.
 *
 * @param tree   Tree to rebuild (non-NULL).
 * @param store  Entity store to read from (non-NULL).
 */
void outliner_tree_rebuild(outliner_tree_t *tree,
                            const struct edit_entity_store *store);

/**
 * @brief Set the filter string. Refilters the entry list.
 *
 * Empty string clears the filter. Case-insensitive substring match.
 *
 * @param tree    Tree (non-NULL).
 * @param filter  Filter string (non-NULL, may be empty).
 */
void outliner_tree_set_filter(outliner_tree_t *tree, const char *filter);

/* ---- Query (outliner_tree_query.c) ---- */

/**
 * @brief Get the number of visible (filtered) entries.
 * @param tree  Tree (non-NULL).
 * @return Number of visible entries.
 */
uint32_t outliner_tree_count(const outliner_tree_t *tree);

/**
 * @brief Get a visible entry by index.
 * @param tree   Tree (non-NULL).
 * @param index  Entry index.
 * @return Pointer to entry, or NULL if out of bounds.
 */
const outliner_entry_t *outliner_tree_get(const outliner_tree_t *tree,
                                           uint32_t index);

/**
 * @brief Scroll the outliner by delta rows.
 *
 * Clamps to [0, max]. Negative delta scrolls up.
 *
 * @param tree   Tree (non-NULL).
 * @param delta  Row delta (positive = down, negative = up).
 */
void outliner_tree_scroll(outliner_tree_t *tree, int delta);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_PANELS_OUTLINER_TREE_H */
