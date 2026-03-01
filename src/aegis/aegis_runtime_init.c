/**
 * @file aegis_runtime_init.c
 * @brief Aegis script runtime initialization and destruction.
 */

#include "ferrum/aegis/aegis_runtime.h"
#include <stdlib.h>
#include <string.h>

/* ----------------------------------------------------------------------- */
/* aegis_script_runtime_init                                                */
/* ----------------------------------------------------------------------- */

bool aegis_script_runtime_init(aegis_script_runtime_t *rt,
                               const aegis_runtime_config_t *cfg) {
    memset(rt, 0, sizeof(*rt));
    rt->config = *cfg;
    rt->instance_cap = cfg->max_instances;

    rt->instances = (aegis_script_instance_t *)calloc(
        cfg->max_instances, sizeof(aegis_script_instance_t));
    if (!rt->instances) {
        return false;
    }

    aegis_topic_table_init(&rt->topics, cfg->max_subscriptions,
                           cfg->max_instances);
    return true;
}

/* ----------------------------------------------------------------------- */
/* aegis_script_runtime_destroy                                             */
/* ----------------------------------------------------------------------- */

void aegis_script_runtime_destroy(aegis_script_runtime_t *rt) {
    if (!rt) return;

    /* Clear the registry (also unloads spawned instances). */
    aegis_script_runtime_clear_registry(rt);

    /* Unload any remaining active instances not from the registry. */
    for (uint32_t i = 0; i < rt->instance_cap; i++) {
        if (rt->instances[i].active) {
            aegis_script_runtime_unload(rt, i);
        }
    }

    aegis_topic_table_destroy(&rt->topics);
    free(rt->instances);
    rt->instances = NULL;
    rt->instance_count = 0;
    rt->instance_cap = 0;
}
