/**
 * @file procgen_level_load.h
 * @brief Load a procgen level: tokenize → rasterize → SVO → mesh.
 */

#ifndef FERRUM_PROCGEN_LEVEL_LOAD_H
#define FERRUM_PROCGEN_LEVEL_LOAD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ferrum/procgen/procgen_layout.h"
#include "ferrum/procgen/procgen_svo_builder.h"

typedef struct {
    procgen_raster_config_t config;
    procgen_mesh_t          mesh;
    fr_dungeon_layout_t     layout;
    int                     ok;
} procgen_level_t;

void procgen_level_init(procgen_level_t *lvl);
void procgen_level_free(procgen_level_t *lvl);
int  procgen_level_load(procgen_level_t *lvl, const char *path);
int  procgen_level_load_string(procgen_level_t *lvl, const char *grammar, const char *token_str);

#ifdef __cplusplus
}
#endif

#endif
