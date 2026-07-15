/**
 * @file sdf_field_serialize.c
 * @brief sdf_field (de)serialization to the asset/on-disk form (see sdf_field.h).
 */
#include "ferrum/renderer/chunk/sdf_field.h"

#include <string.h>

/* magic(4) + dims[3] i32 + min[3] f32 + voxel f32 */
#define SDFF_HEADER (4u + 3u*4u + 3u*4u + 4u)
static const char SDFF_MAGIC[4] = { 'S', 'D', 'F', 'F' };

size_t sdf_field_serial_size(const sdf_field_t *f)
{
    if (f == NULL)
        return 0u;
    return (size_t)SDFF_HEADER + (size_t)sdf_field_cells(f) * sizeof(float);
}

size_t sdf_field_serialize(const sdf_field_t *f, void *buf, size_t cap)
{
    if (f == NULL || buf == NULL || f->data == NULL)
        return 0u;
    size_t need = sdf_field_serial_size(f);
    if (cap < need)
        return 0u;

    unsigned char *p = (unsigned char *)buf;
    memcpy(p, SDFF_MAGIC, 4);                          p += 4;
    memcpy(p, f->dims, 3 * sizeof(int32_t));           p += 3 * sizeof(int32_t);
    memcpy(p, f->min, 3 * sizeof(float));              p += 3 * sizeof(float);
    memcpy(p, &f->voxel, sizeof(float));               p += sizeof(float);
    size_t bytes = (size_t)sdf_field_cells(f) * sizeof(float);
    memcpy(p, f->data, bytes);
    return need;
}

bool sdf_field_deserialize(const void *buf, size_t len, sdf_field_t *out,
                           float *data_dst, size_t data_cap_floats)
{
    if (buf == NULL || out == NULL || data_dst == NULL || len < SDFF_HEADER)
        return false;
    const unsigned char *p = (const unsigned char *)buf;
    if (memcmp(p, SDFF_MAGIC, 4) != 0)
        return false;
    p += 4;

    int32_t dims[3];
    memcpy(dims, p, 3 * sizeof(int32_t));              p += 3 * sizeof(int32_t);
    if (dims[0] <= 0 || dims[1] <= 0 || dims[2] <= 0)
        return false;
    size_t cells = (size_t)dims[0] * (size_t)dims[1] * (size_t)dims[2];
    if (data_cap_floats < cells || len < SDFF_HEADER + cells * sizeof(float))
        return false;

    out->dims[0] = dims[0]; out->dims[1] = dims[1]; out->dims[2] = dims[2];
    memcpy(out->min, p, 3 * sizeof(float));            p += 3 * sizeof(float);
    memcpy(&out->voxel, p, sizeof(float));             p += sizeof(float);
    memcpy(data_dst, p, cells * sizeof(float));
    out->data = data_dst;
    return true;
}
