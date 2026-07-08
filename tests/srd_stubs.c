/**
 * @file srd_stubs.c
 * @brief Stub implementations for SRD v2 test linking.
 *
 * These stubs provide no-op definitions for functions referenced by
 * srd_grammar.c but not needed in isolated SRD v2 tests.
 */
#include "ferrum/procgen/procgen_srd_types.h"
#include "ferrum/procgen/srd/srd_tile.h"

void srd_tile_list_add(srd_tile_list_t *list, srd_tile_type_t type, float x,
                       float z, float floor_h, float ceil_h, float half_x,
                       float half_z) {
    (void)list; (void)type; (void)x; (void)z;
    (void)floor_h; (void)ceil_h; (void)half_x; (void)half_z;
}

void fr_room_box_init(fr_room_box_t *box) { (void)box; }
