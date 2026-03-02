/**
 * @file body_trigger.c
 * @brief Trigger body flag query.
 *
 * Non-static functions: phys_body_is_trigger (1).
 */

#include "ferrum/physics/body.h"

bool phys_body_is_trigger(const phys_body_t *body) {
    if (!body) return false;
    return (body->flags & PHYS_BODY_FLAG_TRIGGER) != 0;
}
