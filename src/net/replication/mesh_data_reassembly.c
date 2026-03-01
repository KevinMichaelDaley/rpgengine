/**
 * @file mesh_data_reassembly.c
 * @brief MESH_DATA chunk reassembly — client-side helper.
 */
#include "ferrum/net/replication/mesh_data.h"

#include <stdlib.h>
#include <string.h>

/* ── Init / destroy ────────────────────────────────────────────── */

bool net_repl_mesh_reassembly_init(net_repl_mesh_reassembly_table_t *table,
                                   uint32_t capacity) {
    if (!table || capacity == 0) return false;
    table->slots = calloc(capacity, sizeof(net_repl_mesh_reassembly_t));
    if (!table->slots) return false;
    table->capacity = capacity;
    return true;
}

void net_repl_mesh_reassembly_destroy(net_repl_mesh_reassembly_table_t *table) {
    if (!table || !table->slots) return;
    for (uint32_t i = 0; i < table->capacity; i++) {
        free(table->slots[i].buffer);
    }
    free(table->slots);
    table->slots = NULL;
    table->capacity = 0;
}

/* ── Find or allocate a slot for a body_id ─────────────────────── */

static net_repl_mesh_reassembly_t *find_slot_(
        net_repl_mesh_reassembly_table_t *table,
        uint16_t body_id, uint16_t total_chunks, uint32_t total_size) {
    /* Look for existing slot with this body_id. */
    for (uint32_t i = 0; i < table->capacity; i++) {
        if (table->slots[i].buffer && table->slots[i].body_id == body_id) {
            return &table->slots[i];
        }
    }
    /* Allocate a free slot. */
    for (uint32_t i = 0; i < table->capacity; i++) {
        if (!table->slots[i].buffer) {
            net_repl_mesh_reassembly_t *s = &table->slots[i];
            s->body_id = body_id;
            s->total_chunks = total_chunks;
            s->total_size = total_size;
            s->received_mask = 0;
            s->buffer = calloc(total_size, 1);
            if (!s->buffer) return NULL;
            return s;
        }
    }
    return NULL; /* table full */
}

/* ── Push a chunk ──────────────────────────────────────────────── */

bool net_repl_mesh_reassembly_push(net_repl_mesh_reassembly_table_t *table,
                                   const net_repl_mesh_chunk_t *chunk,
                                   uint8_t **out_data,
                                   uint32_t *out_size) {
    if (!table || !chunk || !out_data || !out_size) return false;
    if (chunk->chunk_index >= chunk->total_chunks) return false;
    if (chunk->total_chunks > 32) return false; /* bitmask limit */

    net_repl_mesh_reassembly_t *slot = find_slot_(
        table, chunk->body_id, chunk->total_chunks, chunk->total_size);
    if (!slot) return false;

    /* Compute offset for this chunk. */
    uint32_t chunk_max = NET_REPL_MESH_CHUNK_MAX;
    uint32_t offset = (uint32_t)chunk->chunk_index * chunk_max;
    if (offset + chunk->payload_size > slot->total_size) return false;

    /* Copy payload and mark received. */
    memcpy(slot->buffer + offset, chunk->payload, chunk->payload_size);
    slot->received_mask |= (1u << chunk->chunk_index);

    /* Check if complete (all bits set). */
    uint32_t expected_mask = (slot->total_chunks == 32)
                             ? 0xFFFFFFFFu
                             : (1u << slot->total_chunks) - 1u;
    if (slot->received_mask == expected_mask) {
        /* Transfer ownership to caller. */
        *out_data = slot->buffer;
        *out_size = slot->total_size;
        slot->buffer = NULL;
        slot->received_mask = 0;
        return true;
    }

    return false;
}
