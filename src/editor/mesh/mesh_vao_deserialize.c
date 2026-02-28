/**
 * @file mesh_vao_deserialize.c
 * @brief FVMA deserializer — byte buffer → mesh_slot_t.
 *
 * Non-static functions: deserialize (1 of 4 allowed).
 */
#include "ferrum/editor/mesh/mesh_vao_format.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------ */
/* Internal helpers                                                          */
/* ------------------------------------------------------------------------ */

/** @brief Read a uint32_t from buf (little-endian). */
static uint32_t read_u32_(const uint8_t **p) {
    uint32_t val;
    memcpy(&val, *p, 4);
    *p += 4;
    return val;
}

/** @brief Read float array from buf. */
static void read_floats_(const uint8_t **p, float *dst, uint32_t count) {
    size_t bytes = (size_t)count * sizeof(float);
    memcpy(dst, *p, bytes);
    *p += bytes;
}

/** @brief Read u32 array from buf. */
static void read_u32s_(const uint8_t **p, uint32_t *dst, uint32_t count) {
    size_t bytes = (size_t)count * sizeof(uint32_t);
    memcpy(dst, *p, bytes);
    *p += bytes;
}

/** @brief Read u16 array from buf. */
static void read_u16s_(const uint8_t **p, uint16_t *dst, uint32_t count) {
    size_t bytes = (size_t)count * sizeof(uint16_t);
    memcpy(dst, *p, bytes);
    *p += bytes;
}

/* ------------------------------------------------------------------------ */
/* Public API                                                                */
/* ------------------------------------------------------------------------ */

bool mesh_vao_deserialize(const uint8_t *buf, size_t buf_size,
                          mesh_slot_t *out) {
    if (!buf || !out) { return false; }
    if (buf_size < MESH_VAO_HEADER_SIZE) { return false; }

    const uint8_t *p = buf;

    /* Read header */
    uint32_t magic   = read_u32_(&p);
    uint32_t version = read_u32_(&p);
    uint32_t vc      = read_u32_(&p);
    uint32_t ic      = read_u32_(&p);
    uint32_t flags   = read_u32_(&p);
    /* polygroup_count */ read_u32_(&p);

    /* Validate header */
    if (magic != MESH_VAO_MAGIC)     { return false; }
    if (version != MESH_VAO_VERSION) { return false; }
    if (vc > MESH_SLOT_MAX_VERTICES) { return false; }
    if (ic > MESH_SLOT_MAX_INDICES)  { return false; }

    uint32_t fc = ic / 3;

    /* Calculate expected data size */
    size_t data_size = (size_t)vc * 12; /* positions always */
    if (flags & MESH_VAO_FLAG_NORMALS)  { data_size += (size_t)vc * 12; }
    if (flags & MESH_VAO_FLAG_TANGENTS) { data_size += (size_t)vc * 16; }
    if (flags & MESH_VAO_FLAG_UV0)      { data_size += (size_t)vc * 8;  }
    if (flags & MESH_VAO_FLAG_UV1)      { data_size += (size_t)vc * 8;  }
    if (flags & MESH_VAO_FLAG_COLORS)   { data_size += (size_t)vc * 16; }
    data_size += (size_t)ic * 4;
    data_size += (size_t)fc * 2;

    if (buf_size < MESH_VAO_HEADER_SIZE + data_size) { return false; }

    /* Allocate output slot */
    if (!mesh_slot_init(out, vc, ic)) { return false; }

    /* Read positions (always present) */
    if (vc > 0) {
        read_floats_(&p, out->positions, vc * 3);
    }

    /* Read optional attributes */
    if (flags & MESH_VAO_FLAG_NORMALS) {
        if (vc > 0) { read_floats_(&p, out->normals, vc * 3); }
    }
    if (flags & MESH_VAO_FLAG_TANGENTS) {
        if (vc > 0) { read_floats_(&p, out->tangents, vc * 4); }
    }
    if (flags & MESH_VAO_FLAG_UV0) {
        if (vc > 0) { read_floats_(&p, out->uvs[0], vc * 2); }
    }
    if (flags & MESH_VAO_FLAG_UV1) {
        if (vc > 0) { read_floats_(&p, out->uvs[1], vc * 2); }
    }
    if (flags & MESH_VAO_FLAG_COLORS) {
        if (vc > 0) { read_floats_(&p, out->colors, vc * 4); }
    }

    /* Read indices */
    if (ic > 0) {
        read_u32s_(&p, out->indices, ic);
    }

    /* Read polygroup IDs */
    if (fc > 0) {
        read_u16s_(&p, out->polygroup_ids, fc);
    }

    out->vertex_count = vc;
    out->index_count  = ic;

    return true;
}
