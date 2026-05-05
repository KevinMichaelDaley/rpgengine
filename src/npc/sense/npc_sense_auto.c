/**
 * @file npc_sense_auto.c
 * @brief Per-NPC auto-update pipeline: awareness tracking → KG insertion.
 *
 * Called after each SENSE_QUERY completes to maintain the NPC's
 * awareness list and knowledge graph.  Detects new entities, refreshes
 * known ones, and decays lost ones.
 *
 * Non-static functions (4 of 4 max):
 *   1. npc_sense_awareness_init
 *   2. npc_sense_awareness_destroy
 *   3. npc_sense_awareness_find
 *   4. npc_sense_auto_update
 */

#include "ferrum/npc/npc_sense.h"
#include "ferrum/npc/npc_knowledge_graph.h"
#include "ferrum/aegis/aegis_sense.h"

#include <stdlib.h>
#include <string.h>

/* ── Lifecycle ──────────────────────────────────────────────────── */

bool npc_sense_awareness_init(npc_sense_awareness_t *aw, uint32_t cap) {
    if (!aw || cap == 0) return false;
    memset(aw, 0, sizeof(*aw));
    aw->entries = (npc_sense_awareness_entry_t *)calloc(
        cap, sizeof(npc_sense_awareness_entry_t));
    if (!aw->entries) return false;
    aw->cap = cap;
    return true;
}

void npc_sense_awareness_destroy(npc_sense_awareness_t *aw) {
    if (!aw) return;
    free(aw->entries);
    memset(aw, 0, sizeof(*aw));
}

/* ── Query ──────────────────────────────────────────────────────── */

const npc_sense_awareness_entry_t *
npc_sense_awareness_find(const npc_sense_awareness_t *aw,
                         uint32_t entity_id) {
    if (!aw) return NULL;
    for (uint32_t i = 0; i < aw->count; i++) {
        if (aw->entries[i].entity_id == entity_id) {
            return &aw->entries[i];
        }
    }
    return NULL;
}

/* ── Internal helpers ───────────────────────────────────────────── */

/**
 * @brief Scan the awareness list for entity_id; return index or UINT32_MAX.
 */
static uint32_t find_index_(const npc_sense_awareness_t *aw,
                            uint32_t entity_id) {
    for (uint32_t i = 0; i < aw->count; i++) {
        if (aw->entries[i].entity_id == entity_id) return i;
    }
    return UINT32_MAX;
}

/**
 * @brief Grow the awareness entries array if needed.
 */
static bool ensure_cap_(npc_sense_awareness_t *aw) {
    if (aw->count < aw->cap) return true;
    uint32_t new_cap = aw->cap * 2;
    if (new_cap < aw->cap) return false;
    npc_sense_awareness_entry_t *new_entries =
        (npc_sense_awareness_entry_t *)realloc(
            aw->entries, new_cap * sizeof(npc_sense_awareness_entry_t));
    if (!new_entries) return false;
    aw->entries = new_entries;
    aw->cap = new_cap;
    return true;
}

/**
 * @brief Insert a new knowledge-graph node using a 4-d embedding
 *        derived from distance and salience.
 */
static void kg_insert_sensed_(struct npc_knowledge_graph *kg,
                              uint32_t entity_id,
                              float distance, float salience) {
    if (!kg || kg->node_count >= kg->node_cap) return;
    /* Simple 4-d embedding: [normalised_distance, salience, 0, 0]. */
    float emb[4];
    emb[0] = distance / 50.0f;
    emb[1] = salience;
    emb[2] = 0.0f;
    emb[3] = 0.0f;
    npc_kg_insert_node(kg, (uint64_t)entity_id, NPC_KG_NODE_ENTITY, emb);
}

/* ── Public API: auto-update ────────────────────────────────────── */

uint32_t npc_sense_auto_update(npc_sense_awareness_t *aw,
                               struct npc_knowledge_graph *kg,
                               const struct aegis_sense_result *result,
                               uint64_t sim_time_us) {
    if (!aw || !result) return 0;
    if (result->status != 0) return 0;

    uint32_t inserted = 0;

    /* Snapshot the pre-sweep count so we only decay old entries. */
    const uint32_t old_count = aw->count;

    /* Track which old entries (indices < old_count) are seen. */
    uint8_t *seen = NULL;
    if (old_count > 0) {
        seen = (uint8_t *)calloc(old_count, 1);
    }

    /* Walk the sense result entities. */
    const uint8_t *ent_ptr = (const uint8_t *)result + sizeof(*result);
    for (uint32_t i = 0; i < result->entity_count; i++) {
        const aegis_sense_entity_t *ent =
            (const aegis_sense_entity_t *)ent_ptr;
        uint32_t idx = find_index_(aw, ent->entity_id);

        if (idx == UINT32_MAX) {
            /* New entity — appended beyond old_count, never decayed. */
            if (ensure_cap_(aw)) {
                npc_sense_awareness_entry_t *e = &aw->entries[aw->count++];
                e->entity_id = ent->entity_id;
                e->last_salience = ent->salience;
                e->last_seen_us = sim_time_us;
                kg_insert_sensed_(kg, ent->entity_id,
                                  ent->distance, ent->salience);
                inserted++;
            }
        } else {
            /* Known entity: refresh and mark as seen. */
            npc_sense_awareness_entry_t *e = &aw->entries[idx];
            if (ent->salience > 0.0f) {
                e->last_salience = ent->salience;
            }
            e->last_seen_us = sim_time_us;
            if (seen && idx < old_count) seen[idx] = 1;
        }

        ent_ptr += aegis_sense_entity_size(ent->name);
    }

    /* Pass 1: decay old unseen entries; clear entity_id of pruned ones. */
    if (old_count > 0) {
        for (uint32_t i = 0; i < old_count; i++) {
            if (seen && seen[i]) continue;

            aw->entries[i].last_salience /= NPC_SENSE_SALIENCE_DECAY_FACTOR;

            if (aw->entries[i].last_salience <= NPC_SENSE_SALIENCE_PRUNE_THRESHOLD) {
                aw->entries[i].entity_id = UINT32_MAX; /* mark pruned */
            }
        }
    }

    /* Pass 2: compact by removing entries with entity_id == UINT32_MAX. */
    {
        uint32_t write = 0;
        for (uint32_t read = 0; read < aw->count; read++) {
            if (aw->entries[read].entity_id == UINT32_MAX) continue;
            if (write != read) {
                aw->entries[write] = aw->entries[read];
            }
            write++;
        }
        aw->count = write;
    }

    free(seen);
    return inserted;
}
