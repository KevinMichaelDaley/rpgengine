#include "ferrum/renderer/skinning/pipeline.h"

#include <stdlib.h>

int skinning_pipeline_build_draw_list(const skinning_pipeline_t *pipeline,
                                      ecs_sparse_set_base_t *skins,
                                      skinning_draw_list_t *out_list,
                                      entity_t *storage,
                                      uint32_t storage_capacity) {
    if (pipeline == NULL || skins == NULL || out_list == NULL || storage == NULL) {
        return SKINNING_PIPELINE_ERR_INVALID;
    }
    if (skins->size > storage_capacity) {
        return SKINNING_PIPELINE_ERR_INVALID;
    }

    uint32_t count = skins->size;
    uint32_t *palette_indices = NULL;
    if (count > 0u) {
        palette_indices = (uint32_t *)malloc(sizeof(uint32_t) * count);
        if (palette_indices == NULL) {
            return SKINNING_PIPELINE_ERR_OOM;
        }
    }

    skinning_skin_t *skin_data = (skinning_skin_t *)skins->dense;
    for (uint32_t i = 0; i < count; ++i) {
        storage[i] = skins->dense_entities[i];
        if (skinning_pipeline_palette_index(pipeline, skin_data[i].skeleton_entity, &palette_indices[i]) !=
            SKINNING_PIPELINE_OK) {
            free(palette_indices);
            return SKINNING_PIPELINE_ERR_NOT_FOUND;
        }
    }

    for (uint32_t i = 1; i < count; ++i) {
        entity_t key_entity = storage[i];
        uint32_t key_palette = palette_indices[i];
        uint32_t j = i;
        while (j > 0) {
            uint32_t prev_palette = palette_indices[j - 1];
            entity_t prev_entity = storage[j - 1];
            if (prev_palette < key_palette) {
                break;
            }
            if (prev_palette == key_palette && prev_entity.index < key_entity.index) {
                break;
            }
            storage[j] = prev_entity;
            palette_indices[j] = prev_palette;
            --j;
        }
        storage[j] = key_entity;
        palette_indices[j] = key_palette;
    }

    free(palette_indices);
    out_list->entities = storage;
    out_list->count = count;
    return SKINNING_PIPELINE_OK;
}
