#include <stdlib.h>

#include "ferrum/ecs/sparse_set.h"

void ecs_sparse_set_base_destroy(ecs_sparse_set_base_t *base) {
    if (base == NULL) {
        return;
    }
    free(base->dense);
    free(base->dense_entities);
    free(base->sparse);
    base->dense = NULL;
    base->dense_entities = NULL;
    base->sparse = NULL;
    base->capacity = 0u;
    base->size = 0u;
    base->stride = 0u;
}
