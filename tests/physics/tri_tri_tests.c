/**
 * @file tri_tri_tests.c
 * @brief Tests for triangle-vs-triangle narrowphase intersection.
 */

#include "ferrum/physics/mesh_narrowphase.h"
#include "ferrum/physics/mesh_collider.h"
#include "ferrum/physics/manifold.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

#define ASSERT(expr) do { \
    if (!(expr)) { \
        printf("  FAIL: %s (line %d)\n", #expr, __LINE__); \
        return 0; \
    } \
} while (0)

#define ASSERT_NEAR(a, b, eps) ASSERT(fabsf((a) - (b)) < (eps))

#define RUN(fn) do { \
    printf("RUN  %s\n", #fn); \
    if (fn()) printf("OK   %s\n", #fn); \
    else { printf("FAIL %s\n", #fn); fails++; } \
    total++; \
} while (0)

/* ---- Tests ---- */

/** Two coplanar overlapping triangles in XZ plane. */
static int test_coplanar_overlap(void) {
    phys_triangle_t a = {{{-1,0,-1}, {1,0,-1}, {0,0,1}}};
    phys_triangle_t b = {{{-0.5f,0,-0.5f}, {1.5f,0,-0.5f}, {0.5f,0,1.5f}}};

    phys_contact_point_t contact;
    memset(&contact, 0, sizeof(contact));
    bool hit = phys_triangle_vs_triangle(&a, &b, 0.0f, &contact);

    /* Coplanar overlapping triangles should generate a contact. */
    ASSERT(hit);
    ASSERT(contact.penetration >= 0.0f);
    return 1;
}

/** Two separated triangles should not intersect. */
static int test_separated(void) {
    phys_triangle_t a = {{{-1,0,-1}, {1,0,-1}, {0,0,1}}};
    phys_triangle_t b = {{{-1,5,-1}, {1,5,-1}, {0,5,1}}};

    phys_contact_point_t contact;
    memset(&contact, 0, sizeof(contact));
    bool hit = phys_triangle_vs_triangle(&a, &b, 0.0f, &contact);

    ASSERT(!hit);
    return 1;
}

/** Two triangles crossing through each other (like an X). */
static int test_crossing(void) {
    /* Triangle A in XZ plane. */
    phys_triangle_t a = {{{-1,0,-1}, {1,0,-1}, {0,0,1}}};
    /* Triangle B in XY plane, intersecting A. */
    phys_triangle_t b = {{{-1,-1,0}, {1,-1,0}, {0,1,0}}};

    phys_contact_point_t contact;
    memset(&contact, 0, sizeof(contact));
    bool hit = phys_triangle_vs_triangle(&a, &b, 0.0f, &contact);

    ASSERT(hit);
    ASSERT(contact.penetration > 0.0f);
    return 1;
}

/** One triangle just touching the edge of another. */
static int test_edge_touching(void) {
    phys_triangle_t a = {{{0,0,0}, {1,0,0}, {0.5f,0,1}}};
    /* B shares edge with A at Y=0, but offset in Y slightly. */
    phys_triangle_t b = {{{0,0.01f,0}, {1,0.01f,0}, {0.5f,0.01f,-1}}};

    phys_contact_point_t contact;
    memset(&contact, 0, sizeof(contact));
    bool hit = phys_triangle_vs_triangle(&a, &b, 0.0f, &contact);

    /* Barely separated — should not hit. */
    ASSERT(!hit);
    return 1;
}

/** NULL inputs should not crash. */
static int test_null_safety(void) {
    phys_triangle_t a = {{{0,0,0}, {1,0,0}, {0,0,1}}};
    phys_contact_point_t contact;
    memset(&contact, 0, sizeof(contact));

    ASSERT(!phys_triangle_vs_triangle(NULL, &a, 0.0f, &contact));
    ASSERT(!phys_triangle_vs_triangle(&a, NULL, 0.0f, &contact));
    ASSERT(!phys_triangle_vs_triangle(&a, &a, 0.0f, NULL));
    return 1;
}

/** Speculative margin should detect near-miss. */
static int test_speculative_margin(void) {
    phys_triangle_t a = {{{-1,0,-1}, {1,0,-1}, {0,0,1}}};
    /* B is 0.05 above A. */
    phys_triangle_t b = {{{-1,0.05f,-1}, {1,0.05f,-1}, {0,0.05f,1}}};

    phys_contact_point_t contact;
    memset(&contact, 0, sizeof(contact));

    /* Without margin: should not hit. */
    ASSERT(!phys_triangle_vs_triangle(&a, &b, 0.0f, &contact));

    /* With margin of 0.1: should generate speculative contact. */
    ASSERT(phys_triangle_vs_triangle(&a, &b, 0.1f, &contact));
    /* Speculative contacts have negative penetration. */
    ASSERT(contact.penetration < 0.0f);
    return 1;
}

/* ---- Main ---- */

int main(void) {
    int fails = 0, total = 0;

    RUN(test_coplanar_overlap);
    RUN(test_separated);
    RUN(test_crossing);
    RUN(test_edge_touching);
    RUN(test_null_safety);
    RUN(test_speculative_margin);

    printf("\n%d / %d passed\n", total - fails, total);
    return fails ? 1 : 0;
}
