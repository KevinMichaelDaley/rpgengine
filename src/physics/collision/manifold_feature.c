/**
 * @file manifold_feature.c
 * @brief Feature ID computation for face and vertex contacts.
 */

#include "ferrum/physics/manifold.h"

uint32_t phys_feature_id_face(uint8_t face) {
    return 0x10000u | (uint32_t)face;
}

uint32_t phys_feature_id_vertex(uint8_t vertex) {
    return 0x20000u | (uint32_t)vertex;
}
