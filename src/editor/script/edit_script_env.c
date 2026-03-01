/**
 * @file edit_script_env.c
 * @brief Script environment init/reset and snapshot builder.
 */

#include "ferrum/editor/edit_script_env.h"

#include <string.h>

/* ----------------------------------------------------------------------- */
/* env init / reset                                                          */
/* ----------------------------------------------------------------------- */

void script_env_init_blob(script_env_t *env, uint8_t *blob, uint32_t capacity)
{
    memset(env, 0, sizeof(*env));
    env->update_blob          = blob;
    env->update_blob_used     = 0;
    env->update_blob_capacity = capacity;
}

void script_env_reset(script_env_t *env)
{
    env->update_blob_used = 0;
}

/* ----------------------------------------------------------------------- */
/* snapshot builder                                                          */
/* ----------------------------------------------------------------------- */

uint32_t script_snapshot_build(const edit_entity_store_t *store,
                               script_entity_snapshot_t *out,
                               uint32_t capacity)
{
    uint32_t written = 0;

    for (uint32_t i = 0; i < store->capacity && written < capacity; i++) {
        const edit_entity_t *e = &store->entities[i];
        if (!e->active) continue;

        script_entity_snapshot_t *s = &out[written];
        memset(s, 0, sizeof(*s));

        s->entity_id  = i;
        s->generation = 0; /* edit entities have no generation */
        s->active     = 1;
        s->type       = (uint8_t)e->type;
        s->body_index = e->body_index;

        memcpy(s->pos,   e->pos,   sizeof(e->pos));
        memcpy(s->rot,   e->rot,   sizeof(e->rot));
        memcpy(s->scale, e->scale, sizeof(e->scale));
        memcpy(s->name,  e->name,  sizeof(e->name));
        memcpy(s->materials, e->materials, sizeof(e->materials));

        /* Copy dynamic attributes. */
        memcpy(&s->attrs, &e->attrs, sizeof(entity_attrs_t));

        written++;
    }

    return written;
}

/* ----------------------------------------------------------------------- */
/* script_entity_get_attr                                                    */
/* ----------------------------------------------------------------------- */

const void *script_entity_get_attr(const script_entity_snapshot_t *entity,
                                   uint16_t key, uint8_t *out_type,
                                   uint8_t *out_size)
{
    return entity_attrs_get(&entity->attrs, key, out_type, out_size);
}
