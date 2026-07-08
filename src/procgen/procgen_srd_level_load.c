#include "ferrum/procgen/procgen_srd_level_load.h"
#include "ferrum/procgen/srd/srd_bridge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[rd] = '\0';
    return buf;
}

void procgen_srd_level_init(procgen_srd_level_t *lvl) {
    if (!lvl) return;
    memset(lvl, 0, sizeof(*lvl));
    procgen_mesh_init(&lvl->mesh);
}

void procgen_srd_level_free(procgen_srd_level_t *lvl) {
    if (!lvl) return;
    if (lvl->mesh.vertices) {
        procgen_mesh_destroy(&lvl->mesh);
    }
    srd_free_geometry(lvl->rooms, lvl->corridors);
    memset(lvl, 0, sizeof(*lvl));
}

int procgen_srd_level_load(procgen_srd_level_t *lvl,
                            const char *path,
                            uint32_t seed,
                            double time_budget) {
    if (!lvl || !path) return -1;

    char *ascii = read_file(path);
    if (!ascii) return -1;

    int rc = procgen_srd_level_load_string(lvl, ascii, seed, time_budget);
    free(ascii);
    return rc;
}

int procgen_srd_level_load_string(procgen_srd_level_t *lvl,
                                   const char *ascii,
                                   uint32_t seed,
                                   double time_budget) {
    if (!lvl || !ascii) return -1;

    if (time_budget <= 0.0) time_budget = 3.0;

    /* ── SRD generation → SVO directly ── */
    npc_svo_grid_t svo;
    memset(&svo, 0, sizeof(svo));

    int rc = srd_generate_svo(ascii, seed, time_budget, &svo);
    if (rc != 0) return -1;

    printf("procgen-srd: SVO generated (depth=%u, %.1fs budget)\n",
           svo.max_depth, time_budget);

    /* ── Build mesh from SVO ── */
    uint32_t tris = 0;
    if (svo.max_depth > 0) {
        tris = procgen_mesh_from_svo(&svo, &lvl->mesh);
        printf("procgen-srd: mesh generated with %u triangles (%u vertices)\n",
               tris, lvl->mesh.vertex_count);
    }
    npc_svo_grid_destroy(&svo);

    /* No rooms/corridors in the new pipeline */
    lvl->rooms = NULL;
    lvl->room_count = 0;
    lvl->corridors = NULL;
    lvl->corridor_count = 0;
    lvl->ok = 1;

    return 0;
}
