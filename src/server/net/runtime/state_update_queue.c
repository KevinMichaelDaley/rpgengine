#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include "ferrum/server/net/state_update_queue.h"

typedef struct fr_state_update_node {
    uint16_t client_id;
    uint16_t schema_id;
    uint16_t payload_size;
    uint8_t payload[];
} fr_state_update_node_t;

struct fr_state_update_queue {
    fr_state_update_node_t **ring;
    uint32_t capacity;
    uint32_t max_payload_size;
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    mtx_t lock;
};

static uint32_t u32_or_default_(uint32_t v, uint32_t def) {
    return (v == 0u) ? def : v;
}

fr_state_update_queue_t *fr_state_update_queue_create(const fr_state_update_queue_config_t *cfg) {
    const uint32_t cap = u32_or_default_(cfg ? cfg->capacity : 0u, 1024u);
    const uint32_t max_payload = u32_or_default_(cfg ? cfg->max_payload_size : 0u, 1024u);
    if (cap == 0u || max_payload == 0u) {
        return NULL;
    }

    fr_state_update_queue_t *q = (fr_state_update_queue_t *)calloc(1u, sizeof(*q));
    if (!q) {
        return NULL;
    }

    q->ring = (fr_state_update_node_t **)calloc((size_t)cap, sizeof(*q->ring));
    if (!q->ring) {
        free(q);
        return NULL;
    }

    q->capacity = cap;
    q->max_payload_size = max_payload;
    q->head = 0u;
    q->tail = 0u;
    q->count = 0u;

    if (mtx_init(&q->lock, mtx_plain) != thrd_success) {
        free(q->ring);
        free(q);
        return NULL;
    }

    return q;
}

void fr_state_update_queue_destroy(fr_state_update_queue_t *q) {
    if (!q) {
        return;
    }

    (void)mtx_lock(&q->lock);
    if (q->ring) {
        for (uint32_t i = 0u; i < q->capacity; ++i) {
            free(q->ring[i]);
            q->ring[i] = NULL;
        }
        free(q->ring);
        q->ring = NULL;
    }
    q->capacity = 0u;
    q->max_payload_size = 0u;
    q->head = 0u;
    q->tail = 0u;
    q->count = 0u;
    (void)mtx_unlock(&q->lock);

    mtx_destroy(&q->lock);
    free(q);
}

bool fr_state_update_queue_push(fr_state_update_queue_t *q,
                               uint16_t client_id,
                               uint16_t schema_id,
                               const uint8_t *payload,
                               uint16_t payload_size) {
    if (!q || !payload || payload_size == 0u) {
        return false;
    }
    if ((uint32_t)payload_size > q->max_payload_size) {
        return false;
    }

    fr_state_update_node_t *node = (fr_state_update_node_t *)malloc(sizeof(*node) + (size_t)payload_size);
    if (!node) {
        return false;
    }

    node->client_id = client_id;
    node->schema_id = schema_id;
    node->payload_size = payload_size;
    memcpy(node->payload, payload, payload_size);

    (void)mtx_lock(&q->lock);
    if (q->count >= q->capacity) {
        (void)mtx_unlock(&q->lock);
        free(node);
        return false;
    }

    if (q->ring[q->tail] != NULL) {
        (void)mtx_unlock(&q->lock);
        free(node);
        return false;
    }

    q->ring[q->tail] = node;
    q->tail = (q->tail + 1u) % q->capacity;
    q->count++;
    (void)mtx_unlock(&q->lock);

    return true;
}

bool fr_state_update_queue_pop(fr_state_update_queue_t *q,
                              uint16_t *out_client_id,
                              uint16_t *out_schema_id,
                              uint8_t *out_payload,
                              uint16_t *inout_payload_size) {
    if (!q || !out_client_id || !out_schema_id || !out_payload || !inout_payload_size) {
        return false;
    }

    (void)mtx_lock(&q->lock);
    if (q->count == 0u) {
        (void)mtx_unlock(&q->lock);
        return false;
    }

    fr_state_update_node_t *node = q->ring[q->head];
    if (!node) {
        (void)mtx_unlock(&q->lock);
        return false;
    }

    if (node->payload_size > *inout_payload_size) {
        (void)mtx_unlock(&q->lock);
        return false;
    }

    q->ring[q->head] = NULL;
    q->head = (q->head + 1u) % q->capacity;
    q->count--;
    (void)mtx_unlock(&q->lock);

    *out_client_id = node->client_id;
    *out_schema_id = node->schema_id;
    memcpy(out_payload, node->payload, node->payload_size);
    *inout_payload_size = node->payload_size;

    free(node);
    return true;
}
