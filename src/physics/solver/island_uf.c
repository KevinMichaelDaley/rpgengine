/**
 * @file island_uf.c
 * @brief Union-find primitives for island discovery.
 */

#include "ferrum/physics/island.h"

/* ── phys_uf_find ──────────────────────────────────────────────── */

uint32_t phys_uf_find(phys_island_list_t *list, uint32_t x) {
    if (!list || !list->parent || x >= list->uf_size) {
        return x;
    }
    /* Path halving: point each node to its grandparent. */
    while (list->parent[x] != x) {
        list->parent[x] = list->parent[list->parent[x]];
        x = list->parent[x];
    }
    return x;
}

/* ── phys_uf_union ─────────────────────────────────────────────── */

void phys_uf_union(phys_island_list_t *list, uint32_t x, uint32_t y) {
    if (!list || !list->parent || !list->rank) {
        return;
    }
    uint32_t rx = phys_uf_find(list, x);
    uint32_t ry = phys_uf_find(list, y);
    if (rx == ry) {
        return;
    }
    /* Union by rank: attach smaller tree under larger. */
    if (list->rank[rx] < list->rank[ry]) {
        uint32_t tmp = rx;
        rx = ry;
        ry = tmp;
    }
    list->parent[ry] = rx;
    if (list->rank[rx] == list->rank[ry]) {
        list->rank[rx]++;
    }
}
