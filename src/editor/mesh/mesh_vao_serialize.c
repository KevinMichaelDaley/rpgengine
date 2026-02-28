/**
 * @file mesh_vao_serialize.c
 * @brief FVMA serializer — mesh_slot_t → byte buffer.
 *
 * Non-static functions: serialized_size, serialize (2 of 4 allowed).
 */
#include "ferrum/editor/mesh/mesh_vao_format.h"

#include <string.h>

/* ------------------------------------------------------------------------ */
/* Internal helpers                                                          */
/* ------------------------------------------------------------------------ */

/** @brief Write a uint32_t to buf (little-endian). */
static void write_u32_(uint8_t **p, uint32_t val) {
    memcpy(*p, &val, 4);
    *p += 4;
}

/** @brief Write a float array to buf. */
static void write_floats_(uint8_t **p, const float *data, uint32_t count) {
    size_t bytes = (size_t)count * sizeof(float);
    memcpy(*p, data, bytes);
    *p += bytes;
}

/** @brief Write a u32 array to buf. */
static void write_u32s_(uint8_t **p, const uint32_t *data, uint32_t count) {
    size_t bytes = (size_t)count * sizeof(uint32_t);
    memcpy(*p, data, bytes);
    *p += bytes;
}

/** @brief Write a u16 array to buf. */
static void write_u16s_(uint8_t **p, const uint16_t *data, uint32_t count) {
    size_t bytes = (size_t)count * sizeof(uint16_t);
    memcpy(*p, data, bytes);
    *p += bytes;
}

/* ------------------------------------------------------------------------ */
/* Public API                                                                */
/* ------------------------------------------------------------------------ */

size_t mesh_vao_serialized_size(const mesh_slot_t *slot, uint32_t flags) {
    if (!slot) { return 0; }

    uint32_t vc = slot->vertex_count;
    uint32_t ic = slot->index_count;
    uint32_t fc = ic / 3;

    size_t size = MESH_VAO_HEADER_SIZE;
    size += (size_t)vc * 12;                             /* positions always */
    if (flags & MESH_VAO_FLAG_NORMALS)  { size += (size_t)vc * 12; }
    if (flags & MESH_VAO_FLAG_TANGENTS) { size += (size_t)vc * 16; }
    if (flags & MESH_VAO_FLAG_UV0)      { size += (size_t)vc * 8;  }
    if (flags & MESH_VAO_FLAG_UV1)      { size += (size_t)vc * 8;  }
    if (flags & MESH_VAO_FLAG_COLORS)   { size += (size_t)vc * 16; }
    size += (size_t)ic * 4;   /* indices */
    size += (size_t)fc * 2;   /* polygroup IDs */

    return size;
}

size_t mesh_vao_serialize(const mesh_slot_t *slot, uint32_t flags,
                          uint8_t *buf, size_t buf_size) {
    if (!slot || !buf) { return 0; }

    size_t needed = mesh_vao_serialized_size(slot, flags);
    if (needed == 0 || buf_size < needed) { return 0; }

    uint8_t *p = buf;
    uint32_t vc = slot->vertex_count;
    uint32_t ic = slot->index_count;
    uint32_t fc = ic / 3;

    /* Header */
    write_u32_(&p, MESH_VAO_MAGIC);
    write_u32_(&p, MESH_VAO_VERSION);
    write_u32_(&p, vc);
    write_u32_(&p, ic);
    write_u32_(&p, flags);
    write_u32_(&p, 0); /* polygroup_count (informational, set to 0 for now) */

    /* Positions (always) */
    if (vc > 0) {
        write_floats_(&p, slot->positions, vc * 3);
    }

    /* Optional attributes */
    if ((flags & MESH_VAO_FLAG_NORMALS) && vc > 0) {
        write_floats_(&p, slot->normals, vc * 3);
    }
    if ((flags & MESH_VAO_FLAG_TANGENTS) && vc > 0) {
        write_floats_(&p, slot->tangents, vc * 4);
    }
    if ((flags & MESH_VAO_FLAG_UV0) && vc > 0) {
        write_floats_(&p, slot->uvs[0], vc * 2);
    }
    if ((flags & MESH_VAO_FLAG_UV1) && vc > 0) {
        write_floats_(&p, slot->uvs[1], vc * 2);
    }
    if ((flags & MESH_VAO_FLAG_COLORS) && vc > 0) {
        write_floats_(&p, slot->colors, vc * 4);
    }

    /* Indices */
    if (ic > 0) {
        write_u32s_(&p, slot->indices, ic);
    }

    /* Polygroup IDs */
    if (fc > 0) {
        write_u16s_(&p, slot->polygroup_ids, fc);
    }

    return (size_t)(p - buf);
}
