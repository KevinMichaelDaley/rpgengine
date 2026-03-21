/**
 * @file scene_asset_load.c
 * @brief Scene editor asset loading from disk.
 *
 * Reads FVMA mesh files from the asset directory and loads them
 * into the viewport's mesh registry for rendering.
 *
 * Non-static functions (4 / 4 limit):
 *   scene_load_entity_mesh
 *   scene_load_entity_collision_mesh
 *   scene_load_entity_skeleton
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

bool scene_load_entity_collision_mesh(struct scene_editor *ed,
                                       uint32_t entity_id,
                                       const char *asset_path) {
    if (!ed || !asset_path || asset_path[0] == '\0') {
        return false;
    }

    /* Check if already loaded. */
    if (viewport_render_get_collision_mesh(&ed->viewport, entity_id)) {
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
        snprintf(msg, sizeof(msg), "Failed to read collision mesh: %.80s",
                 full_path);
        scene_ui_tui_log_error(&ed->ui, msg);
        return false;
    }

    /* Upload as collision mesh overlay. */
    bool ok = viewport_render_load_collision_mesh(&ed->viewport, entity_id,
                                                   fvma_data, fvma_size);
    free(fvma_data);

    if (ok) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Loaded collision mesh: %s (entity %u)",
                 asset_path, entity_id);
        scene_ui_tui_log(&ed->ui, msg);
    } else {
        char msg[256];
        snprintf(msg, sizeof(msg), "Failed to load collision mesh: %s",
                 asset_path);
        scene_ui_tui_log_error(&ed->ui, msg);
    }

    return ok;
}

bool scene_load_entity_skeleton(struct scene_editor *ed, uint32_t entity_id,
                                 const char *skel_path) {
    if (!ed || !skel_path || skel_path[0] == '\0') return false;

    /* Check if demotion is blocked (entity already skeletal). */
    if (viewport_render_demote_to_static_blocked(&ed->viewport, entity_id)) {
        /* Already skeletal — treat as no-op success. */
        return true;
    }

    /* Entity must have a mesh_path attr so we can re-read the FVMA. */
    const edit_entity_t *ent = edit_entity_store_get(&ed->entities, entity_id);
    if (!ent || !ent->active) return false;

    uint8_t attr_type = 0;
    uint8_t attr_size = 0;
    const void *mp = entity_attrs_get(&ent->attrs, SCRIPT_KEY_MESH_PATH,
                                       &attr_type, &attr_size);
    if (!mp || attr_type != SCRIPT_ATTR_STR) {
        char msg[128];
        snprintf(msg, sizeof(msg),
                 "Entity %u has no mesh_path — cannot bind skeleton",
                 entity_id);
        scene_ui_tui_log_error(&ed->ui, msg);
        return false;
    }

    const char *mesh_path = (const char *)mp;
    if (mesh_path[0] == '\0') return false;

    /* Re-read the FVMA binary from disk. */
    const char *root = ed->config.asset_dir;
    char full_path[ASSET_PATH_MAX];
    int n = snprintf(full_path, sizeof(full_path), "%s/%s", root, mesh_path);
    if (n < 0 || (size_t)n >= sizeof(full_path)) return false;

    size_t fvma_size = 0;
    uint8_t *fvma_data = read_file_(full_path, &fvma_size);
    if (!fvma_data) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Cannot re-read FVMA for skeleton: %.80s",
                 full_path);
        scene_ui_tui_log_error(&ed->ui, msg);
        return false;
    }

    /* Promote to skeletal mesh. */
    bool ok = viewport_render_promote_to_skeletal(&ed->viewport, entity_id,
                                                    fvma_data, fvma_size);
    free(fvma_data);

    if (ok) {
        /* Also load the .fskel file into the skeleton registry so bone
         * overlay rendering can access joint positions and hierarchy. */
        char skel_full[ASSET_PATH_MAX];
        int sn = snprintf(skel_full, sizeof(skel_full), "%s/%s",
                          root, skel_path);
        if (sn > 0 && (size_t)sn < sizeof(skel_full)) {
            edit_skeleton_registry_load(&ed->skeleton_registry, skel_full);
        }

        char msg[256];
        snprintf(msg, sizeof(msg),
                 "Bound skeleton %s to entity %u", skel_path, entity_id);
        scene_ui_tui_log(&ed->ui, msg);
    } else {
        char msg[256];
        snprintf(msg, sizeof(msg),
                 "Failed to bind skeleton: FVMA has no bone weights "
                 "(entity %u, mesh %s)", entity_id, mesh_path);
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
