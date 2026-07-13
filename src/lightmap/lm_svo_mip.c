/**
 * @file lm_svo_mip.c
 * @brief Filtered SVO shading pyramid (see lm_svo_mip.h).
 */
#include "ferrum/lightmap/lm_svo_mip.h"

#include <stddef.h>

/* Recursively compute the solid-leaf-weighted average shade of the subtree at
 * @p node_idx, writing it into the node. Returns the subtree's solid-leaf count
 * (the averaging weight, so larger solid regions dominate a coarse voxel). */
static uint32_t lm_svo_mip_node(npc_svo_grid_t *svo,
                                const lm_material_table_t *table,
                                uint32_t node_idx)
{
    if (node_idx == NPC_SVO_INVALID_NODE || node_idx >= svo->node_count)
        return 0;
    npc_svo_node_t *n = &svo->nodes[node_idx];

    if (n->occupancy == 0) {
        /* Leaf: a solid leaf takes its material's shade, air stays black. */
        if (n->flags & NPC_SVO_FLAG_SOLID) {
            lm_material_t m = lm_material_lookup(table, n->material);
            n->diffuse[0] = m.albedo.x; n->diffuse[1] = m.albedo.y;
            n->diffuse[2] = m.albedo.z;
            n->emissive[0] = m.emissive.x; n->emissive[1] = m.emissive.y;
            n->emissive[2] = m.emissive.z;
            return 1;
        }
        n->diffuse[0] = n->diffuse[1] = n->diffuse[2] = 0.0f;
        n->emissive[0] = n->emissive[1] = n->emissive[2] = 0.0f;
        return 0;
    }

    /* Interior: weighted average over occupied children. */
    float d[3] = { 0.0f, 0.0f, 0.0f };
    float e[3] = { 0.0f, 0.0f, 0.0f };
    uint32_t w = 0;
    for (int c = 0; c < 8; ++c) {
        uint32_t ci = n->children[c];
        if (ci == NPC_SVO_INVALID_NODE)
            continue;
        uint32_t cw = lm_svo_mip_node(svo, table, ci);
        if (cw == 0)
            continue;
        const npc_svo_node_t *cn = &svo->nodes[ci];
        float fw = (float)cw;
        for (int k = 0; k < 3; ++k) {
            d[k] += cn->diffuse[k] * fw;
            e[k] += cn->emissive[k] * fw;
        }
        w += cw;
    }
    float inv = (w > 0) ? 1.0f / (float)w : 0.0f;
    for (int k = 0; k < 3; ++k) {
        n->diffuse[k] = d[k] * inv;
        n->emissive[k] = e[k] * inv;
    }
    return w;
}

void lm_svo_mip_build(npc_svo_grid_t *svo, const lm_material_table_t *table)
{
    if (svo == NULL || svo->nodes == NULL || svo->node_count == 0 ||
        table == NULL)
        return;
    lm_svo_mip_node(svo, table, 0u); /* the root is always node 0 */
}

lm_svo_shade_t lm_svo_mip_sample(const npc_svo_grid_t *svo, uint32_t leaf_node,
                                 uint32_t levels_up)
{
    lm_svo_shade_t s = { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } };
    if (svo == NULL || svo->nodes == NULL || leaf_node == NPC_SVO_INVALID_NODE ||
        leaf_node >= svo->node_count)
        return s;

    uint32_t idx = leaf_node;
    for (uint32_t i = 0; i < levels_up; ++i) {
        uint32_t p = svo->nodes[idx].parent;
        if (p == NPC_SVO_INVALID_NODE || p >= svo->node_count)
            break; /* reached the root */
        idx = p;
    }
    const npc_svo_node_t *n = &svo->nodes[idx];
    s.diffuse = (vec3_t){ n->diffuse[0], n->diffuse[1], n->diffuse[2] };
    s.emissive = (vec3_t){ n->emissive[0], n->emissive[1], n->emissive[2] };
    return s;
}
