/**
 * @file npc_sense.h
 * @brief NPC sense awareness tracking: per-NPC awareness list and
 *        auto-update pipeline that feeds the knowledge graph.
 *
 * After each SENSE_QUERY completes, the engine calls
 * npc_sense_auto_update() to compare new observations against
 * the NPC's awareness list and insert new entities into the
 * knowledge graph.
 */
#ifndef FERRUM_NPC_SENSE_H
#define FERRUM_NPC_SENSE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

struct aegis_sense_result;
struct npc_knowledge_graph;

/* ── Constants ──────────────────────────────────────────────────── */

/** Factor by which lost-entity salience decays per missed sweep. */
#define NPC_SENSE_SALIENCE_DECAY_FACTOR 4.0f

/** Salience below which an entity is pruned from awareness. */
#define NPC_SENSE_SALIENCE_PRUNE_THRESHOLD 0.001f

/* ── Types ──────────────────────────────────────────────────────── */

/**
 * @brief A single entry in the per-NPC awareness list.
 */
typedef struct npc_sense_awareness_entry {
    uint32_t entity_id;     /**< ECS entity index. */
    float    last_salience; /**< Composite salience from last observation. */
    uint64_t last_seen_us;  /**< Sim time (us) of last observation. */
} npc_sense_awareness_entry_t;

/**
 * @brief Per-NPC awareness list tracking known entities.
 */
typedef struct npc_sense_awareness {
    npc_sense_awareness_entry_t *entries;
    uint32_t                     count;
    uint32_t                     cap;
} npc_sense_awareness_t;

/* ── Lifecycle ──────────────────────────────────────────────────── */

/**
 * @brief Initialise an awareness list.
 *
 * Caller owns @p aw. Must be destroyed with npc_sense_awareness_destroy().
 *
 * @return true on success, false on allocation failure.
 */
bool npc_sense_awareness_init(npc_sense_awareness_t *aw, uint32_t cap);

/**
 * @brief Deallocate all internal resources.
 *
 * Safe to call on a zeroed struct.
 */
void npc_sense_awareness_destroy(npc_sense_awareness_t *aw);

/* ── Query ──────────────────────────────────────────────────────── */

/**
 * @brief Look up an entity in the awareness list by entity_id.
 *
 * @return Pointer to the entry, or NULL if not found.
 */
const npc_sense_awareness_entry_t *
npc_sense_awareness_find(const npc_sense_awareness_t *aw, uint32_t entity_id);

/* ── Update ─────────────────────────────────────────────────────── */

/**
 * @brief Feed a completed SENSE_QUERY result into the awareness system.
 *
 * For each entity in @p result:
 *   - If new: inserts a node into the knowledge graph (using distance
 *     and salience as a simple 4-d embedding) and adds an awareness entry.
 *   - If known: refreshes last_salience and last_seen_us.
 *
 * Entities in @p aw but absent from @p result have their salience
 * multiplied by 1 / NPC_SENSE_SALIENCE_DECAY_FACTOR.  When salience
 * falls below NPC_SENSE_SALIENCE_PRUNE_THRESHOLD the entry is removed.
 *
 * @param aw          Per-NPC awareness list (updated in place).
 * @param kg          Knowledge graph for this NPC (may be NULL).
 * @param result      Completed SENSE_QUERY result buffer.
 * @param sim_time_us Current simulation time in microseconds.
 * @return Number of new entities inserted into the knowledge graph.
 */
uint32_t npc_sense_auto_update(npc_sense_awareness_t *aw,
                               struct npc_knowledge_graph *kg,
                               const struct aegis_sense_result *result,
                               uint64_t sim_time_us);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_NPC_SENSE_H */
