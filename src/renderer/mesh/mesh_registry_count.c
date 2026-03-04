/**
 * @file mesh_registry_count.c
 * @brief Count and capacity accessors for mesh_registry_t.
 */

#include "ferrum/renderer/mesh/mesh_registry.h"

/* ── mesh_registry_count ──────────────────────────────────────────── */

uint32_t mesh_registry_count(const mesh_registry_t *reg)
{
    if (!reg) { return 0; }
    return reg->count;
}

/* ── mesh_registry_capacity ───────────────────────────────────────── */

uint32_t mesh_registry_capacity(const mesh_registry_t *reg)
{
    if (!reg) { return 0; }
    return reg->capacity;
}
