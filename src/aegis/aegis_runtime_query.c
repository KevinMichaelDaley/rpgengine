/**
 * @file aegis_runtime_query.c
 * @brief Aegis script runtime queries: find registered scripts, set job system.
 */

#include "ferrum/aegis/aegis_runtime.h"
#include <string.h>

/* ----------------------------------------------------------------------- */
/* aegis_script_runtime_set_job_sys                                         */
/* ----------------------------------------------------------------------- */

void aegis_script_runtime_set_job_sys(aegis_script_runtime_t *rt,
                                      struct job_system *sys) {
    rt->job_sys = sys;
}

/* ----------------------------------------------------------------------- */
/* aegis_script_runtime_find                                                */
/* ----------------------------------------------------------------------- */

const aegis_script_entry_t *aegis_script_runtime_find(
    const aegis_script_runtime_t *rt, const char *name) {
    for (uint32_t i = 0; i < AEGIS_REGISTRY_MAX; i++) {
        if (rt->registry[i].registered &&
            strcmp(rt->registry[i].name, name) == 0) {
            return &rt->registry[i];
        }
    }
    return NULL;
}
