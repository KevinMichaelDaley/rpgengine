/**
 * @file sdf_field.h
 * @brief A sampled signed-distance field on a uniform cubic grid + trilinear
 *        sampling and resampling/downsampling (rpg-fzht). The per-chunk near SDF
 *        and the downsampled 128^3 medium/far fields all share this type; the
 *        gather sphere-traces them and the runtime uploads them to 3D textures.
 *
 * Ownership: @c data is BORROWED (caller-owned; @ref sdf_field_t is a view). A
 * negative value is inside solid, positive is air, and a large positive sentinel
 * (>= SDF_FIELD_OUTSIDE) marks samples outside the source grid. Nullability:
 * pointer args are required. No heap allocation here.
 */
#ifndef FERRUM_RENDERER_CHUNK_SDF_FIELD_H
#define FERRUM_RENDERER_CHUNK_SDF_FIELD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Sentinel returned when sampling outside a field's grid. */
#define SDF_FIELD_OUTSIDE 1.0e9f

/** A signed-distance grid: dims cells, origin @c min, cubic @c voxel, @c data. */
typedef struct sdf_field {
    int    dims[3];  /**< cells per axis. */
    float  min[3];   /**< world origin (min corner of cell 0). */
    float  voxel;    /**< cubic cell size. */
    float *data;     /**< dims[0]*dims[1]*dims[2] signed distances (borrowed). */
} sdf_field_t;

/** @brief Cell count (product of dims). */
uint32_t sdf_field_cells(const sdf_field_t *f);

/**
 * @brief Trilinearly sample @p f at world point (@p x,@p y,@p z). Returns
 *        @ref SDF_FIELD_OUTSIDE when the point falls outside the grid.
 */
float sdf_field_sample(const sdf_field_t *f, float x, float y, float z);

/**
 * @brief Fill @p dst by sampling @p src at each of @p dst's cell centres
 *        (trilinear). @p dst's dims/min/voxel/data must be preset; a coarser
 *        @p dst downsamples @p src (e.g. a 128^3 medium/far field). Cells whose
 *        centre lies outside @p src get @ref SDF_FIELD_OUTSIDE.
 */
void sdf_field_resample(const sdf_field_t *src, sdf_field_t *dst);

/* ── Serialization (asset/on-disk form; native-endian) ─────────────────────
 * Layout: magic "SDFF", int32 dims[3], float min[3], float voxel, then
 * dims[0]*dims[1]*dims[2] float distances. Folds in the rpg-iudw FLM SDF chunk.
 */

/** @brief Bytes @ref sdf_field_serialize needs for @p f (header + data). */
size_t sdf_field_serial_size(const sdf_field_t *f);

/**
 * @brief Serialize @p f into @p buf (must be >= @ref sdf_field_serial_size).
 *        Returns bytes written, or 0 on a NULL arg or too-small buffer.
 */
size_t sdf_field_serialize(const sdf_field_t *f, void *buf, size_t cap);

/**
 * @brief Deserialize a field from @p buf (@p len bytes) into @p out; the
 *        distances are written into @p data_dst (>= dims product floats) and
 *        @c out->data points at it. Returns false on a NULL arg, short/truncated
 *        buffer, bad magic, or a @p data_dst too small for the stored grid.
 */
bool sdf_field_deserialize(const void *buf, size_t len, sdf_field_t *out,
                           float *data_dst, size_t data_cap_floats);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_RENDERER_CHUNK_SDF_FIELD_H */
