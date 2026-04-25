/**
 * @file material_def.h
 * @brief Material definition file format (.fmat).
 *
 * JSON-based material definitions that specify PBR texture slots
 * and material parameters.
 */
#ifndef FERRUM_EDITOR_DEF_MATERIAL_DEF_H
#define FERRUM_EDITOR_DEF_MATERIAL_DEF_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/* ------------------------------------------------------------------------ */
/* Constants                                                                 */
/* ------------------------------------------------------------------------ */

/** Maximum name length for material definition. */
#define MATERIAL_DEF_NAME_MAX 64

/** Maximum path length for texture files. */
#define MATERIAL_DEF_PATH_MAX 256

/* ------------------------------------------------------------------------ */
/* Types                                                                     */
/* ------------------------------------------------------------------------ */

/**
 * @brief Material definition loaded from .fmat file.
 */
typedef struct material_def {
    char name[MATERIAL_DEF_NAME_MAX];       /**< Display name. */

    /* Texture slots. */
    char slot_albedo[MATERIAL_DEF_PATH_MAX];
    char slot_normal[MATERIAL_DEF_PATH_MAX];
    char slot_roughness[MATERIAL_DEF_PATH_MAX];
    char slot_metallic[MATERIAL_DEF_PATH_MAX];
    char slot_emissive[MATERIAL_DEF_PATH_MAX];

    /* PBR parameters. */
    float roughness_factor;   /**< Multiplier for roughness (0–1). */
    float metallic_factor;    /**< Multiplier for metallic (0–1). */
    float emissive_strength;  /**< Emissive intensity (0+). */
    float alpha_cutoff;       /**< Alpha test threshold (0–1). */

    /* Flags. */
    bool double_sided;       /**< Render both faces. */
    bool alpha_blend;        /**< Enable alpha blending. */
    bool alpha_test;         /**< Enable alpha testing. */
} material_def_t;

/**
 * @brief Result code for material definition operations.
 */
typedef enum material_def_result {
    MATERIAL_DEF_OK = 0,
    MATERIAL_DEF_ERR_FILE_NOT_FOUND,
    MATERIAL_DEF_ERR_PARSE_FAILED,
    MATERIAL_DEF_ERR_INVALID_SCHEMA,
    MATERIAL_DEF_ERR_OUT_OF_MEMORY,
} material_def_result_t;

/* ------------------------------------------------------------------------ */
/* API                                                                       */
/* ------------------------------------------------------------------------ */

/**
 * @brief Initialize a material definition to defaults.
 * @param def Definition to initialize. Must not be NULL.
 */
void material_def_init(material_def_t *def);

/**
 * @brief Load a material definition from a .fmat file.
 * @param path File path to .fmat JSON.
 * @param out_def Output definition. Must not be NULL.
 * @return MATERIAL_DEF_OK on success, error code otherwise.
 */
material_def_result_t material_def_load(const char *path, material_def_t *out_def);

/**
 * @brief Parse a material definition from JSON string.
 * @param json JSON string (null-terminated).
 * @param out_def Output definition. Must not be NULL.
 * @return MATERIAL_DEF_OK on success, error code otherwise.
 */
material_def_result_t material_def_parse(const char *json, material_def_t *out_def);

/**
 * @brief Get error string for result code.
 * @param result Result code from material_def_load or material_def_parse.
 * @return Human-readable error string.
 */
const char *material_def_result_str(material_def_result_t result);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_EDITOR_DEF_MATERIAL_DEF_H */
