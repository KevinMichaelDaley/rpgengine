/**
 * @file aegis_ops_llm.c
 * @brief LLM prompt opcode handler.
 *
 * Submits AEGIS_TASK_LLM_PROMPT to the async buffer. Params layout:
 *   params[0..3]  = prompt heap offset (int32_t)
 *   params[4..7]  = max_tokens (int32_t, clamped)
 *   params[8..11] = prompt length (int32_t)
 */

#include "ferrum/aegis/aegis_ops_llm.h"
#include "ferrum/aegis/aegis_vm.h"
#include "ferrum/aegis/aegis_async.h"
#include "ferrum/aegis/aegis_decode.h"
#include "ferrum/engine_settings.h"

#include <string.h>

#define LLM_RESULT_SLOT_SIZE 256
#define LLM_MAX_TOKENS_DEFAULT 4096
#define LLM_MAX_PROMPT_LEN 16384

static bool submit_llm_prompt(aegis_vm_t *vm, const aegis_decode_result_t *d) {
    if (vm->async_task_count >= vm->config.max_async_tasks) {
        return false;
    }
    if (vm->async_task_count >= 32) {
        return false;
    }

    /* Read prompt offset from register B. */
    int32_t prompt_off = vm->regs[d->raw_b].i32;
    if (prompt_off < 0 || (uint32_t)prompt_off >= vm->memory.arena_size) {
        return false;
    }

    /* Validate prompt is non-empty and within bounds. */
    const char *prompt = (const char *)(vm->memory.base + prompt_off);
    uint32_t arena_end = vm->memory.arena_size - prompt_off;
    uint32_t plen = 0;
    while (plen < arena_end && prompt[plen] != '\0') {
        plen++;
    }
    if (plen == 0 || plen >= arena_end) {
        return false; /* Empty or unterminated. */
    }
    if (plen > LLM_MAX_PROMPT_LEN) {
        return false;
    }

    /* Clamp max_tokens. */
    int32_t max_tokens = vm->regs[d->raw_c].i32;
    if (max_tokens <= 0) {
        max_tokens = LLM_MAX_TOKENS_DEFAULT;
    }
    const fr_engine_settings_t *settings = fr_engine_settings_get();
    if (settings && settings->llm_max_tokens > 0 &&
        (uint32_t)max_tokens > settings->llm_max_tokens) {
        max_tokens = (int32_t)settings->llm_max_tokens;
    }
    if ((uint32_t)max_tokens > LLM_MAX_TOKENS_DEFAULT) {
        max_tokens = LLM_MAX_TOKENS_DEFAULT;
    }

    /* Allocate result slot in heap arena. */
    int32_t offset = aegis_memory_alloc(&vm->memory, LLM_RESULT_SLOT_SIZE);
    if (offset < 0) {
        return false;
    }

    /* Build task. */
    aegis_async_task_t task;
    memset(&task, 0, sizeof(task));
    atomic_store(&task.status, AEGIS_ASYNC_PENDING);
    task.task_type  = AEGIS_TASK_LLM_PROMPT;
    task.result_ptr = vm->memory.base + offset;
    task.result_cap = LLM_RESULT_SLOT_SIZE;

    /* Pack params: prompt_off, max_tokens, prompt_len. */
    memcpy(task.params,      &prompt_off, sizeof(prompt_off));
    memcpy(task.params + 4,  &max_tokens, sizeof(max_tokens));
    memcpy(task.params + 8,  &plen,       sizeof(plen));

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

bool aegis_op_llm_prompt(aegis_vm_t *vm, const aegis_decode_result_t *d) {
    return submit_llm_prompt(vm, d);
}
