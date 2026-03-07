#include "ferrum/physics/body.h"

#include <stddef.h>

void phys_body_init(phys_body_t *body) {
    if (!body) {
        return;
    }

    *body = (phys_body_t){0};
    body->orientation.w = 1.0f;
    body->flags = PHYS_BODY_FLAG_STATIC;
    body->tier = 0;
    body->friction = 0.5f;
    body->restitution = 0.0f;
    body->linear_damping  = 0.0f; /* No drag by default. */
    body->angular_damping = 0.0f;
    body->entity_index = UINT32_MAX;
}

void phys_body_set_mass(phys_body_t *body, float mass) {
    if (!body) {
        return;
    }

    if (mass > 0.0f) {
        body->inv_mass = 1.0f / mass;
        body->flags &= ~PHYS_BODY_FLAG_STATIC;
    } else {
        body->inv_mass = 0.0f;
        body->flags |= PHYS_BODY_FLAG_STATIC;
    }
}
