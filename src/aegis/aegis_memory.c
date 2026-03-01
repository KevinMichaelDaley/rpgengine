/**
 * @file aegis_memory.c
 * @brief Aegis VM three-zone memory layout implementation.
 *
 * Per ref/aegis_bytecode_spec.md §3.6.
 */

#include "ferrum/aegis/aegis_memory.h"
#include <string.h>

/* ----------------------------------------------------------------------- */
/* Init                                                                      */
/* ----------------------------------------------------------------------- */

bool aegis_memory_init(aegis_memory_t *mem, uint8_t *buf,
                       uint32_t arena_size, uint32_t static_size,
                       uint32_t stack_size) {
    if (arena_size == 0) {
        return false;
    }
    if ((uint64_t)static_size + stack_size > arena_size) {
        return false;
    }

    mem->base        = buf;
    mem->arena_size  = arena_size;
    mem->static_end  = static_size;
    mem->stack_limit = static_size + stack_size;
    mem->stack_top   = static_size; /* stack starts empty at static_end */
    mem->call_depth  = 0;
    mem->heap_bump   = mem->stack_limit; /* heap starts at stack_limit */

    /* Zero-initialize the static zone. */
    if (static_size > 0) {
        memset(buf, 0, static_size);
    }

    return true;
}

/* ----------------------------------------------------------------------- */
/* Static array                                                              */
/* ----------------------------------------------------------------------- */

bool aegis_memory_static_load(const aegis_memory_t *mem, uint32_t offset,
                              aegis_register_t *out) {
    if (offset + 16 > mem->static_end) {
        return false;
    }
    memcpy(out, mem->base + offset, 16);
    return true;
}

bool aegis_memory_static_store(aegis_memory_t *mem, uint32_t offset,
                               const aegis_register_t *val) {
    if (offset + 16 > mem->static_end) {
        return false;
    }
    memcpy(mem->base + offset, val, 16);
    return true;
}

/* ----------------------------------------------------------------------- */
/* Call stack                                                                */
/* ----------------------------------------------------------------------- */

bool aegis_memory_stack_push(aegis_memory_t *mem,
                             const aegis_register_t *val) {
    if (mem->stack_top + 16 > mem->stack_limit) {
        return false;
    }
    memcpy(mem->base + mem->stack_top, val, 16);
    mem->stack_top += 16;
    return true;
}

bool aegis_memory_stack_pop(aegis_memory_t *mem, aegis_register_t *out) {
    if (mem->stack_top < mem->static_end + 16) {
        return false;
    }
    mem->stack_top -= 16;
    memcpy(out, mem->base + mem->stack_top, 16);
    return true;
}

bool aegis_memory_push_frame(aegis_memory_t *mem, uint32_t return_pc) {
    /* Store the return PC as a register-sized value on the stack. */
    aegis_register_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.u32 = return_pc;
    if (!aegis_memory_stack_push(mem, &frame)) {
        return false;
    }
    mem->call_depth++;
    return true;
}

bool aegis_memory_pop_frame(aegis_memory_t *mem, uint32_t *out_return_pc) {
    if (mem->call_depth == 0) {
        return false;
    }
    aegis_register_t frame;
    if (!aegis_memory_stack_pop(mem, &frame)) {
        return false;
    }
    *out_return_pc = frame.u32;
    mem->call_depth--;
    return true;
}

uint32_t aegis_memory_call_depth(const aegis_memory_t *mem) {
    return mem->call_depth;
}

/* ----------------------------------------------------------------------- */
/* Heap arena                                                                */
/* ----------------------------------------------------------------------- */

int32_t aegis_memory_alloc(aegis_memory_t *mem, uint32_t size) {
    if (mem->heap_bump + size > mem->arena_size) {
        return -1;
    }
    int32_t offset = (int32_t)mem->heap_bump;
    mem->heap_bump += size;
    return offset;
}

void aegis_memory_heap_reset(aegis_memory_t *mem) {
    mem->heap_bump = mem->stack_limit;
}

bool aegis_memory_heap_load(const aegis_memory_t *mem,
                            uint32_t base, uint32_t offset,
                            aegis_register_t *out) {
    uint64_t addr = (uint64_t)base + offset;
    /* Must be within heap zone: [stack_limit, arena_size). */
    if (addr < mem->stack_limit || addr + 16 > mem->arena_size) {
        return false;
    }
    memcpy(out, mem->base + addr, 16);
    return true;
}

bool aegis_memory_heap_store(aegis_memory_t *mem,
                             uint32_t base, uint32_t offset,
                             const aegis_register_t *val) {
    uint64_t addr = (uint64_t)base + offset;
    /* Must be within heap zone: [stack_limit, arena_size). */
    if (addr < mem->stack_limit || addr + 16 > mem->arena_size) {
        return false;
    }
    memcpy(mem->base + addr, val, 16);
    return true;
}
