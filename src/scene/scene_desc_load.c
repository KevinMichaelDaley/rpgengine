/**
 * @file scene_desc_load.c
 * @brief Load a scene descriptor from a file into a caller arena (rpg-51nf).
 */
#include <stdio.h>

#include "ferrum/memory/arena.h"
#include "ferrum/scene/scene_desc.h"

bool scene_desc_load(const char *path, struct arena *arena, scene_desc_t *out)
{
    if (path == NULL || arena == NULL || out == NULL) return false;
    FILE *f = fopen(path, "rb");
    if (f == NULL) return false;

    bool ok = false;
    if (fseek(f, 0, SEEK_END) == 0) {
        long sz = ftell(f);
        if (sz >= 0 && fseek(f, 0, SEEK_SET) == 0) {
            char *buf = arena_alloc((arena_t *)arena, 1u, (size_t)sz + 1u);
            if (buf != NULL) {
                size_t rd = fread(buf, 1, (size_t)sz, f);
                if (rd == (size_t)sz) {
                    buf[sz] = '\0';
                    ok = scene_desc_parse(buf, (size_t)sz, arena, out);
                }
            }
        }
    }
    fclose(f);
    return ok;
}
