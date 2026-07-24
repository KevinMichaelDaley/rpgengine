/**
 * @file refl_chunk_load.c
 * @brief Streamed .rprobe chunk payload load/free (see refl_file.h).
 */
#include "ferrum/renderer/gi/refl_file.h"

#include <stdlib.h>
#include <string.h>

refl_chunk_payload_t *refl_chunk_load(const char *path)
{
    if (path == NULL)
        return NULL;
    refl_chunk_payload_t *p =
        (refl_chunk_payload_t *)calloc(1u, sizeof(*p));
    if (p == NULL)
        return NULL;
    refl_probe_set_init(&p->set, p->probes, REFL_CHUNK_MAX_PROBES);
    if (!refl_file_load(path, &p->set, p->mips, &p->depth)) {
        free(p);
        return NULL;
    }
    return p;
}

void refl_chunk_free(refl_chunk_payload_t *p)
{
    if (p == NULL)
        return;
    for (uint32_t m = 0; m < REFL_PROBE_MAX_MIPS; ++m)
        free(p->mips[m]);
    free(p->depth);
    free(p);
}
