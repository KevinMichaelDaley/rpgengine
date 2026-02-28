/**
 * @file edit_asset_query.c
 * @brief Asset registry query functions — find, list, search, complete.
 *
 * Non-static functions: 4 (find, list, search, complete).
 */

#include "ferrum/editor/edit_asset_registry.h"

#include <regex.h>
#include <string.h>

const edit_asset_entry_t *edit_asset_registry_find(
    const edit_asset_registry_t *reg, const char *path) {
    if (!reg || !reg->entries || !path) return NULL;
    for (uint32_t i = 0; i < reg->count; i++) {
        if (strcmp(reg->entries[i].path, path) == 0) {
            return &reg->entries[i];
        }
    }
    return NULL;
}

uint32_t edit_asset_registry_list(const edit_asset_registry_t *reg,
                                   const char *prefix,
                                   edit_asset_type_t type,
                                   const edit_asset_entry_t **out,
                                   uint32_t max) {
    if (!reg || !reg->entries || !out || max == 0) return 0;
    size_t plen = prefix ? strlen(prefix) : 0;
    uint32_t found = 0;

    for (uint32_t i = 0; i < reg->count && found < max; i++) {
        const edit_asset_entry_t *e = &reg->entries[i];

        /* Prefix filter. */
        if (plen > 0 && strncmp(e->path, prefix, plen) != 0) continue;

        /* Type filter. */
        if (type != EDIT_ASSET_ANY && e->type != type) continue;

        out[found++] = e;
    }
    return found;
}

uint32_t edit_asset_registry_search(const edit_asset_registry_t *reg,
                                     const char *pattern,
                                     edit_asset_type_t type,
                                     const edit_asset_entry_t **out,
                                     uint32_t max) {
    if (!reg || !reg->entries || !pattern || !out || max == 0) return 0;

    regex_t re;
    int rc = regcomp(&re, pattern, REG_EXTENDED | REG_NOSUB | REG_ICASE);
    if (rc != 0) return 0;

    uint32_t found = 0;
    for (uint32_t i = 0; i < reg->count && found < max; i++) {
        const edit_asset_entry_t *e = &reg->entries[i];

        /* Type filter. */
        if (type != EDIT_ASSET_ANY && e->type != type) continue;

        /* Regex match on path. */
        if (regexec(&re, e->path, 0, NULL, 0) == 0) {
            out[found++] = e;
        }
    }

    regfree(&re);
    return found;
}

uint32_t edit_asset_registry_complete(const edit_asset_registry_t *reg,
                                       const char *prefix,
                                       const edit_asset_entry_t **out,
                                       uint32_t max) {
    if (!reg || !reg->entries || !prefix || !out || max == 0) return 0;
    size_t plen = strlen(prefix);
    uint32_t found = 0;

    for (uint32_t i = 0; i < reg->count && found < max; i++) {
        if (strncmp(reg->entries[i].path, prefix, plen) == 0) {
            out[found++] = &reg->entries[i];
        }
    }
    return found;
}
