/**
 * @file edit_script_update.c
 * @brief Update blob management: write_attr, update buffer init/swap.
 */

#include "ferrum/editor/edit_script_env.h"

#include <stdlib.h>
#include <string.h>

/* ----------------------------------------------------------------------- */
/* Update buffer lifecycle                                                   */
/* ----------------------------------------------------------------------- */

bool script_update_buffer_init(script_update_buffer_t *buf, uint32_t capacity)
{
    buf->blob[0] = (uint8_t *)calloc(1, capacity);
    buf->blob[1] = (uint8_t *)calloc(1, capacity);
    if (!buf->blob[0] || !buf->blob[1]) {
        free(buf->blob[0]);
        free(buf->blob[1]);
        buf->blob[0] = buf->blob[1] = NULL;
        return false;
    }
    buf->used[0]  = 0;
    buf->used[1]  = 0;
    buf->capacity = capacity;
    atomic_store(&buf->ready, 0);
    return true;
}

void script_update_buffer_destroy(script_update_buffer_t *buf)
{
    free(buf->blob[0]);
    free(buf->blob[1]);
    buf->blob[0] = buf->blob[1] = NULL;
}

void script_update_buffer_swap(script_update_buffer_t *buf)
{
    /* Swap pointers. */
    uint8_t *tmp   = buf->blob[0];
    buf->blob[0]   = buf->blob[1];
    buf->blob[1]   = tmp;

    /* Front gets back's used count; back is cleared. */
    buf->used[0]   = buf->used[1];
    buf->used[1]   = 0;

    atomic_store_explicit(&buf->ready, 1, memory_order_release);
}

/* ----------------------------------------------------------------------- */
/* script_env_write_attr                                                     */
/* ----------------------------------------------------------------------- */

void script_env_write_attr(script_env_t *env, uint32_t entity_id,
                           uint32_t generation, uint16_t key,
                           uint8_t type, const void *data, uint8_t size)
{
    /* Total bytes needed for this attr write. */
    uint32_t attr_bytes = (uint32_t)sizeof(script_attr_write_t) + size;

    /* Check if the last update in the blob is for the same entity.
     * If so, we can append to it instead of creating a new header. */
    if (env->update_blob_used > 0) {
        /* Walk backwards to find the last update header.
         * We track it by scanning from the start — the last header
         * is the one whose offset + total_size == used. */
        uint32_t off = 0;
        uint32_t last_off = 0;
        while (off < env->update_blob_used) {
            last_off = off;
            const script_entity_update_t *u =
                (const script_entity_update_t *)(env->update_blob + off);
            off += u->total_size;
        }

        script_entity_update_t *last =
            (script_entity_update_t *)(env->update_blob + last_off);
        if (last->entity_id == entity_id &&
            last->generation == generation) {
            /* Append to existing update. */
            uint32_t new_total = (uint32_t)last->total_size + attr_bytes;
            if (env->update_blob_used + attr_bytes > env->update_blob_capacity) {
                return; /* blob full */
            }

            /* Write attr header + payload at current end. */
            uint8_t *dest = env->update_blob + env->update_blob_used;
            script_attr_write_t *aw = (script_attr_write_t *)dest;
            aw->key  = key;
            aw->type = type;
            aw->size = size;
            if (size > 0) {
                memcpy(dest + sizeof(script_attr_write_t), data, size);
            }

            last->attr_count++;
            last->total_size = (uint16_t)new_total;
            env->update_blob_used += attr_bytes;
            return;
        }
    }

    /* Create a new update header. */
    uint32_t header_bytes = (uint32_t)sizeof(script_entity_update_t);
    uint32_t total_needed = header_bytes + attr_bytes;

    if (env->update_blob_used + total_needed > env->update_blob_capacity) {
        return; /* blob full */
    }

    uint8_t *dest = env->update_blob + env->update_blob_used;

    /* Write update header. */
    script_entity_update_t *upd = (script_entity_update_t *)dest;
    upd->entity_id  = entity_id;
    upd->generation = generation;
    upd->attr_count = 1;
    upd->total_size = (uint16_t)total_needed;

    /* Write attr header + payload. */
    script_attr_write_t *aw =
        (script_attr_write_t *)(dest + header_bytes);
    aw->key  = key;
    aw->type = type;
    aw->size = size;
    if (size > 0) {
        memcpy(dest + header_bytes + sizeof(script_attr_write_t), data, size);
    }

    env->update_blob_used += total_needed;
}
