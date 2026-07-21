/**
 * @file gi_sdf_stream_config.c
 * @brief Pre-init pool configuration for the SDF chunk stream (see
 *        gi_sdf_stream_configure in gi_sdf_stream.h).
 */
#include <string.h>

#include "ferrum/renderer/gi/gi_sdf_stream.h"

void gi_sdf_stream_configure(gi_sdf_stream_t *s, int n_slots, int fp16)
{
    if (s == NULL) return;
    if (n_slots < 0) n_slots = 0;                       /* 0 = default (8). */
    if (n_slots > GI_SDF_MAX_RESIDENT) n_slots = GI_SDF_MAX_RESIDENT;
    s->n_slots = n_slots;
    s->fp16 = fp16 ? 1 : 0;
}
