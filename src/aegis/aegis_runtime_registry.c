/**
 * @file aegis_runtime_registry.c
 * @brief Aegis script registry: register, unregister, clear, and lazy spawn.
 *
 * Scripts are registered (compiled bytecode stored) but NOT immediately
 * started. When publish() sees an event matching a registered-but-unspawned
 * script's topic, it lazily spawns the script (loads VM + dispatches fiber).
 */

#include "ferrum/aegis/aegis_runtime.h"
#include "ferrum/job/system.h"
#include <stdlib.h>
#include <string.h>

/* ----------------------------------------------------------------------- */
/* aegis_script_runtime_register                                            */
/* ----------------------------------------------------------------------- */

uint32_t aegis_script_runtime_register(aegis_script_runtime_t *rt,
                                       const char *name,
                                       const aegis_bytecode_t *bc) {
    if (rt->registry_count >= AEGIS_REGISTRY_MAX) {
        return AEGIS_SCRIPT_ID_INVALID;
    }

    /* Check for duplicate name. */
    for (uint32_t i = 0; i < AEGIS_REGISTRY_MAX; i++) {
        if (rt->registry[i].registered &&
            strcmp(rt->registry[i].name, name) == 0) {
            return AEGIS_SCRIPT_ID_INVALID;
        }
    }

    /* Find free slot. */
    uint32_t slot = AEGIS_SCRIPT_ID_INVALID;
    for (uint32_t i = 0; i < AEGIS_REGISTRY_MAX; i++) {
        if (!rt->registry[i].registered) {
            slot = i;
            break;
        }
    }
    if (slot == AEGIS_SCRIPT_ID_INVALID) {
        return AEGIS_SCRIPT_ID_INVALID;
    }

    aegis_script_entry_t *entry = &rt->registry[slot];
    memset(entry, 0, sizeof(*entry));

    /* Copy name. */
    size_t nlen = strlen(name);
    if (nlen > 63) nlen = 63;
    memcpy(entry->name, name, nlen);
    entry->name[nlen] = '\0';

    /* Copy bytecode (own the instructions). */
    entry->bytecode = *bc;
    if (bc->instruction_count > 0 && bc->instructions) {
        size_t isz = bc->instruction_count * sizeof(aegis_instruction_t);
        entry->bytecode.instructions = (aegis_instruction_t *)malloc(isz);
        if (!entry->bytecode.instructions) {
            return AEGIS_SCRIPT_ID_INVALID;
        }
        memcpy(entry->bytecode.instructions, bc->instructions, isz);
    }

    entry->registered = true;
    entry->spawned = false;
    entry->instance_id = AEGIS_SCRIPT_ID_INVALID;
    rt->registry_count++;

    return slot;
}

/* ----------------------------------------------------------------------- */
/* aegis_script_runtime_unregister                                          */
/* ----------------------------------------------------------------------- */

bool aegis_script_runtime_unregister(aegis_script_runtime_t *rt,
                                     const char *name) {
    for (uint32_t i = 0; i < AEGIS_REGISTRY_MAX; i++) {
        if (!rt->registry[i].registered) continue;
        if (strcmp(rt->registry[i].name, name) != 0) continue;

        /* If spawned, unload the active instance. */
        if (rt->registry[i].spawned &&
            rt->registry[i].instance_id != AEGIS_SCRIPT_ID_INVALID) {
            aegis_script_runtime_unload(rt, rt->registry[i].instance_id);
        }

        /* Free owned bytecode. */
        free(rt->registry[i].bytecode.instructions);
        rt->registry[i].bytecode.instructions = NULL;
        rt->registry[i].registered = false;
        rt->registry_count--;
        return true;
    }
    return false;
}

/* ----------------------------------------------------------------------- */
/* aegis_script_runtime_clear_registry                                      */
/* ----------------------------------------------------------------------- */

void aegis_script_runtime_clear_registry(aegis_script_runtime_t *rt) {
    for (uint32_t i = 0; i < AEGIS_REGISTRY_MAX; i++) {
        if (rt->registry[i].registered) {
            if (rt->registry[i].spawned &&
                rt->registry[i].instance_id != AEGIS_SCRIPT_ID_INVALID) {
                aegis_script_runtime_unload(rt,
                                            rt->registry[i].instance_id);
            }
            free(rt->registry[i].bytecode.instructions);
            rt->registry[i].bytecode.instructions = NULL;
            rt->registry[i].registered = false;
        }
    }
    rt->registry_count = 0;
}
