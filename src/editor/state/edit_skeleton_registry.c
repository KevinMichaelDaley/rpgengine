/**
 * @file edit_skeleton_registry.c
 * @brief Editor skeleton registry — stores loaded skeleton definitions.
 *
 * The registry takes ownership of skeleton_def_t and IBM arrays passed
 * to edit_skeleton_registry_add(). Callers must NOT destroy them after add.
 *
 * Non-static functions (4 / 4 limit):
 *   edit_skeleton_registry_init
 *   edit_skeleton_registry_destroy
 *   edit_skeleton_registry_add
 *   edit_skeleton_registry_get
 */

#include "ferrum/editor/edit_skeleton_registry.h"
#include "ferrum/memory/vm_alloc.h"

#include <stdlib.h>
#include <string.h>

bool edit_skeleton_registry_init(edit_skeleton_registry_t *reg,
                                  uint32_t capacity) {
    if (!reg || capacity == 0) return false;

    /* Use demand-paged virtual memory: physical pages are only
     * committed when first written, so large capacities are cheap. */
    size_t alloc_size = (size_t)capacity * sizeof(edit_skeleton_entry_t);
    reg->entries = (edit_skeleton_entry_t *)vm_reserve(alloc_size);
    if (!reg->entries) return false;

    reg->capacity = capacity;
    reg->count = 0;
    return true;
}

void edit_skeleton_registry_destroy(edit_skeleton_registry_t *reg) {
    if (!reg) return;

    if (reg->entries) {
        for (uint32_t i = 0; i < reg->capacity; i++) {
            if (!reg->entries[i].active) continue;
            skeleton_def_destroy(&reg->entries[i].skel);
            free(reg->entries[i].ibms);
        }
        vm_release(reg->entries,
                   (size_t)reg->capacity * sizeof(edit_skeleton_entry_t));
    }

    reg->entries = NULL;
    reg->capacity = 0;
    reg->count = 0;
}

uint32_t edit_skeleton_registry_add(edit_skeleton_registry_t *reg,
                                     const char *path,
                                     skeleton_def_t *skel,
                                     mat4_t *ibms,
                                     uint32_t ibm_count) {
    if (!reg || !path || path[0] == '\0' || !skel) return UINT32_MAX;

    /* Check for existing entry with same path (replace). */
    for (uint32_t i = 0; i < reg->capacity; i++) {
        if (reg->entries[i].active &&
            strcmp(reg->entries[i].path, path) == 0) {
            /* Replace: destroy old, take ownership of new. */
            skeleton_def_destroy(&reg->entries[i].skel);
            free(reg->entries[i].ibms);

            reg->entries[i].skel = *skel;
            reg->entries[i].ibms = ibms;
            reg->entries[i].ibm_count = ibm_count;

            /* Zero the caller's copy so they can't double-free. */
            memset(skel, 0, sizeof(*skel));
            return i;
        }
    }

    /* Find free slot. */
    if (reg->count >= reg->capacity) return UINT32_MAX;

    uint32_t slot = UINT32_MAX;
    for (uint32_t i = 0; i < reg->capacity; i++) {
        if (!reg->entries[i].active) {
            slot = i;
            break;
        }
    }
    if (slot == UINT32_MAX) return UINT32_MAX;

    /* Take ownership of the skeleton and IBMs. */
    edit_skeleton_entry_t *entry = &reg->entries[slot];
    memset(entry, 0, sizeof(*entry));

    uint32_t plen = (uint32_t)strlen(path);
    if (plen >= EDIT_SKELETON_PATH_MAX) plen = EDIT_SKELETON_PATH_MAX - 1;
    memcpy(entry->path, path, plen);
    entry->path[plen] = '\0';

    entry->skel = *skel;
    entry->ibms = ibms;
    entry->ibm_count = ibm_count;
    entry->active = true;
    reg->count++;

    /* Zero the caller's copy so they can't double-free. */
    memset(skel, 0, sizeof(*skel));
    return slot;
}

const edit_skeleton_entry_t *edit_skeleton_registry_get(
    const edit_skeleton_registry_t *reg, const char *path) {
    if (!reg || !path) return NULL;

    for (uint32_t i = 0; i < reg->capacity; i++) {
        if (reg->entries[i].active &&
            strcmp(reg->entries[i].path, path) == 0) {
            return &reg->entries[i];
        }
    }
    return NULL;
}
