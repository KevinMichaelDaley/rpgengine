#include "ferrum/procgen/procgen_srd_level_load.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int srd_generate(const char *ascii, uint32_t seed, double time_budget,
                         fr_room_box_t **rooms_out, uint32_t *n_rooms_out,
                         fr_corridor_seg_t **corridors_out, uint32_t *n_corridors_out);

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    char *buf = (char *)malloc(sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t rd = fread(buf, 1, sz, f);
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
    free(lvl->rooms);
    free(lvl->corridors);
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

    /* ── SRD generation ── */
    fr_room_box_t *rooms = NULL;
    uint32_t n_rooms = 0;
    fr_corridor_seg_t *corridors = NULL;
    uint32_t n_corridors = 0;

    int rc = srd_generate(ascii, seed, time_budget,
                           &rooms, &n_rooms, &corridors, &n_corridors);
    if (rc != 0) return -1;

    printf("procgen-srd: %u rooms, %u corridors generated (%.1fs budget)\n",
           n_rooms, n_corridors, time_budget);

    /* ── SVO rasterization ── */
    npc_svo_grid_t svo;
    uint32_t solid = procgen_svo_build_from_srd(&svo, rooms, n_rooms,
                                                 corridors, n_corridors);
    printf("procgen-srd: SVO built with %u solid voxels\n", solid);

    /* ── Mesh generation ── */
    uint32_t tris = procgen_mesh_from_svo(&svo, &lvl->mesh);
    printf("procgen-srd: mesh generated with %u triangles (%u vertices)\n",
           tris, lvl->mesh.vertex_count);

    npc_svo_grid_destroy(&svo);

    /* Store room/corridor data for later use (collision, etc.) */
    lvl->rooms      = rooms;
    lvl->room_count = n_rooms;
    lvl->corridors  = corridors;
    lvl->corridor_count = n_corridors;
    lvl->ok = 1;

    return 0;
}
