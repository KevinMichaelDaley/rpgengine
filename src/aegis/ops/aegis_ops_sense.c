/**
 * @file aegis_ops_sense.c
 * @brief SENSE_QUERY async opcode submit handler.
 *
 * Packs query parameters into a 24-byte param block:
 *   query_mode, sense_flags, target_entity, npc_position, max_range.
 *
 * The querier's position and dynamic sense range are read from the
 * entity_view snapshot using vm->entity_id.  If the entity or view
 * is missing, falls back to position (0,0,0) and range 50.0f.
 */

#include "ferrum/aegis/aegis_ops_async.h"
#include "ferrum/aegis/aegis_vm.h"
#include "ferrum/aegis/aegis_async.h"
#include "ferrum/aegis/aegis_decode.h"
#include "ferrum/aegis/aegis_sense.h"
#include "ferrum/entity/entity_attrs.h"
#include "ferrum/editor/edit_script_env.h"

#include <string.h>
#include <math.h>

/** Result slot size for sense queries (4 KB, supports pagination). */
#define SENSE_RESULT_SLOT_SIZE AEGIS_SENSE_RESULT_CAPACITY

/** Default sense range when entity attribute is absent. */
#define SENSE_RANGE_DEFAULT 50.0f

/**
 * @brief Look up the snapshot index for entity_id in the entity view.
 * @return Snapshot index, or UINT32_MAX if not found.
 */
static uint32_t find_entity_in_view(const script_entity_view_t *view,
                                    uint32_t entity_id) {
    if (!view || !view->entities) return UINT32_MAX;
    for (uint32_t i = 0; i < view->count; i++) {
        if (view->entities[i].entity_id == entity_id) {
            return i;
        }
    }
    return UINT32_MAX;
}

/**
 * @brief Read a float attribute from the entity snapshot, returning default
 *        if the attribute is missing or the view is NULL.
 */
static float read_entity_f32(const script_entity_view_t *view,
                             uint32_t snapshot_idx, uint16_t key,
                             float default_val) {
    if (!view || snapshot_idx >= view->count) return default_val;
    const entity_attrs_t *attrs = &view->entities[snapshot_idx].attrs;
    uint8_t type = 0, size = 0;
    const void *val = entity_attrs_get(attrs, key, &type, &size);
    if (!val || type != SCRIPT_ATTR_F32 || size < sizeof(float)) return default_val;
    float result;
    memcpy(&result, val, sizeof(result));
    return result;
}

/**
 * @brief Read a vec3 attribute from the entity snapshot, returning default
 *        if the attribute is missing.
 */
static void read_entity_vec3(const script_entity_view_t *view,
                             uint32_t snapshot_idx, uint16_t key,
                             float default_val,
                             float out[3]) {
    if (!view || snapshot_idx >= view->count) {
        out[0] = out[1] = out[2] = default_val;
        return;
    }
    const entity_attrs_t *attrs = &view->entities[snapshot_idx].attrs;
    uint8_t type = 0, size = 0;
    const void *val = entity_attrs_get(attrs, key, &type, &size);
    if (!val || type != SCRIPT_ATTR_VEC3 || size < 12) {
        out[0] = out[1] = out[2] = default_val;
        return;
    }
    memcpy(out, val, 12);
}

bool aegis_op_sense_query(aegis_vm_t *vm, const aegis_decode_result_t *d) {
    if (vm->async_task_count >= vm->config.max_async_tasks) {
        return false;
    }
    if (vm->async_task_count >= 32) {
        return false;
    }

    /* Allocate 4 KB result slot in heap arena. */
    int32_t offset = aegis_memory_alloc(&vm->memory, SENSE_RESULT_SLOT_SIZE);
    if (offset < 0) {
        return false;
    }

    /* Decode mode flags. */
    uint32_t mode_flags = vm->regs[d->raw_b].u32;
    uint16_t query_mode = (uint16_t)(mode_flags & 0xFFFFu);
    uint16_t sense_flags = (uint16_t)(mode_flags >> 16);
    uint32_t target_entity = vm->regs[d->raw_c].entity_id;

    /* Look up querier position and sense range from entity_view. */
    float pos[3] = {0.0f, 0.0f, 0.0f};
    float max_range = SENSE_RANGE_DEFAULT;
    uint32_t querier_idx = find_entity_in_view(
        (const script_entity_view_t *)vm->entity_view, vm->entity_id);
    if (querier_idx != UINT32_MAX) {
        read_entity_vec3((const script_entity_view_t *)vm->entity_view,
                         querier_idx, SCRIPT_KEY_POS, 0.0f, pos);
        max_range = read_entity_f32((const script_entity_view_t *)vm->entity_view,
                                    querier_idx, SCRIPT_KEY_SENSE_RANGE,
                                    SENSE_RANGE_DEFAULT);
        if (max_range <= 0.0f || !isfinite(max_range)) {
            max_range = SENSE_RANGE_DEFAULT;
        }
    }

    /* Build task. */
    aegis_async_task_t task;
    memset(&task, 0, sizeof(task));
    atomic_store(&task.status, AEGIS_ASYNC_PENDING);
    task.task_type  = AEGIS_TASK_SENSE_QUERY;
    task.result_ptr = vm->memory.base + offset;
    task.result_cap = SENSE_RESULT_SLOT_SIZE;

    /* Pack params (24 bytes). */
    memcpy(task.params,      &query_mode,     sizeof(query_mode));
    memcpy(task.params + 2,  &sense_flags,    sizeof(sense_flags));
    memcpy(task.params + 4,  &target_entity,  sizeof(target_entity));
    memcpy(task.params + 8,  pos,             12);
    memcpy(task.params + 20, &max_range,      sizeof(max_range));

    /* Track in VM's local task array. */
    uint32_t idx = vm->async_task_count;
    vm->async_tasks[idx] = task;
    vm->async_tasks[idx].result_ptr = vm->memory.base + offset;
    vm->async_task_count++;

    task.status_ptr = &vm->async_tasks[idx].status;

    if (!aegis_async_buffer_submit(vm->async_buffer, &task)) {
        vm->async_task_count--;
        return false;
    }

    vm->regs[d->raw_a].i32 = offset;
    return true;
}
