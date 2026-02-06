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
