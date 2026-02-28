/**
 * @file edit_asset_scan.c
 * @brief Recursive directory scanner for asset registration.
 *
 * Walks a directory tree, detects asset type from file extension,
 * computes CRC32 hash of file contents, and registers each file.
 *
 * Non-static functions: 1 (edit_asset_registry_scan).
 */

#include "ferrum/editor/edit_asset_registry.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

/* ── CRC32 (ISO 3309 polynomial) ─────────────────────────────────── */

static uint32_t crc32_update_(uint32_t crc, const uint8_t *data,
                               size_t len) {
    crc = ~crc;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (-(crc & 1u)));
        }
    }
    return ~crc;
}

/** Compute CRC32 hash of a file's contents. */
static uint32_t hash_file_(const char *abs_path) {
    FILE *f = fopen(abs_path, "rb");
    if (!f) return 0;

    uint32_t crc = 0;
    uint8_t buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        crc = crc32_update_(crc, buf, n);
    }
    fclose(f);
    return crc;
}

/* ── Extension → type mapping ─────────────────────────────────────── */

static edit_asset_type_t type_from_ext_(const char *ext) {
    if (!ext) return EDIT_ASSET_UNKNOWN;

    /* Mesh formats. */
    if (strcmp(ext, ".glb") == 0 || strcmp(ext, ".gltf") == 0 ||
        strcmp(ext, ".obj") == 0 || strcmp(ext, ".fbx") == 0) {
        return EDIT_ASSET_MESH;
    }

    /* Texture formats. */
    if (strcmp(ext, ".png") == 0 || strcmp(ext, ".jpg") == 0 ||
        strcmp(ext, ".jpeg") == 0 || strcmp(ext, ".ktx2") == 0 ||
        strcmp(ext, ".bmp") == 0 || strcmp(ext, ".tga") == 0) {
        return EDIT_ASSET_TEXTURE;
    }

    /* Material files. */
    if (strcmp(ext, ".mat") == 0) {
        return EDIT_ASSET_MATERIAL;
    }

    /* Prefab files. */
    if (strcmp(ext, ".prefab") == 0) {
        return EDIT_ASSET_PREFAB;
    }

    /* Script files. */
    if (strcmp(ext, ".lua") == 0 || strcmp(ext, ".wren") == 0 ||
        strcmp(ext, ".ed") == 0) {
        return EDIT_ASSET_SCRIPT;
    }

    return EDIT_ASSET_UNKNOWN;
}

/* ── Recursive scan ───────────────────────────────────────────────── */

/**
 * @brief Recursively scan a directory and register assets.
 *
 * @param reg       Registry to populate.
 * @param root_dir  Absolute path to the asset root (for making relative paths).
 * @param dir_path  Current directory being scanned (absolute).
 * @return Number of assets registered from this directory and its children.
 */
static uint32_t scan_dir_(edit_asset_registry_t *reg,
                           const char *root_dir,
                           const char *dir_path) {
    DIR *d = opendir(dir_path);
    if (!d) return 0;

    uint32_t registered = 0;
    size_t root_len = strlen(root_dir);

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        /* Skip hidden files and . / .. */
        if (ent->d_name[0] == '.') continue;

        /* Build absolute path. */
        char abs_path[1024];
        int n = snprintf(abs_path, sizeof(abs_path), "%s/%s",
                         dir_path, ent->d_name);
        if (n < 0 || (size_t)n >= sizeof(abs_path)) continue;

        struct stat st;
        if (stat(abs_path, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            /* Recurse into subdirectory. */
            registered += scan_dir_(reg, root_dir, abs_path);
        } else if (S_ISREG(st.st_mode)) {
            /* Determine type from extension. */
            const char *ext = strrchr(ent->d_name, '.');
            edit_asset_type_t type = type_from_ext_(ext);
            if (type == EDIT_ASSET_UNKNOWN) continue;

            /* Build relative path (strip root_dir + '/').  */
            const char *rel = abs_path + root_len;
            if (*rel == '/') rel++;

            /* Compute hash and register. */
            uint32_t hash = hash_file_(abs_path);
            uint32_t size = (uint32_t)st.st_size;

            if (edit_asset_registry_add(reg, rel, type, size, hash)) {
                registered++;
            }
        }
    }

    closedir(d);
    return registered;
}

uint32_t edit_asset_registry_scan(edit_asset_registry_t *reg,
                                   const char *root_dir) {
    if (!reg || !root_dir) return 0;
    return scan_dir_(reg, root_dir, root_dir);
}
