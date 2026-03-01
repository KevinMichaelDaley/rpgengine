/**
 * @file aegis_ops_update_push.c
 * @brief Update finalization handler: push_update.
 *
 * Per ref/aegis_bytecode_spec.md §3.3.
 * 1 non-static function.
 */

#include "ferrum/aegis/aegis_ops_update.h"
#include <string.h>

/* ----------------------------------------------------------------------- */
/* push_update: finalize and append staging to update set                   */
/* ----------------------------------------------------------------------- */

bool aegis_op_push_update(aegis_update_set_t *set,
                          aegis_state_update_t *staging) {
    if (set->count >= set->capacity) {
        return false;
    }

    /* Copy staging into the next slot. */
    set->updates[set->count] = *staging;
    set->count++;

    /* Clear staging for the next update. */
    memset(staging, 0, sizeof(*staging));
    return true;
}
