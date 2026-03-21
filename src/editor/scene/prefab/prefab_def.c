/**
 * @file prefab_def.c
 * @brief Prefab definition init/clear.
 *
 * Non-static functions: prefab_def_init, prefab_def_clear (2/4).
 */

#include "ferrum/editor/scene/prefab/prefab_def.h"

#include <string.h>

void prefab_def_init(prefab_def_t *def) {
    if (!def) return;
    memset(def, 0, sizeof(*def));
    def->version = PREFAB_VERSION;
}

void prefab_def_clear(prefab_def_t *def) {
    prefab_def_init(def);
}
