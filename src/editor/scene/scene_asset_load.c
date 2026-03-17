/**
 * @file scene_asset_load.c
 * @brief Scene editor asset loading from disk.
 *
 * Reads FVMA mesh files from the asset directory and loads them
 * into the viewport's mesh registry for rendering.
 *
 * Non-static functions (2 / 4 limit):
 *   scene_load_entity_mesh
 *   scene_load_pending_meshes
 */

#include "ferrum/editor/scene/scene_asset_load.h"
#include "ferrum/editor/scene/scene_main.h"
#include "ferrum/editor/scene/scene_viewport_render.h"
#include "ferrum/editor/scene/scene_ui.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/entity/entity_attrs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Maximum length for a full asset file path. */
#define ASSET_PATH_MAX 512

/* ---- Helpers ---- */

/**
 * @brief Read an entire file into a heap-allocated buffer.
 *
 * @param path      Absolute or relative file path.
 * @param out_size  Receives file size in bytes.
 * @return Heap-allocated buffer (caller must free), or NULL on error.
 */
static uint8_t *read_file_(const char *path, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz <= 0) { fclose(f); return NULL; }
    fseek(f, 0, SEEK_SET);

    uint8_t *buf = (uint8_t *)malloc((size_t)sz);
    if (!buf) { fclose(f); return NULL; }

    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);

    if (rd != (size_t)sz) { free(buf); return NULL; }
    *out_size = (size_t)sz;
    return buf;
}

/* ---- Public API ---- */

bool scene_load_entity_mesh(struct scene_editor *ed, uint32_t entity_id,
                            const char *asset_path) {
    if (!ed || !asset_path || asset_path[0] == '\0') {
        return false;
    }

    /* Check if already loaded. */
    if (viewport_render_get_entity_mesh(&ed->viewport, entity_id)) {
        return true; /* Already cached. */
    }

    /* Build full path: asset_root/asset_path. */
    const char *root = ed->config.asset_dir;
    char full_path[ASSET_PATH_MAX];
    int n = snprintf(full_path, sizeof(full_path), "%s/%s",
                     root, asset_path);
    if (n < 0 || (size_t)n >= sizeof(full_path)) return false;

    /* Read FVMA binary from disk. */
    size_t fvma_size = 0;
    uint8_t *fvma_data = read_file_(full_path, &fvma_size);
    if (!fvma_data) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Failed to read mesh: %.100s", full_path);
        scene_ui_tui_log_error(&ed->ui, msg);
        return false;
    }

    /* Upload to GPU via viewport mesh registry. */
    bool ok = viewport_render_load_entity_mesh(&ed->viewport, entity_id,
                                                fvma_data, fvma_size);
    free(fvma_data);

    if (ok) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Loaded mesh: %s (entity %u)",
                 asset_path, entity_id);
        scene_ui_tui_log(&ed->ui, msg);
    } else {
        char msg[256];
        snprintf(msg, sizeof(msg), "Failed to load mesh format: %s",
                 asset_path);
        scene_ui_tui_log_error(&ed->ui, msg);
    }

    return ok;
}

uint32_t scene_load_pending_meshes(struct scene_editor *ed) {
    if (!ed) return 0;

    uint32_t loaded = 0;
    uint32_t cap = ed->entities.capacity;

    for (uint32_t i = 0; i < cap; i++) {
        const edit_entity_t *ent = edit_entity_store_get(&ed->entities, i);
        if (!ent || !ent->active) continue;
        if (ent->type != EDIT_ENTITY_TYPE_MESH) continue;

        /* Already loaded? */
        if (viewport_render_get_entity_mesh(&ed->viewport, i)) continue;

        /* Check for mesh_path attribute. */
        uint8_t attr_type = 0;
        uint8_t attr_size = 0;
        const void *val = entity_attrs_get(&ent->attrs,
                                            SCRIPT_KEY_MESH_PATH,
                                            &attr_type, &attr_size);
        if (!val || attr_type != SCRIPT_ATTR_STR) continue;

        const char *mesh_path = (const char *)val;
        if (mesh_path[0] == '\0') continue;

        if (scene_load_entity_mesh(ed, i, mesh_path)) {
            loaded++;
        }
    }

    return loaded;
}
