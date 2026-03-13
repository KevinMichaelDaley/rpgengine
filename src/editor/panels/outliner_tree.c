/**
 * @file outliner_tree.c
 * @brief Outliner tree lifecycle, rebuild, and filtering.
 *
 * Non-static functions: 4 (init, destroy, rebuild, set_filter).
 */

#include "ferrum/editor/panels/outliner_tree.h"
#include "ferrum/editor/edit_entity.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief Case-insensitive substring search. */
static bool strcasestr_(const char *haystack, const char *needle) {
    if (!needle[0]) return true;
    size_t nlen = strlen(needle);
    size_t hlen = strlen(haystack);
    if (nlen > hlen) return false;

    for (size_t i = 0; i <= hlen - nlen; i++) {
        bool match = true;
        for (size_t j = 0; j < nlen; j++) {
            if (tolower((unsigned char)haystack[i + j]) !=
                tolower((unsigned char)needle[j])) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

/** @brief Apply current filter to all_entries, populating entries. */
static void apply_filter_(outliner_tree_t *tree) {
    tree->entry_count = 0;
    for (uint32_t i = 0; i < tree->all_count; i++) {
        if (tree->filter[0] == '\0' ||
            strcasestr_(tree->all_entries[i].display_name, tree->filter)) {
            if (tree->entry_count < tree->entry_capacity) {
                tree->entries[tree->entry_count++] = tree->all_entries[i];
            }
        }
    }
}

void outliner_tree_init(outliner_tree_t *tree) {
    memset(tree, 0, sizeof(*tree));
    tree->entry_capacity = OUTLINER_MAX_ENTRIES;
    tree->entries = (outliner_entry_t *)calloc(tree->entry_capacity,
                                                sizeof(outliner_entry_t));
    tree->all_entries = (outliner_entry_t *)calloc(tree->entry_capacity,
                                                    sizeof(outliner_entry_t));
}

void outliner_tree_destroy(outliner_tree_t *tree) {
    free(tree->entries);
    free(tree->all_entries);
    memset(tree, 0, sizeof(*tree));
}

void outliner_tree_rebuild(outliner_tree_t *tree,
                            const struct edit_entity_store *store) {
    tree->all_count = 0;

    uint32_t type_count = 0;
    const edit_entity_type_info_t *types = edit_entity_type_registry(&type_count);

    for (uint32_t i = 0; i < store->capacity && tree->all_count < tree->entry_capacity; i++) {
        const edit_entity_t *e = &store->entities[i];
        if (!e->active) continue;

        outliner_entry_t *entry = &tree->all_entries[tree->all_count];
        entry->entity_id = i;
        entry->entity_type = e->type;

        if (e->name[0] != '\0') {
            snprintf(entry->display_name, sizeof(entry->display_name),
                     "%s", e->name);
        } else {
            /* Generate name from type. */
            const char *tname = "entity";
            for (uint32_t t = 0; t < type_count; t++) {
                if (types[t].type_id == e->type) {
                    tname = types[t].name;
                    break;
                }
            }
            snprintf(entry->display_name, sizeof(entry->display_name),
                     "%s_%u", tname, i);
        }

        tree->all_count++;
    }

    apply_filter_(tree);
}

void outliner_tree_set_filter(outliner_tree_t *tree, const char *filter) {
    snprintf(tree->filter, sizeof(tree->filter), "%s", filter);
    apply_filter_(tree);
}
