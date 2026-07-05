#include "ferrum/procgen/srd/srd_energy.h"

namespace srd {

symx::Scalar srd_room_dist_energy(symx::Workspace &ws,
                                  symx::Scalar cx, symx::Scalar cz,
                                  double target_cx, double target_cz) {
    using Scalar = symx::Scalar;
    Scalar tx = cx.make_constant(target_cx);
    Scalar tz = cx.make_constant(target_cz);
    return (cx - tx) * (cx - tx) + (cz - tz) * (cz - tz);
}

symx::Scalar srd_room_sdf_energy(symx::Workspace &ws,
                                 symx::Scalar cx, symx::Scalar cz,
                                 symx::Scalar hx, symx::Scalar hy, symx::Scalar hz,
                                 double temperature) {
    using Scalar = symx::Scalar;
    Scalar zero = cx.get_zero();
    Scalar one  = cx.get_one();
    Scalar temp = cx.make_constant(temperature);

    Scalar dx = symx::abs(cx - cx) - hx;
    Scalar dy = symx::abs(hy * hy.get_zero()) - hy;
    Scalar dz = symx::abs(cz - cz) - hz;

    Scalar outside = symx::max(dx, symx::max(dy, dz));
    outside = symx::max(outside, zero);

    Scalar inside = symx::min(dx, symx::min(dy, dz));
    inside = symx::min(inside, zero);

    Scalar sdf = outside + inside;
    Scalar occ = one / (one + symx::exp(sdf / temp));
    Scalar err = occ - one;
    return err * err;
}

symx::Scalar srd_corridor_sdf_energy(symx::Workspace &ws,
                                     symx::Scalar fx, symx::Scalar fz,
                                     symx::Scalar tx, symx::Scalar tz,
                                     symx::Scalar radius,
                                     symx::Scalar floor_y, symx::Scalar ceil_y,
                                     double temperature) {
    using Scalar = symx::Scalar;
    Scalar zero = fx.get_zero();
    Scalar one  = fx.get_one();
    Scalar temp = fx.make_constant(temperature);

    /* Sample at corridor midpoint */
    Scalar dx = tx - fx, dz = tz - fz;
    Scalar len2 = dx * dx + dz * dz;
    Scalar mid_t = fx.make_constant(0.5);
    Scalar cx = fx + mid_t * dx;
    Scalar cz = fz + mid_t * dz;

    /* Distance in XZ plane */
    Scalar dist_x = cx - cx;  /* = 0 (sample at centerline) */
    Scalar dist_z = cz - cz;  /* = 0 */
    Scalar d_2d = symx::sqrt(dist_x * dist_x + dist_z * dist_z) - radius;

    /* Y distance */
    Scalar mid_y = (floor_y + ceil_y) * fx.make_constant(0.5);
    Scalar d_y = symx::max(floor_y - mid_y, mid_y - ceil_y);

    /* SDF = max(d_2d, d_y) */
    Scalar sdf = symx::max(d_2d, d_y);

    Scalar occ = one / (one + symx::exp(sdf / temp));
    Scalar err = occ - one;
    return err * err;
}

} /* namespace srd */
