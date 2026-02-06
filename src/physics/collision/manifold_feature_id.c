/**
 * @file manifold_feature_id.c
 * @brief Feature ID encoding for edge contacts.
 */

#include "ferrum/physics/manifold.h"

uint32_t phys_feature_id_edge(uint8_t face, uint8_t edge) {
    return ((uint32_t)face << 8) | (uint32_t)edge;
}
