/**
 * @file lm_material.c
 * @brief Material-id -> reflector table lookup (see lm_material.h).
 */
#include "ferrum/lightmap/lm_material.h"

lm_material_t lm_material_lookup(const lm_material_table_t *table, uint16_t id)
{
    if (id < table->count)
        return table->entries[id];
    return table->fallback;
}
