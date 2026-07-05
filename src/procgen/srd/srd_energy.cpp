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

} /* namespace srd */
