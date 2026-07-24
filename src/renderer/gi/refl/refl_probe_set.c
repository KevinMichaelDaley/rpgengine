/**
 * @file refl_probe_set.c
 * @brief Reflection-probe set container (see refl_probe.h).
 */
#include "ferrum/renderer/gi/refl_probe.h"

#include <string.h>

void refl_probe_set_init(refl_probe_set_t *set, refl_probe_t *storage,
                         uint32_t capacity)
{
    if (set == NULL)
        return;
    memset(set, 0, sizeof(*set));
    set->probes = storage;
    set->capacity = (storage != NULL) ? capacity : 0u;
}
