#include "ferrum/physics/body.h"

bool phys_body_is_kinematic(const phys_body_t *body) {
    if (!body) {
        return false;
    }
    return (body->flags & PHYS_BODY_FLAG_KINEMATIC) != 0;
}

bool phys_body_is_sleeping(const phys_body_t *body) {
    if (!body) {
        return false;
    }
    return (body->flags & PHYS_BODY_FLAG_SLEEPING) != 0;
}

bool phys_body_is_static(const phys_body_t *body) {
    if (!body) {
        return false;
    }
    return (body->inv_mass == 0.0f) && !phys_body_is_kinematic(body);
}

void phys_body_set_sleeping(phys_body_t *body, bool sleeping) {
    if (!body) {
        return;
    }

    if (sleeping) {
        body->flags |= PHYS_BODY_FLAG_SLEEPING;
    } else {
        body->flags &= ~PHYS_BODY_FLAG_SLEEPING;
    }
}
