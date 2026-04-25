/**
 * @file entity_def.h
 * @brief Entity definition file format (.fentity).
 *
 * JSON-based entity templates that specify mesh, material, physics,
 * custom attributes, and scripts for spawning instances.
 */
#ifndef FERRUM_EDITOR_DEF_ENTITY_DEF_H
#define FERRUM_EDITOR_DEF_ENTITY_DEF_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "ferrum/entity/entity_attrs.h"

/* ------------------------------------------------------------------------ */
/* Constants                                                                 */
/* ------------------------------------------------------------------------ */

/** Maximum name length for entity definition. */
#define ENTITY_DEF_NAME_MAX 64

/** Maximum path length for mesh/material/script files. */
#define ENTITY_DEF_PATH_MAX 256

/** Maximum number of scripts per definition. */
#define ENTITY_DEF_SCRIPTS_MAX 8

/* ------------------------------------------------------------------------ */
/* Types                                                                     */
/* ------------------------------------------------------------------------ */

/**
 * @brief Entity definition loaded from .fentity file.
 */
typedef struct entity_def {
    char name[ENTITY_DEF_NAME_MAX];       /**< Display name. */
    char mesh_path[ENTITY_DEF_PATH_MAX];   /**< Mesh path (.fvma). */
    char material_path[ENTITY_DEF_PATH_MAX]; /**< Material path (.fmat). */
    char scripts[ENTITY_DEF_SCRIPTS_MAX][ENTITY_DEF_PATH_MAX]; /**< Script paths. */
    uint32_t script_count;                 /**< Number of scripts. */

    /* Physics properties. */
    bool is_static;       /**< Immovable (infinite mass). */
    bool is_kinematic;    /**< Velocity-driven, no forces. */
    float mass;           /**< Mass in kg (0 = infinite for static). */
    float friction;       /**< Surface friction (0–1+). */
    float restitution;    /**< Bounciness (0–1). */

    /* Custom attributes. */
    entity_attrs_t attrs; /**< Key-value attributes. */
} entity_def_t;

/**
 * @brief Result code for entity definition operations.
 */
typedef enum entity_def_result {
    ENTITY_DEF_OK = 0,
    ENTITY_DEF_ERR_FILE_NOT_FOUND,
    ENTITY_DEF_ERR_PARSE_FAILED,
    ENTITY_DEF_ERR_INVALID_SCHEMA,
    ENTITY_DEF_ERR_OUT_OF_MEMORY,
} entity_def_result_t;

/* ------------------------------------------------------------------------ */
/* API                                                                       */
/* ------------------------------------------------------------------------ */

/**
 * @brief Initialize an entity definition to defaults.
 * @param def Definition to initialize. Must not be NULL.
 */
void entity_def_init(entity_def_t *def);

/**
 * @brief Load an entity definition from a .fentity file.
 * @param path File path to .fentity JSON.
 * @param out_def Output definition. Must not be NULL.
 * @return ENTITY_DEF_OK on success, error code otherwise.
 */
entity_def_result_t entity_def_load(const char *path, entity_def_t *out_def);

/**
 * @brief Parse an entity definition from JSON string.
 * @param json JSON string (null-terminated).
 * @param out_def Output definition. Must not be NULL.
 * @return ENTITY_DEF_OK on success, error code otherwise.
 */
entity_def_result_t entity_def_parse(const char *json, entity_def_t *out_def);

/**
 * @brief Get error string for result code.
 * @param result Result code from entity_def_load or entity_def_parse.
 * @return Human-readable error string.
 */
const char *entity_def_result_str(entity_def_result_t result);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_EDITOR_DEF_ENTITY_DEF_H */
