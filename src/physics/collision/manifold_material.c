/**
 * @file manifold_material.c
 * @brief Material combination functions for contact manifolds.
 */

#include "ferrum/physics/manifold.h"

#include <math.h>

float phys_combine_friction(float f1, float f2) {
    return sqrtf(f1 * f2);
}

float phys_combine_restitution(float r1, float r2) {
    return fminf(r1, r2);
}
