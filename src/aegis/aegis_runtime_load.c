/**
 * @file aegis_runtime_load.c
 * @brief Aegis script runtime: load and unload script instances.
 */

#include "ferrum/aegis/aegis_runtime.h"
#include <stdlib.h>
#include <string.h>

/**
 * @brief Publish callback adapter for the VM's signal opcode.
 *
 * Forwards event publishing to the runtime's routing logic.
 */
static void aegis_runtime_publish_cb(void *ctx, const aegis_event_t *ev) {
    aegis_script_runtime_publish((aegis_script_runtime_t *)ctx, ev);
}

/* ----------------------------------------------------------------------- */
/* aegis_script_runtime_load                                                */
/* ----------------------------------------------------------------------- */

uint32_t aegis_script_runtime_load(aegis_script_runtime_t *rt,
                                   const char *name,
                                   const aegis_bytecode_t *bc) {
    if (rt->instance_count >= rt->instance_cap) {
        return AEGIS_SCRIPT_ID_INVALID;
    }

    /* Find a free slot. */
    uint32_t slot = AEGIS_SCRIPT_ID_INVALID;
    for (uint32_t i = 0; i < rt->instance_cap; i++) {
        if (!rt->instances[i].active) {
            slot = i;
            break;
        }
    }
    if (slot == AEGIS_SCRIPT_ID_INVALID) {
        return AEGIS_SCRIPT_ID_INVALID;
    }

    aegis_script_instance_t *inst = &rt->instances[slot];
    memset(inst, 0, sizeof(*inst));

    /* Copy debug name. */
    size_t nlen = strlen(name);
    if (nlen > 63) nlen = 63;
    memcpy(inst->name, name, nlen);
    inst->name[nlen] = '\0';

    /* Copy bytecode (we own the instruction copy). */
    inst->bytecode = *bc;
    if (bc->instruction_count > 0 && bc->instructions) {
        size_t isz = bc->instruction_count * sizeof(aegis_instruction_t);
        inst->bytecode.instructions = (aegis_instruction_t *)malloc(isz);
        if (!inst->bytecode.instructions) {
            return AEGIS_SCRIPT_ID_INVALID;
        }
        memcpy(inst->bytecode.instructions, bc->instructions, isz);
    }

    /* Allocate arena buffer. */
    uint32_t arena_sz = rt->config.vm_config.arena_size;
    inst->arena_buf = (uint8_t *)calloc(1, arena_sz);
    if (!inst->arena_buf) {
        free(inst->bytecode.instructions);
        return AEGIS_SCRIPT_ID_INVALID;
    }

    /* Initialize VM. */
    if (!aegis_vm_init(&inst->vm, &inst->bytecode, &rt->config.vm_config,
                       inst->arena_buf, arena_sz)) {
        free(inst->arena_buf);
        free(inst->bytecode.instructions);
        return AEGIS_SCRIPT_ID_INVALID;
    }

    /* Initialize per-script event queue. */
    aegis_event_queue_init(&inst->event_queue, rt->config.event_queue_cap);

    inst->script_id = slot;
    inst->active = true;
    inst->runtime = rt;
    inst->job_sys = NULL;
    rt->instance_count++;

    /* Wire VM fields for signal/subscribe/await opcodes. */
    inst->vm.topic_table = &rt->topics;
    inst->vm.event_queue = &inst->event_queue;
    inst->vm.script_id = slot;
    inst->vm.signal_rate_limit_us = rt->config.signal_rate_limit_us;
    inst->vm.publish_fn = aegis_runtime_publish_cb;
    inst->vm.publish_ctx = rt;

    /* Auto-subscribe to topic if bytecode declared one. */
    if (inst->bytecode.topic_hash != 0) {
        aegis_topic_subscribe(&rt->topics, inst->bytecode.topic_hash, slot);
    }

    return slot;
}

/* ----------------------------------------------------------------------- */
/* aegis_script_runtime_unload                                              */
/* ----------------------------------------------------------------------- */

void aegis_script_runtime_unload(aegis_script_runtime_t *rt,
                                 uint32_t script_id) {
    if (script_id >= rt->instance_cap) return;

    aegis_script_instance_t *inst = &rt->instances[script_id];
    if (!inst->active) return;

    /* Unsubscribe from topic. */
    if (inst->bytecode.topic_hash != 0) {
        aegis_topic_unsubscribe(&rt->topics, inst->bytecode.topic_hash,
                                script_id);
    }

    /* Free resources. */
    aegis_event_queue_destroy(&inst->event_queue);
    free(inst->arena_buf);
    free(inst->bytecode.instructions);

    inst->active = false;
    inst->arena_buf = NULL;
    inst->bytecode.instructions = NULL;
    rt->instance_count--;
}
