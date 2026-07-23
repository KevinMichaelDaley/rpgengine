"""Family A -- residential massing. A1: Dingbat Apartment (rpg-qdr5).

TOPOLOGY PLAN (the ticket carries the full ASCII diagrams; summary):

The building shell is ONE welded bmesh built on a GLOBAL LINE GRID: every
horizontal Z-level runs around all four walls, and every window jamb owns an
X (or Y) line, so wall grids weld at corners with plain 4-valence verts and
openings are grid CELLS -- never booleans.

  z-levels   0 grade | 0.9 sill1 | 2.1 head1 | 2.45 soffit | 2.75 slab top
             ... per extra floor: sill/head/slab ... | roof plane | parapet top
  front      fascia + upper floors + parapet only; ground = CARPORT VOID
             (boundary loop rings it; square posts are separate closed shells)
  soffit     horizontal grid at soffit level, x-lines shared with the facade,
             meets the recessed ground wall (unit doors) at shared verts
  windows    per-cell: 4 inset ring quads -> 4 jamb (return) quads -> 1 pane
             quad; every corner is 4-valence
  roof       parapet cap ring (1 quad wide) -> inner drop -> inset roof plane,
             all built from the same clipped line families so edges weld
  stair      switchback of separate closed shells (step boxes, stringers,
             landing, square rail posts + quad-strip handrail) -- no sawtooth
             end caps (those would be L-shaped ngons)

Validation: la.topology.validate() must report 100% quads, 0 ngons/tris,
0 non-manifold, 0 doubles, 0 T-junctions on every emitted mesh.
"""
import bpy

from . import elements as el2
from .. import params
from .. import topology
from ..geom import (
    _MATS, _Shell, _Wall, _box, _material, _sheared_box, _wall_L,
    _wall_solid, _window_lines,
    M_CONCRETE, M_GLASS, M_GYPSUM, M_METAL, M_PLYWOOD, M_RESIN,
    M_STUCCO, M_TRIM,
)

def build_dingbat(p, rng):
    """Build the dingbat per the module topology plan. Returns objects."""
    W, D = p["width"], p["depth"]
    floors = p["floors"]
    cols = p["window_cols"]
    # bay floor: cap the column count so bays never drop below 2.3 m.
    # Below that (a) windows go skinny enough that awnings and AC units
    # physically cannot fit, and (b) the rear door (b0+1.70..2.60) collides
    # with the kitchen-window jambs (left jamb = b0 + 1.2*bay < 2.6).
    cols = max(2, min(cols, int((p["width"] - 2.0) / 2.3)))
    if cols > 2 and cols % 2:
        cols -= 1                    # units are two bays: keep them paired
    cd = min(5.0, D * 0.45)          # carport depth
    margin = 1.0                     # end margins on the facade
    frame = 0.07

    # ---- z levels ----------------------------------------------------------
    z_grade, z_sill1, z_head1, z_soffit = 0.0, 0.9, 2.1, 2.45
    slab, spandrel, win_h, head = 0.30, 0.90, 1.20, 0.30
    zl = [z_grade, z_sill1, z_sill1 + 0.70, z_head1, z_soffit]
    z = z_soffit
    upper_rows = []                  # (sill, head) per upper floor
    floor_bands = []                 # (slab_lo, slab_hi, band_top) per floor
    for _f in range(max(1, floors - 1)):
        slab_lo = z
        z += slab                    # slab/fascia band top
        zl.append(z)
        s = z + spandrel
        h = s + win_h
        upper_rows.append((s, h))
        zl.extend([s, h])
        z = h + head
        zl.append(z)
        floor_bands.append((slab_lo, slab_lo + slab, z))
        zl.append(s + 0.70)              # high bath-window sill (0.5 tall)
    z_roof = z
    z_par = z + 0.4
    zl.extend([z_par])
    zl = sorted(set(zl))

    # ---- per-unit plan precompute (seeded BEFORE the line grid: the rear
    # door and the high bathroom window own x-lines). Dingbat baths sat at
    # the rear corner against the exterior wall with a small HIGH window;
    # the rear entry door shifts off-centre in its bay to make room. -------
    n_units = max(1, cols // 2)
    bayw0 = (W - 2.0 * margin) / cols
    unit_plan, unit_mirror, door_jambs, bath_wins = [], [], [], []
    for u in range(n_units):
        pl = rng.randrange(3)
        mir = (pl == 2)
        unit_plan.append(pl)
        unit_mirror.append(mir)
        b0 = margin + bayw0 * 2 * u          # door bay start
        b1 = b0 + bayw0                      # door bay end
        if not mir:
            door_jambs.append((b0 + 1.70, b0 + 1.70 + 0.90))
            bath_wins.append((b0 + 0.30, b0 + 1.20))   # 0.9 wide (wider than tall)
        else:
            door_jambs.append((b1 - 1.70 - 0.90, b1 - 1.70))
            bath_wins.append((b1 - 1.20, b1 - 0.30))
    # floor-plan-dependent fenestration: the living (door) bay gets the
    # wide 1.5 m sash, the second bay depends on the plan -- studios (P0)
    # keep it wide, one-beds (P1/P2) get a narrower 1.0 m bedroom window.
    # Awnings/AC gate on the ACTUAL width per window below.
    bay_widths = []
    for u in range(n_units):
        bay_widths += [1.5, 1.5 if unit_plan[u] == 0 else 1.0]
    bay_widths += [1.5] * (cols - len(bay_widths))

    # partial carport (rule-2 breaker done RIGHT: dingbats often carved only
    # part of the ground floor, with real support where the wall resumed).
    cb = max(1, min(cols, p["carport_bays"]))
    carport_end = W if cb >= cols else margin + bayw0 * cb
    # loggia: recessed balcony on the TOP floor's end bay; crops the concave
    # profile corner to convex cheeks, platform+railing ship separately.
    loggia = p["loggia"] if floors >= 2 else 'none'
    ld = min(1.6, cd - 0.3) if p["carport"] else 1.5   # loggia depth
    wt0 = 0.15
    lg_walkup = loggia != 'none' and p["loggia_door"] == 'walkup'
    if loggia == 'right':
        # walkup loggias are the WAY IN: they wrap the corner so a side
        # catwalk from the rear walkway can reach the platform. Sliding
        # (interior access) keeps both cheeks and the facade margin.
        lg0 = margin + bayw0 * (cols - 1)
        lg1 = W if lg_walkup else W - margin
    elif loggia == 'left':
        lg0 = 0.0 if lg_walkup else margin
        lg1 = margin + bayw0
    else:
        lg0 = lg1 = None
        lg_walkup = False
    if lg0 is not None:
        lcx = (lg0 + lg1) / 2.0
        lw = 1.6 if p["loggia_door"] == 'sliding' else 0.9
        loggia_door = (lcx - lw / 2.0, lcx + lw / 2.0)
    else:
        loggia_door = None

    t = 0.15                         # parapet ring width -- ALSO a wall line:
    # the cap ring's outer-edge verts at x=t / W-t must exist in the wall top
    # edges or they land mid-edge (T-junction, caught by the auditor).
    xl, xjambs = _window_lines(W, cols, margin, bay_widths)
    xl = sorted(set(xl) | {t, W - t} |
                {v for pr in door_jambs for v in pr} |
                {v for pr in bath_wins for v in pr} |
                ({carport_end, min(carport_end + wt0, W)} if cb < cols else set()) |
                (({lg0, lg1, max(lg0 - wt0, 0.0), min(lg1 + wt0, W)} |
                  set(loggia_door)) if lg0 is not None else set()))
    # optional small HIGH side windows (bath-sash proportions, wider than
    # tall, awning-opening) -- some dingbats punched a couple into each
    # gable end where the lot line allowed.
    side_wins = []
    if p["side_windows"] != 'none':
        for fc2 in (0.34, 0.66):
            yc2 = D * fc2
            side_wins.append((yc2 - 0.45, yc2 + 0.45))
    yl = sorted({0.0, cd, D, t, D - t} |
                ({wt0, ld, ld + wt0} if lg_walkup else set()) |
                {v for pr in side_wins for v in pr})

    shell = _Shell()

    has_carport = p["carport"]
    # ---- front wall (y=0, faces -Y). With a carport: fascia + upper floors
    # only (the ground is the void). Without one the building sits on grade:
    # full-height wall, ground window row, centre-bay entry door. ------------
    front_z = zl        # full height; the classifier voids the carport span

    def near(u0, vals):
        return any(abs(u0 - v) < 1e-6 for v in vals)

    def in_pairs(u0, pairs):
        # SPAN match, not jamb-line match: foreign grid lines (rear door /
        # bath jambs) crossing a window span otherwise shrank the window
        # to its first cell; fill() re-merges the run.
        return any(a - 1e-6 <= u0 < b - 1e-6 for (a, b) in pairs)
    door_x = [a for (a, _b) in xjambs[::2]]   # units are TWO bays wide: door
    # bay + window bay (one entry per apartment, not one per bay).

    top_hi = floor_bands[-1][1]
    lo_t = floor_bands[-1][0]        # the loggia floor DROPS to the slab
    # line: the recessed wall keeps a plinth row and cheap concrete steps
    # rise 0.30 m to the flush threshold ("walk up from the platform").
    top_head = upper_rows[-1][1]     # loggia recess stops at the HEAD line:
    # the head band stays as fascia over the recess (a ceiling coplanar with
    # the roof plane is both ahistorical and non-manifold).

    def front_classify(u0, zc0):
        # loggia recess: the top storey of its bay is VOID in the facade;
        # the strips one cheek-thickness wide either side keep the outer
        # skin but drop the inner skin (the room corner turns at the cheek).
        if lg0 is not None and lo_t - 1e-6 <= zc0 < top_head - 1e-6:
            if lg0 - 1e-6 <= u0 < lg1 - 1e-6:
                return 'void'
            if (max(lg0 - 0.15, 0.0) - 1e-6 <= u0 < lg1 + 0.15 - 1e-6):
                return 'void_in'
        # carport: only the carport span of the ground floor is void; the
        # rest is a real supported wall with ground windows (house look).
        if has_carport and zc0 < z_soffit - 1e-6:
            if u0 < carport_end - 1e-6:
                return 'void'
            if in_pairs(u0, xjambs) and z_sill1 - 1e-6 < zc0 < z_head1 - 1e-6:
                return 'window'
            return 'wall'
        for (sll, hh) in upper_rows:
            if sll - 1e-6 < zc0 < hh - 1e-6 and in_pairs(u0, xjambs):
                return 'window'
        if not has_carport:
            # grounded: windows only -- unit entries are on the back.
            if in_pairs(u0, xjambs) and z_sill1 - 1e-6 < zc0 < z_head1 - 1e-6:
                return 'window'
        return 'wall'

    shell.tag = 'facade_front'
    interior_on = p["mode"] == 'interior'
    wt = 0.15
    top_z_all = upper_rows[-1][1]      # rooms end at the top window head:
    # the roof plane / parapet drop / wall inner face otherwise meet 3 faces
    # to an edge at z_roof (genuine non-manifold).
    thick = wt if interior_on else 0.0
    _wf = _Wall(shell, (0, 0, 0), (1, 0, 0), xl, front_z, (0, -1, 0),
                M_STUCCO, thickness=thick, inner_zmax=top_z_all,
                inner_mat=M_GYPSUM)
    _wf.inner_u0, _wf.inner_u1 = wt, W - wt
    _wf.fill(front_classify, frame=frame, mat_frame=M_TRIM, mat_pane=M_GLASS)

    # ---- back wall (y=D, faces +Y): full height, windows on all floors -----
    back_z = zl

    spandrel_tops = [hi for (_lo, hi, _bt) in floor_bands]
    def in_door_span(u0):
        # RANGE match, not jamb-line match: foreign grid lines (window
        # jambs) landing inside a door span otherwise slice it into a
        # slender slot + wall sliver; fill() re-merges the run.
        return any(a - 1e-6 <= u0 < b - 1e-6 for (a, b) in door_jambs)
    kitchen_wins = xjambs[1::2]                   # window-bay sashes

    def back_classify(u0, zc0):
        # ground units enter from the BACK too ("appear like houses" -- no
        # doors visible from the street), same 0.9 m jambs as upstairs.
        if in_door_span(u0):
            if abs(zc0 - z_grade) < 1e-6:
                return 'doorL'
            if z_grade + 1e-6 < zc0 < z_head1 - 1e-6:
                return 'doorU'
        # ground row: kitchen window (window bay) + HIGH bath window.
        if z_sill1 - 1e-6 < zc0 < z_head1 - 1e-6 and in_pairs(u0, kitchen_wins):
            return 'window'
        if abs(zc0 - (z_sill1 + 0.70)) < 1e-6 and in_pairs(u0, bath_wins):
            return 'window_awning'
        # upper floors: walkway door (own off-centre jambs), kitchen window,
        # and the small high bathroom window beside the door.
        if in_door_span(u0):
            for fi2, (sll, hh) in enumerate(upper_rows):
                st2 = spandrel_tops[fi2]
                if abs(zc0 - st2) < 1e-6:
                    return 'doorL'
                if st2 + 1e-6 < zc0 < hh - 1e-6:
                    return 'doorU'
        if in_pairs(u0, kitchen_wins):
            for (sll, hh) in upper_rows:
                if sll - 1e-6 < zc0 < hh - 1e-6:
                    return 'window'
        if in_pairs(u0, bath_wins):
            for (sll, _hh) in upper_rows:
                if abs(zc0 - (sll + 0.70)) < 1e-6:
                    return 'window_awning'
        return 'wall'

    shell.tag = 'facade_back'
    _wb = _Wall(shell, (0, D, 0), (1, 0, 0), xl, back_z, (0, 1, 0),
                M_STUCCO, thickness=thick, inner_zmax=top_z_all,
                inner_mat=M_GYPSUM)
    _wb.inner_u0, _wb.inner_u1 = wt, W - wt
    _wb.fill(back_classify, frame=frame, mat_frame=M_TRIM, mat_pane=M_GLASS)

    # ---- side walls (x=0 faces -X; x=W faces +X): plain full-height grids --
    def plain(u0, zc0):
        del u0, zc0
        return 'wall'

    shell.tag = 'facade_side'
    open_sides = has_carport and not p["carport_sides"]

    lg_side_x = None
    if lg_walkup:
        lg_side_x = W if loggia == 'right' else 0.0

    def side_classify_at(sx):
        def classify(u0, zc0):
            # corner walkup loggia: the side wall opens over the recess so
            # the catwalk can step in; inner skin opens one cheek wider.
            if (lg_side_x is not None and abs(sx - lg_side_x) < 1e-6 and
                    lo_t - 1e-6 <= zc0 < top_head - 1e-6):
                if u0 < ld - 1e-6:
                    return 'void'
                if u0 < ld + wt - 1e-6:
                    return 'void_in'
            # monotony breaker: the ground-floor side over the carport opens
            # up (posts carry the building) -- a real dingbat variant.
            if open_sides and u0 < cd - 1e-6 and zc0 < z_soffit - 1e-6:
                return 'void'
            side_sel = p["side_windows"]
            this_side = 'left' if sx < W / 2 else 'right'
            if (side_wins and (side_sel == 'both' or side_sel == this_side)
                    and in_pairs(u0, side_wins)):
                for (sll2, _hh2) in upper_rows:
                    if abs(zc0 - (sll2 + 0.70)) < 1e-6:
                        return 'window_awning'
            return 'wall'
        return classify

    for sx, snrm in ((0.0, (-1, 0, 0)), (W, (1, 0, 0))):
        _ws = _Wall(shell, (sx, 0, 0), (0, 1, 0), yl, zl, snrm, M_STUCCO,
                    thickness=thick, inner_zmax=top_z_all, inner_mat=M_GYPSUM)
        _ws.inner_u0, _ws.inner_u1 = wt, D - wt
        _ws.fill(side_classify_at(sx))
    if thick > 0.0 and has_carport:
        # EDGE CLOSURE (carport mouth only -- in grounded mode the front
        # wall itself fills the corner): the side walls' outer+inner sheets
        # must never end as two disconnected faces at an exposed edge.
        # Split at the wall's z-rows so its row verts weld (they landed
        # mid-edge on a single tall quad). When the sides are open, also
        # close the vertical edge where the ground wall resumes at y = cd.
        shell.tag = 'facade_side'
        closure_rows = [v for v in zl if v <= z_soffit + 1e-6]
        for x0c, x1c in ((0.0, wt), (W - wt, W)):
            for ri in range(len(closure_rows) - 1):
                r0, r1 = closure_rows[ri], closure_rows[ri + 1]
                shell.quad((x0c, 0.0, r0), (x1c, 0.0, r0),
                           (x1c, 0.0, r1), (x0c, 0.0, r1), M_STUCCO)
                if open_sides:
                    shell.quad((x0c, cd, r0), (x1c, cd, r0),
                               (x1c, cd, r1), (x0c, cd, r1), M_STUCCO)
    shell.tag = 'parapet'

    # ---- carport liner: recessed ground wall + soffit. A SEPARATE SHELL --
    # welding its edges into the side/front wall faces would put 3 faces on
    # one edge (non-manifold, auditor-caught); a clean abutting shell is the
    # quality bar's sanctioned form.
    liner = _Shell() if has_carport else None
    ground_z = [v for v in zl if v <= z_soffit]

    def ground_classify(u0, zc0):
        # recessed carport wall: WINDOWS only -- entries are on the back
        # (doors were never visible from the street; house-like fronts).
        if in_pairs(u0, xjambs) and z_sill1 - 1e-6 < zc0 < z_head1 - 1e-6:
            return 'window'
        return 'wall'

    if has_carport:
        liner.tag = 'doors'
        cxl = [v for v in xl if v <= carport_end + 1e-6]
        _wg = _Wall(liner, (0, cd, 0), (1, 0, 0), cxl, ground_z, (0, -1, 0),
                    M_STUCCO, thickness=thick, inner_zmax=top_z_all,
                    inner_mat=M_GYPSUM)
        _wg.inner_u0, _wg.inner_u1 = wt, carport_end - (0.0 if cb >= cols else -wt)
        _wg.fill(ground_classify, frame=frame, mat_frame=M_TRIM, mat_pane=M_TRIM)
        liner.tag = 'carport'
        soffit_y = [v for v in yl if v <= cd]
        for iy in range(len(soffit_y) - 1):
            for iu in range(len(cxl) - 1):
                x0, x1 = cxl[iu], cxl[iu + 1]
                y0, y1 = soffit_y[iy], soffit_y[iy + 1]
                if (lg0 is not None and x0 >= lg0 - 1e-6 and
                        x1 <= lg1 + 1e-6 and y1 <= ld + 1e-6):
                    # the loggia's DROPPED slab replaces the structure here
                    # (its top is exactly coplanar with this plane -- the
                    # z-fighting on every walk-up loggia floor); its 12 cm
                    # reveal reads as the dropped slab it is.
                    continue
                # ceiling: faces DOWN into the carport (it rendered as a
                # backface from below).
                liner.quad((x0, y0, z_soffit), (x0, y1, z_soffit),
                           (x1, y1, z_soffit), (x1, y0, z_soffit), M_CONCRETE)
        if cb < cols:
            # SUPPORT where the carport ends: a return wall from the facade
            # to the recessed wall -- even dingbats didn't go that cheap.
            liner.tag = 'carport'
            ret_rows = [v for v in ground_z]
            ret_y = sorted({0.0, wt0, cd} |
                           {v for v in yl if v <= cd + 1e-6})  # split at
            # EVERY y-line up to the liner: the front wall's inner verts and
            # the soffit's row verts land mid-edge on a full-span quad.
            for ri in range(len(ret_rows) - 1):
                r0, r1 = ret_rows[ri], ret_rows[ri + 1]
                for yi in range(len(ret_y) - 1):
                    ya4, yb4 = ret_y[yi], ret_y[yi + 1]
                    liner.quad((carport_end, ya4, r0), (carport_end, yb4, r0),
                               (carport_end, yb4, r1), (carport_end, ya4, r1),
                               M_STUCCO)
                    if thick > 0.0:
                        xw = min(carport_end + wt0, W)
                        liner.quad((xw, yb4, r0), (xw, ya4, r0),
                                   (xw, ya4, r1), (xw, yb4, r1), M_GYPSUM)

    # ---- parapet cap + inner drop + roof plane -----------------------------
    rx = sorted({0.0, t, W - t, W} | set(v for v in xl if t < v < W - t))
    ry = sorted({0.0, t, D - t, D} | set(v for v in yl if t < v < D - t))

    def ring_cell(v0, v1, lo, hi):
        return v1 <= lo + 1e-6 or v0 >= hi - 1e-6

    shell.tag = 'parapet'
    for iy in range(len(ry) - 1):
        for ix in range(len(rx) - 1):
            x0, x1 = rx[ix], rx[ix + 1]
            y0, y1 = ry[iy], ry[iy + 1]
            on_ring = ring_cell(x0, x1, t, W - t) or ring_cell(y0, y1, t, D - t)
            zc = z_par if on_ring else z_roof
            m = M_TRIM if on_ring else M_CONCRETE
            shell.quad((x0, y0, zc), (x0, y1, zc), (x1, y1, zc), (x1, y0, zc), m)
    # inner drop: perimeter of the inset rect, z_par -> z_roof
    inner_x = [v for v in rx if t - 1e-6 <= v <= W - t + 1e-6]
    inner_y = [v for v in ry if t - 1e-6 <= v <= D - t + 1e-6]
    for i in range(len(inner_x) - 1):
        x0, x1 = inner_x[i], inner_x[i + 1]
        shell.quad((x0, t, z_par), (x1, t, z_par),
                   (x1, t, z_roof), (x0, t, z_roof), M_TRIM)
        shell.quad((x1, D - t, z_par), (x0, D - t, z_par),
                   (x0, D - t, z_roof), (x1, D - t, z_roof), M_TRIM)
    for i in range(len(inner_y) - 1):
        y0, y1 = inner_y[i], inner_y[i + 1]
        shell.quad((t, y1, z_par), (t, y0, z_par),
                   (t, y0, z_roof), (t, y1, z_roof), M_TRIM)
        shell.quad((W - t, y0, z_par), (W - t, y1, z_par),
                   (W - t, y1, z_roof), (W - t, y0, z_roof), M_TRIM)

    mats = [_material(n) for n in _MATS]
    # ---- loggia: cheeks + recessed thick wall (with door) + ceiling -------
    loggia_platform = None
    if lg0 is not None:
        shell.tag = 'loggia'
        l_rows = [v for v in zl if lo_t - 1e-6 <= v <= top_head + 1e-6]
        lgi0, lgi1 = max(lg0 - wt, 0.0), min(lg1 + wt, W)

        def lg_door_classify(u0, zc0):
            # bottom row [lo_t..top_hi] stays WALL everywhere: the plinth
            # under the door -- the steps climb it to the flush threshold.
            if loggia_door and \
                    loggia_door[0] - 1e-6 <= u0 < loggia_door[1] - 1e-6:
                if abs(zc0 - top_hi) < 1e-6:
                    return 'doorL'
                if top_hi + 1e-6 < zc0 < upper_rows[-1][1] - 1e-6:
                    return 'doorU'
            return 'wall'

        # cheeks: outer face into the loggia, inner face to the neighbour
        # room. A corner (walkup) loggia has ONE cheek -- the open corner
        # is carried by a slim post instead.
        cheek_y = [0.0, wt, ld]   # split at the inner-skin line: the front
        # wall's inner corner verts otherwise land mid-edge on the cheek.
        cheek_defs = ((lg0, lgi0, 1.0), (lg1, lgi1, -1.0))
        if lg_walkup:
            cheek_defs = (cheek_defs[0],) if loggia == 'right' \
                else (cheek_defs[1],)
        for (cx2, inner_x, out_sign) in cheek_defs:
            for ri in range(len(l_rows) - 1):
                r0, r1 = l_rows[ri], l_rows[ri + 1]
                for yi in range(len(cheek_y) - 1):
                    ya5, yb5 = cheek_y[yi], cheek_y[yi + 1]
                    pts = [(cx2, ya5, r0), (cx2, yb5, r0), (cx2, yb5, r1),
                           (cx2, ya5, r1)]
                    if out_sign < 0:
                        pts.reverse()
                    shell.quad(*pts, M_STUCCO)
                if interior_on:
                    pts = [(inner_x, wt, r0), (inner_x, ld + wt, r0),
                           (inner_x, ld + wt, r1), (inner_x, wt, r1)]
                    if out_sign > 0:
                        pts.reverse()
                    shell.quad(*pts, M_GYPSUM)
        # recessed wall segment (thick, with the loggia door cut through).
        lxl = [v for v in xl if lg0 - 1e-6 <= v <= lg1 + 1e-6]
        _wl = _Wall(shell, (0, ld, 0), (1, 0, 0), lxl, l_rows, (0, -1, 0),
                    M_STUCCO, thickness=thick, inner_zmax=top_z_all,
                    inner_mat=M_GYPSUM)
        if lg_walkup:
            # corner loggia: the recessed wall's inner face stops at the
            # side wall's inner skin (the _Wall corner-clip mechanism).
            if loggia == 'right':
                _wl.inner_u1 = W - wt
            else:
                _wl.inner_u0 = wt
        _wl.fill(lg_door_classify, frame=frame, mat_frame=M_TRIM)
        if interior_on:
            # inner-skin corner fillers (row-split so the wall's verts
            # weld) -- cheek sides only; the open corner clips instead.
            filler_defs = [(lgi0, lg0), (lg1, lgi1)]
            if lg_walkup:
                filler_defs = [filler_defs[0]] if loggia == 'right' \
                    else [filler_defs[1]]
            for (fx0, fx1) in filler_defs:
                for ri in range(len(l_rows) - 1):
                    r0, r1 = l_rows[ri], l_rows[ri + 1]
                    shell.quad((fx0, ld + wt, r0), (fx1, ld + wt, r0),
                               (fx1, ld + wt, r1), (fx0, ld + wt, r1),
                               M_GYPSUM)
        # loggia ceiling at the HEAD line (faces down), x-split on the grid
        # and y-split at the cheeks' inner-skin line so all corners weld.
        for iu in range(len(lxl) - 1):
            x0c, x1c = lxl[iu], lxl[iu + 1]
            for yi in range(len(cheek_y) - 1):
                ya6, yb6 = cheek_y[yi], cheek_y[yi + 1]
                shell.quad((x0c, ya6, top_head), (x0c, yb6, top_head),
                           (x1c, yb6, top_head), (x1c, ya6, top_head),
                           M_STUCCO)

        # ---- the separate loggia object: structural slab (always), door
        # steps, corner post + catwalk (walkup), and the OPTIONAL finished
        # platform -- deck + solid stucco railing. Some dingbats skipped
        # the platform entirely yet poured the steps anyway; loggia_platform
        # False reproduces exactly that.
        plat = _Shell(recalc=True)
        plat.tag = 'slabs'
        # dropped structural slab: its top IS the recess floor (bare and
        # walkable even with no platform).
        _box(plat, (lg0 + 0.003, 0.003, lo_t - 0.12),
             (lg1 - 0.003, ld - 0.003, lo_t), M_CONCRETE)
        has_deck = p["loggia_platform"]
        d0g, d1g = loggia_door
        if lg_walkup or not has_deck:
            # cheap concrete steps: slab -> flush threshold (2 x 0.15).
            plat.tag = 'steps'
            y_lo3, y_hi3 = ld - 0.64, ld - 0.002
            for sx3 in (d0g - 0.10, d1g + 0.01):
                _sheared_box(plat, sx3, sx3 + 0.09, y_lo3, y_hi3,
                             lo_t + 0.112, top_hi + 0.11, 0.11, M_CONCRETE)
            ix3, ix4 = d0g - 0.01 + 0.001, d1g + 0.01 - 0.001
            z3 = lo_t + 0.002
            rise3 = (top_hi - z3) / 2.0
            run3 = (y_hi3 - y_lo3) / 2.0
            for k3 in range(2):
                ya3, yb3 = y_lo3 + run3 * k3, y_lo3 + run3 * (k3 + 1)
                zl3, zh3 = z3 + rise3 * k3, z3 + rise3 * (k3 + 1)
                plat.quad((ix3, ya3, zl3), (ix4, ya3, zl3),
                          (ix4, ya3, zh3), (ix3, ya3, zh3), M_CONCRETE)
                plat.quad((ix3, ya3, zh3), (ix4, ya3, zh3),
                          (ix4, yb3, zh3), (ix3, yb3, zh3), M_CONCRETE)
        if lg_walkup:
            # slim corner post: the fascia over the open corner is CARRIED
            # ("they had to be supported somehow; even dingbats didn't go
            # that cheap").
            plat.tag = 'columns'
            pxc = W - 0.16 if loggia == 'right' else 0.06
            _box(plat, (pxc, 0.04, lo_t + 0.002),
                 (pxc + 0.10, 0.14, top_head - 0.002), M_METAL)
            # side catwalk: the WAY IN from the back -- rear walkway to the
            # loggia, cantilevered off the gable end on two thin posts.
            plat.tag = 'walkway'
            if loggia == 'right':
                cx0, cx1 = W + 0.004, W + 0.86
                rail_at = W + 0.74
                ret0, ret1 = W + 0.05, W + 0.74 - 0.004
            else:
                cx0, cx1 = -0.86, -0.004
                rail_at = -0.86
                ret0, ret1 = -0.74 + 0.004, -0.05
            ycat1 = (D + 1.25) if p["stair_side"] != loggia else (D - 0.004)
            _box(plat, (cx0, 0.004, top_hi - 0.12), (cx1, ycat1, top_hi),
                 M_CONCRETE)
            plat.tag = 'loggia'
            # picket railing: ONE mitred L path (return run + outer run
            # merged at the corner) instead of two solid stucco walls.
            corner_x = rail_at + 0.06
            inner_x = (ret0 if loggia == 'right' else ret1)
            el2.emit_railing_path(
                plat, [(inner_x, 0.11), (corner_x, 0.11),
                       (corner_x, ycat1 - 0.05)],
                z0=top_hi + 0.002, height=0.92, post_every=1.5)
            plat.tag = 'columns'
            pxm = (cx0 + cx1) / 2.0
            for pyc in (D * 0.35, D * 0.70):
                # embedded 20 mm into the catwalk slab underside (touching).
                _box(plat, (pxm - 0.05, pyc - 0.05, 0.0),
                     (pxm + 0.05, pyc + 0.05, top_hi - 0.10), M_METAL)
        if has_deck:
            plat.tag = 'loggia'
            dx0 = lg0 + 0.004
            dx1 = lg1 - 0.004
            if lg_walkup:
                # finished deck (0.05 screed) with a notch for the steps.
                zt3 = lo_t + 0.052
                _box(plat, (dx0, 0.152, lo_t + 0.002),
                     (dx1, ld - 0.646, zt3), M_CONCRETE)
                _box(plat, (dx0, ld - 0.644, lo_t + 0.002),
                     (d0g - 0.102, ld - 0.004, zt3), M_CONCRETE)
                _box(plat, (d1g + 0.102, ld - 0.644, lo_t + 0.002),
                     (dx1, ld - 0.004, zt3), M_CONCRETE)
                rail_top = lo_t + 0.952
            else:
                # sliding luxury: the deck fills the drop -- flush walkout.
                _box(plat, (dx0, 0.152, lo_t + 0.002),
                     (dx1, ld - 0.004, top_hi - 0.002), M_CONCRETE)
                rail_top = top_hi + 0.88
            rx0 = lg0 + 0.02 if not (lg_walkup and loggia == 'left') else 0.18
            rx1 = lg1 - 0.02 if not (lg_walkup and loggia == 'right') \
                else W - 0.18
            z0r = (lo_t if lg_walkup else top_hi) + 0.002
            el2.emit_railing_path(plat, [(rx0, 0.09), (rx1, 0.09)],
                                  z0=z0r, height=rail_top - z0r,
                                  post_every=1.4)
        loggia_platform = plat.to_object("LA_Dingbat_Loggia", mats)

    body = shell.to_object("LA_Dingbat_Body", mats)
    liner_ob = liner.to_object("LA_Dingbat_Carport", mats) if has_carport else None

    # ---- posts (separate closed shells) ------------------------------------
    post_ob = None
    if has_carport:
        posts = _Shell()
        posts.tag = 'carport'
        n_posts = max(2, cb + 1)
        px = 0.14
        span_hi = (carport_end - margin) if cb < cols else (W - margin)
        for i in range(n_posts):
            x = margin + (span_hi - margin) * (i / (n_posts - 1))
            x = min(max(x, px), W - px)
            # posts under the loggia's DROPPED slab stop at ITS underside
            # (embedded 20 mm) -- run to the soffit plane they'd spear the
            # slab and z-fight its top face.
            ptop = z_soffit
            if (lg0 is not None and x + px / 2 > lg0 + 1e-6 and
                    x - px / 2 < lg1 - 1e-6):
                # OVERLAP test, not containment: the boundary-straddling
                # post still touched the slab-top plane with its corner.
                ptop = lo_t - 0.10
            _box(posts, (x - px / 2, 0.30, 0.0),
                 (x + px / 2, 0.30 + px, ptop), M_METAL)
        post_ob = posts.to_object("LA_Dingbat_Posts", mats)

    # ---- rear stair tower: stringer-carried flights + posted landings (see
    # the ticket's rev-2 topology plan). Each flight: two SHEARED-BOX
    # stringers (planar 6-quad prisms) carrying a folded tread/riser strip,
    # underside closed by one sloped soffit quad -- the sawtooth profile that
    # would be an ngon end-cap terminates INSIDE the stringer faces, which is
    # what stringers are for. Landings are slab plates on four continuous
    # posts to grade. All separate clean shells. -----------------------------
    wd = 1.25                            # walkway depth (shared below)
    st_style = p.get("stair_style", 'tower')
    stair_ob = None
    stair_kit_obs = []
    if st_style == 'switchback':
        # STACKED kit switchbacks (fire-escape style): one unit per
        # storey in the walkway extension, each EXIT flight landing ON
        # that floor's walkway slab (never beside it).
        levels_k = [0.0] + [hi for (_lo, hi, _bt) in floor_bands]
        w9, gap9 = 1.0, 0.06
        ox9 = (W + 0.12) if p["stair_side"] == 'right' else (-2.28)
        for k9 in range(1, len(levels_k)):
            hgt9 = levels_k[k9] - levels_k[k9 - 1]
            if hgt9 < 1.2:
                continue
            n9 = max(6, int(round(hgt9 / 0.185)))
            n1_9 = (n9 + 1) // 2
            run1_9 = 0.27 * n1_9
            ytop9 = run1_9 + 0.02 - 0.27 * (n9 - n1_9)
            for ob9 in el2.build_switchback_stair(
                    dict(width=w9, height=hgt9, rail_height=0.95,
                         double_rail=True), rng):
                ob9.name = "LA_Dingbat_Stair"
                ob9.location = (ox9, D + wd + 0.03 - ytop9,
                                levels_k[k9 - 1])
                stair_kit_obs.append(ob9)
    if st_style == 'tower':
        stair = _Shell()
        stair.tag = 'steps'
        tread, s_w, st = 0.26, 1.1, 0.09          # tread run, lane width, stringer
        side = p["stair_side"]
        sdir = -1.0 if side == 'left' else 1.0
        laneB_x0 = (-s_w - 0.05) if side == 'left' else (W + 0.05)
        laneA_x0 = laneB_x0 + sdir * (s_w + 0.06)
        wd = 1.25
        levels = [0.0] + [hi for (_lo, hi, _bt) in floor_bands]   # = floor lines
        # (walkway tops sit here too: doors flush, no threshold step)
        y_arr = D + wd + 0.02     # flights meet the walkway at its outer EDGE:
        # starting/arriving inside the band collided with the slab (user-hit:
        # "joined at the wrong places except the bottom one").

        def _stringer(x0s, x1s, y_base, z_base, y_top, z_top, foot_lo, foot_hi):
            """One footed stringer solid from base (low z) to top (high z),
            either y direction. The bottom is CLIPPED flat at each end --
            z_base - foot_lo and z_top - foot_hi -- instead of running the
            constant 0.34 m depth past the ends (the old 'tooth' hanging
            0.28 m under every platform, and underground at grade). All
            parametrics run base->top so the rise is ALWAYS positive (a y-
            sorted version inverted the kinks on descending flights and grew
            the solid past both ends). Hexagon profile: 3 top / 3 bottom /
            3-per-side / 2 end quads, all planar; winding flips with y."""
            # 0.22 m channel depth: at stair slope the end clip (rising from
            # full depth to the platform underside) spans depth/slope -- the
            # old 0.34 m made a 24 cm sliver taper that read as 'super
            # compressed' at every flight top; 0.22 m clips in ~8 cm.
            cover, depth = 0.06, 0.22
            rise_t = z_top - z_base
            t1 = min(0.45, (depth - cover - foot_lo) / rise_t)
            t2 = max(0.55, 1.0 - (depth - cover - foot_hi) / rise_t)
            ys4 = [y_base + (y_top - y_base) * t for t in (0.0, t1, t2, 1.0)]
            ts4 = [z_base + rise_t * t + cover for t in (0.0, t1, t2, 1.0)]
            bs4 = [z_base - foot_lo, z_base - foot_lo,
                   z_top - foot_hi, z_top - foot_hi]
            flip = y_top < y_base

            def q(a, b, c, d):
                if flip:
                    stair.quad(a, d, c, b, M_METAL)
                else:
                    stair.quad(a, b, c, d, M_METAL)

            for k4 in range(3):
                ya4, yb4 = ys4[k4], ys4[k4 + 1]
                ta4, tb4 = ts4[k4], ts4[k4 + 1]
                ba4, bb4 = bs4[k4], bs4[k4 + 1]
                q((x0s, ya4, ta4), (x1s, ya4, ta4), (x1s, yb4, tb4),
                  (x0s, yb4, tb4))                                # top (+z)
                q((x0s, ya4, ba4), (x0s, yb4, bb4), (x1s, yb4, bb4),
                  (x1s, ya4, ba4))                                # bottom (-z)
                q((x0s, ya4, ba4), (x0s, ya4, ta4), (x0s, yb4, tb4),
                  (x0s, yb4, bb4))                                # -x side
                q((x1s, ya4, ta4), (x1s, ya4, ba4), (x1s, yb4, bb4),
                  (x1s, yb4, tb4))                                # +x side
            q((x0s, ys4[0], ts4[0]), (x0s, ys4[0], bs4[0]),
              (x1s, ys4[0], bs4[0]), (x1s, ys4[0], ts4[0]))            # base (-y)
            q((x0s, ys4[3], bs4[3]), (x0s, ys4[3], ts4[3]),
              (x1s, ys4[3], ts4[3]), (x1s, ys4[3], bs4[3]))            # top (+y)

        def flight(x0, y_from, y_to, z_from, z_to, foot_base, foot_top):
            """One flight between two levels along y (either direction).
            foot_base/foot_top: how far below its start/arrival level the
            stringer may reach (0 at grade, slab/plate thickness elsewhere)."""
            x1 = x0 + s_w
            n = max(3, int(round(abs(z_to - z_from) / 0.185)))
            rise = (z_to - z_from) / n
            run = (y_to - y_from) / n
            for sx0, sx1 in ((x0, x0 + st), (x1 - st, x1)):
                _stringer(sx0, sx1, y_from, z_from, y_to, z_to,
                          foot_base, foot_top)
            # tread/riser strip between the stringers, inset 1 mm: its boundary
            # hides inside the joint but never lands on a stringer edge (the
            # auditor's 16 T-junctions per flight when coincident).
            # 6 mm channel reveal between strip and stringers (1 mm put the
            # ground flight's first riser corners visually ON the stringer
            # feet -- the same 'merged vertices' read as the old soffit).
            ix0, ix1 = x0 + st + 0.006, x1 - st - 0.006
            dflip = y_to < y_from

            def qf(a, b, c, d):
                # riser/tread/soffit winding must track travel direction: the
                # fixed order inverted every descending flight's faces (the
                # 'decimated' look under backface culling).
                if dflip:
                    stair.quad(a, d, c, b, M_CONCRETE)
                else:
                    stair.quad(a, b, c, d, M_CONCRETE)

            for k2 in range(n):
                ya2, yb2 = y_from + run * k2, y_from + run * (k2 + 1)
                zlo2, zhi2 = z_from + rise * k2, z_from + rise * (k2 + 1)
                qf((ix0, ya2, zlo2), (ix1, ya2, zlo2),
                   (ix1, ya2, zhi2), (ix0, ya2, zhi2))                      # riser
                qf((ix0, ya2, zhi2), (ix1, ya2, zhi2),
                   (ix1, yb2, zhi2), (ix0, yb2, zhi2))                      # tread
            # soffit: a RECESSED panel between the stringers -- 6 mm in from
            # their faces, 20 mm above the footed bottom line. A 1 mm shadow
            # copy of the stringer profile put its corners 1.4-3 mm from the
            # stringer corners at every kink/end: in wireframe that read as
            # doubled 'merged' vertices along the rails (user-hit).
            cover, depth = 0.06, 0.22
            rise_t = z_to - z_from
            t1 = min(0.45, (depth - cover - foot_base) / rise_t)
            t2 = max(0.55, 1.0 - (depth - cover - foot_top) / rise_t)
            ydir = 1.0 if y_to > y_from else -1.0
            sx0f, sx1f = x0 + st + 0.006, x1 - st - 0.006
            ysf = [y_from + (y_to - y_from) * t for t in (0.0, t1, t2, 1.0)]
            ysf[0] += 0.006 * ydir
            ysf[3] -= 0.006 * ydir
            bsf = [z_from - foot_base + 0.02, z_from - foot_base + 0.02,
                   z_to - foot_top + 0.02, z_to - foot_top + 0.02]
            for k4 in range(3):
                qf((sx0f, ysf[k4], bsf[k4]), (sx0f, ysf[k4 + 1], bsf[k4 + 1]),
                   (sx1f, ysf[k4 + 1], bsf[k4 + 1]), (sx1f, ysf[k4], bsf[k4]))

        land_y0 = None
        for i in range(len(levels) - 1):
            zb, zt = levels[i], levels[i + 1]
            zh = (zb + zt) / 2.0
            n_half = max(3, int(round((zh - zb) / 0.185)))
            run = tread * n_half
            land_y0 = y_arr + run + 0.003
            # flight A (outer lane): away from the building, base at the
            # walkway edge (grade for the first), TOP at the plate's near edge.
            flight(laneA_x0, y_arr, land_y0 - 0.006, zb, zh,
                   0.0 if i == 0 else 0.12, 0.10)
            # half landing plate.
            lx0 = min(laneA_x0, laneB_x0)
            lx1 = max(laneA_x0, laneB_x0) + s_w
            _box(stair, (lx0, land_y0, zh - 0.10), (lx1, land_y0 + s_w, zh),
                 M_CONCRETE)
            # flight B (inner lane): BASE AT THE SAME (near) EDGE of the plate
            # -- a true switchback: top out beside it, turn around on the
            # plate, and board the upper flight from where you stand. (Basing
            # it at the FAR edge put every upper flight's first riser across
            # the plate from the arriving walker: consistently un-ascendable.)
            flight(laneB_x0, land_y0 - 0.006, y_arr, zh, zt, 0.10, 0.12)

        # four continuous posts carry the stacked half-landings, grade -> top.
        # UNDER the plate corners and embedded 20 mm into the underside --
        # posts standing outside the corners with air gaps held up nothing
        # (user-hit: "they dont actually touch the things they are holding up").
        stair.tag = 'columns'
        top_land = (levels[-2] + levels[-1]) / 2.0 if len(levels) > 1 else levels[-1]
        lx0 = min(laneA_x0, laneB_x0)
        lx1 = max(laneA_x0, laneB_x0) + s_w
        for (px2, py2) in ((lx0 + 0.01, land_y0 + 0.01), (lx1 - 0.09, land_y0 + 0.01),
                           (lx0 + 0.01, land_y0 + s_w - 0.09),
                           (lx1 - 0.09, land_y0 + s_w - 0.09)):
            _box(stair, (px2, py2, 0.0), (px2 + 0.08, py2 + 0.08, top_land - 0.08),
                 M_METAL)
        # walkway-extension posts: carry the arrival edge over the lanes --
        # under the extension slab, embedded 20 mm into its underside.
        wpx = (-2 * s_w - 0.10 + 0.02) if side == 'left' else (W + 2 * s_w + 0.02)
        for wy in (D + 0.05, D + wd - 0.13):
            _box(stair, (wpx, wy, 0.0), (wpx + 0.08, wy + 0.08,
                 floor_bands[-1][1] - 0.10), M_METAL)
        stair_ob = stair.to_object("LA_Dingbat_Stair", mats)

    # ---- rear walkway serving the upper units: slab + square posts. Built
    # in BOTH modes -- it is exterior circulation, and the stair tower
    # (always built) arrives at it: facade-mode buildings otherwise had
    # stairs climbing to platforms that did not exist. --------------------
    walk = _Shell()
    walk.tag = 'walkway'
    s_w2 = 1.1
    wx0 = (-2 * s_w2 - 0.10) if p["stair_side"] == 'left' else 0.0
    wx1 = W if p["stair_side"] == 'left' else (W + 2 * s_w2 + 0.10)
    for wi, (_lo3, hi3, _bt3) in enumerate(floor_bands):
        # top FLUSH with the unit floor line (= slab band top = door
        # bottom): threshold steps were expensive, nobody poured them.
        _box(walk, (wx0, D + 0.003 + 0.002 * wi, hi3 - 0.12),
             (wx1 + 0.001 * wi, D + wd, hi3), M_CONCRETE)
    walk.tag = 'columns'
    for i in range(3):
        x = 0.2 + (W - 0.4) * (i / 2.0)
        _box(walk, (x - 0.06, D + wd - 0.14, 0.0),
             (x + 0.06, D + wd - 0.02, floor_bands[-1][1] - 0.10), M_METAL)
    # BALCONY RAILINGS (rpg-caur): per upper floor, a mitred picket path
    # along the walkway's outer edge with a return at the far (non-stair)
    # end. The stair zone stays open: the tower's flights arrive at the
    # outer edge; the kit switchback exits through a gap in its span.
    y_out = D + wd - 0.062
    top_hi3 = floor_bands[-1][1]
    for (_lo3, hi3, _bt3) in floor_bands:
        if hi3 < 1.0:
            continue                          # grade level: no rail
        # switchback gaps: the TOP floor only receives the exit flight
        # (narrow gap); every intermediate floor ALSO boards the next
        # unit's entry flight, so the gap spans the WHOLE stair unit --
        # a railing across the entry blocked the stair (user-hit).
        top9 = abs(hi3 - top_hi3) < 1e-6
        if p["stair_side"] == 'right':
            far_x, ext0 = wx0 + 0.05, W - 0.02
            segs9 = [('far', far_x, ext0)]
            if st_style == 'switchback':
                gapa = (W + 1.10) if top9 else (W + 0.06)
                gapb = W + 2.24
                segs9 = [('far', far_x, gapa), ('run', gapb, wx1 - 0.05)]
        else:
            far_x, ext0 = wx1 - 0.05, 0.02
            segs9 = [('far', far_x, ext0)]
            if st_style == 'switchback':
                gapb = (-1.28) if top9 else (-0.06 - 2.18)
                gapa = -0.14
                segs9 = [('far', far_x, gapa), ('run', wx0 + 0.05, gapb)]
        for (kind9, a9, b9) in segs9:
            lo9, hi9 = min(a9, b9), max(a9, b9)
            if hi9 - lo9 < 0.3:
                continue
            if kind9 == 'far':
                pts9 = [(far_x, D + 0.06), (far_x, y_out),
                        (b9 if far_x == a9 else a9, y_out)]
            else:
                pts9 = [(lo9, y_out), (hi9, y_out)]
            el2.emit_railing_path(walk, pts9, z0=hi3, height=0.92,
                                  post_every=1.5, tag='loggia')
    walkway_ob = walk.to_object("LA_Dingbat_Walkway", mats)

    # ---- INTERIOR MODE (rule 1): inner wall liners, slabs, partitions,
    # rear walkway -- everything structural, just-built, walkable. Each piece
    # is a clean separate shell; inner liners mirror the exterior openings as
    # VOID cells so the existing jamb returns read as reveals from inside. ---
    interior_obs = []
    if p["mode"] == 'interior':
        # exterior walls already carry their inner faces + cut-through
        # openings (thick walls, one mesh); only the free-standing interior
        # structure is added here. Geometry references:
        ix0, ix1 = wt, W - wt
        iy1 = D - wt
        top_z = top_z_all

        # slabs: one closed plate per storey (top face = floor, underside =
        # ceiling below). Inset 1 mm from the liner so shells never share
        # planes. Ground slab only without a carport (else the soffit is it).
        slabs = _Shell()
        slabs.tag = 'slabs'
        e = 0.001
        gy0 = (cd + wt) if has_carport else wt
        slab_boxes = [(ix0, ix1, gy0, iy1, z_grade, z_grade + 0.12)]
        for bi, (lo, hi, _bt) in enumerate(floor_bands):
            if lg0 is not None and bi == len(floor_bands) - 1:
                # top band: clipped AROUND the loggia recess -- its dropped
                # slab ships with the loggia object, and an unclipped plate
                # here would poke 0.30 m up into the recess mouth.
                ycl = ld + wt + 0.002
                slab_boxes.append((ix0, ix1, ycl, iy1, lo, hi))
                lgc0, lgc1 = max(lg0 - wt, 0.0), min(lg1 + wt, W)
                if lgc0 - 0.002 > ix0 + 0.01:
                    slab_boxes.append((ix0, lgc0 - 0.002, wt, ycl, lo, hi))
                if lgc1 + 0.002 < ix1 - 0.01:
                    slab_boxes.append((lgc1 + 0.002, ix1, wt, ycl, lo, hi))
            else:
                slab_boxes.append((ix0, ix1, wt, iy1, lo, hi))
        # top-storey CEILING slab (the roof structure underside).
        slab_boxes.append((ix0, ix1, wt, iy1, top_z, top_z + 0.12))
        for (x0s, x1s, y0s, y1s, z_lo, z_hi) in slab_boxes:
            _box(slabs, (x0s + e, y0s + e, z_lo), (x1s - e, y1s - e, z_hi),
                 M_CONCRETE)
        interior_obs.append(slabs.to_object("LA_Dingbat_Slabs", mats))

        # unit partitions: one wall between window bays per storey, spanning
        # liner to liner. Thin closed boxes; carried on both floors so they
        # read load-bearing. 2 mm shy of the liners (no shared planes).
        parts = _Shell(recalc=True)
        parts.tag = 'partitions'
        pt = 0.10
        # one storey per floor band: that band's slab top to the next band's
        # slab bottom (roof structure underside for the last).
        storeys = [(z_grade + 0.12, floor_bands[0][0],
                    ((cd + wt) if has_carport else wt) + 0.002)]
        for i2, (lo, hi, _bt) in enumerate(floor_bands):
            nxt = floor_bands[i2 + 1][0] if i2 + 1 < len(floor_bands) else top_z
            storeys.append((hi, nxt, wt + 0.002))
        for k in range(2, cols, 2):   # two-bay units -> every other boundary
            xb = margin + (W - 2 * margin) * (k / cols)
            for (zlo, zhi, ys) in storeys:
                _box(parts, (xb - pt / 2, ys, zlo),
                     (xb + pt / 2, iy1 - 0.002, zhi - 0.001), M_GYPSUM)

        # unit floor plans: dingbats rotated a few STANDARD plans -- none
        # were a bare square with no bathroom. P0 studio, P1 one-bed, P2
        # mirrored one-bed, seeded per unit. RULES OF THE WALLS:
        #  - interior doorways are FRAMED: wall / opening (0.76) / wall,
        #    with a HEADER box above the 2.03 m head line -- never a
        #    floor-to-ceiling gap between stubs;
        #  - the bathroom is an interior ISLAND against the party wall at
        #    mid-depth, so its walls can never intersect facade windows
        #    (the front-corner placement clipped straight through them).
        DOOR_W, DOOR_H = 0.78, 2.03

        def wall_x(x, ya2, yb2, zlo, zhi, door=None):
            _wall_solid(parts, 'y', x - 0.045, ya2, yb2, zlo, zhi, 0.09,
                        door, M_GYPSUM)

        def wall_y(y, xa2, xb2, zlo, zhi, door=None):
            _wall_solid(parts, 'x', y - 0.045, xa2, xb2, zlo, zhi, 0.09,
                        door, M_GYPSUM)

        def doored_wall_y(y, xa2, xb2, door_at, zlo, zhi):
            g0 = max(xa2 + 0.05, min(door_at, xb2 - DOOR_W - 0.05))
            wall_y(y, xa2, xb2, zlo, zhi,
                   door=(g0, g0 + DOOR_W, zlo + DOOR_H))

        def doored_wall_x(x, ya2, yb2, door_at, zlo, zhi):
            g0 = max(ya2 + 0.05, min(door_at, yb2 - DOOR_W - 0.05))
            wall_x(x, ya2, yb2, zlo, zhi,
                   door=(g0, g0 + DOOR_W, zlo + DOOR_H))

        for u in range(n_units):
            ux0 = margin + bayw0 * 2 * u + 0.06
            ux1 = margin + bayw0 * 2 * (u + 1) - 0.06
            if ux1 - ux0 < 2.6:
                continue
            plan = unit_plan[u]
            mir = unit_mirror[u]
            dj = door_jambs[u]
            # bath spans party wall -> just past the door-side jamb.
            bx_in = (dj[0] - 0.06) if not mir else (dj[1] + 0.06)
            for si, (zlo, zhi, ys) in enumerate(storeys):
                y0u = ys + 0.05
                y1u = iy1 - 0.05
                depth_u = y1u - y0u
                if depth_u < 3.4:
                    continue
                zl2, zh2 = zlo + 0.001, zhi - 0.002
                by0 = y1u - 2.25                   # bath front wall line
                # rear-corner bathroom: side wall + doored front wall; the
                # building's rear wall (with its high window) closes it.
                keep_tag = parts.tag
                parts.tag = 'partitions'
                if not mir:
                    g0d = max(ux0 + 0.06, bx_in - DOOR_W - 0.10)
                    _wall_L(parts, bx_in, by0, y1u - 0.002, ux0 + 0.001,
                            zl2, zh2, 0.09, (g0d, g0d + DOOR_W, zl2 + DOOR_H),
                            M_GYPSUM)
                else:
                    g0d = min(ux1 - 0.06 - DOOR_W, bx_in + 0.10)
                    _wall_L(parts, bx_in, by0, y1u - 0.002, ux1 - 0.001,
                            zl2, zh2, 0.09, (g0d, g0d + DOOR_W, zl2 + DOOR_H),
                            M_GYPSUM)
                parts.tag = keep_tag
                # kitchen stub on the window-bay side, ahead of its window.
                kx0 = ux1 - 2.0 if not mir else ux0
                kx1 = ux1 if not mir else ux0 + 2.0
                wall_y(y1u - 2.3, kx0, kx1, zl2, zh2)
                if plan >= 1:
                    # one-bed: bedroom wall mid-depth, framed doorway away
                    # from the bathroom side.
                    bw_y = y0u + depth_u * 0.45
                    door_at = (ux1 - DOOR_W - 0.12) if not mir                         else (ux0 + 0.12)
                    doored_wall_y(bw_y, ux0 + 0.001, ux1 - 0.001,
                                  door_at, zl2, zh2)

        interior_obs.append(parts.to_object("LA_Dingbat_Partitions", mats))

    # ---- STORY OPTIONS (rule 3, off by default; theme: abandonment /
    # regime / resistance) ---------------------------------------------------
    story = _Shell()
    story.tag = 'story'
    if p["all_broken"]:
        # shattered panes: 2 quad shards per window + plywood behind some.
        for (a2, b2) in xjambs:
            for (sll, hh) in upper_rows:
                cx, cz = (a2 + b2) / 2.0, (sll + hh) / 2.0
                for k in range(2):
                    ang = rng.uniform(-0.6, 0.6)
                    dx = 0.28 * (1 if k == 0 else -1)
                    story.quad((cx + dx - 0.18, 0.06, cz - 0.22 + ang * 0.1),
                               (cx + dx + 0.10, 0.06, cz - 0.30 - ang * 0.1),
                               (cx + dx + 0.16, 0.065, cz + 0.24 + ang * 0.1),
                               (cx + dx - 0.12, 0.065, cz + 0.30 - ang * 0.1),
                               M_GLASS)
                if rng.random() < 0.45:
                    _box(story, (a2 + 0.02, 0.10, sll + 0.02),
                         (b2 - 0.02, 0.14, hh - 0.02), M_PLYWOOD)
    if p["sealed_unit"] and cols >= 2:
        # one unit's openings swallowed in aberration resin: a bulged panel
        # (3x3 quad grid, centre pushed proud) over the bay, floor to head.
        k = rng.randrange(cols)
        a2, b2 = xjambs[k]
        x0, x1 = a2 - 0.25, b2 + 0.25
        zlo = z_soffit + slab - 0.1 if has_carport else z_grade
        zhi = upper_rows[0][1] + 0.25
        xs = [x0, x0 + (x1 - x0) / 3, x0 + 2 * (x1 - x0) / 3, x1]
        zs = [zlo, zlo + (zhi - zlo) / 3, zlo + 2 * (zhi - zlo) / 3, zhi]
        for iz2 in range(3):
            for ix2 in range(3):
                bulge = 0.22 if (ix2 == 1 and iz2 == 1) else 0.10
                def co(xx, zz, bb):
                    return (xx, -bb, zz)
                c00 = co(xs[ix2], zs[iz2], 0.10 if (ix2 in (0,) or iz2 in (0,)) else bulge)
                c10 = co(xs[ix2+1], zs[iz2], 0.10 if (ix2+1 in (3,) or iz2 in (0,)) else bulge)
                c11 = co(xs[ix2+1], zs[iz2+1], 0.10 if (ix2+1 in (3,) or iz2+1 in (3,)) else bulge)
                c01 = co(xs[ix2], zs[iz2+1], 0.10 if (ix2 in (0,) or iz2+1 in (3,)) else bulge)
                story.quad(c00, c10, c11, c01, M_RESIN)
    if p["rooftop_roost"]:
        # pigeon-loft signal post: shed + cage posts + perch, rear roof corner.
        rx0, ry0 = W - 3.2, D - 2.6
        _box(story, (rx0, ry0, z_roof + 0.001), (rx0 + 1.6, ry0 + 1.2, z_roof + 1.1),
             M_PLYWOOD)
        story.quad((rx0 - 0.05, ry0 - 0.05, z_roof + 1.35),
                   (rx0 + 1.65, ry0 - 0.05, z_roof + 1.25),
                   (rx0 + 1.65, ry0 + 1.25, z_roof + 1.25),
                   (rx0 - 0.05, ry0 + 1.25, z_roof + 1.35), M_METAL)
        for (px2, py2) in ((rx0 - 0.02, ry0 - 0.02), (rx0 + 1.58, ry0 - 0.02),
                           (rx0 - 0.02, ry0 + 1.18), (rx0 + 1.58, ry0 + 1.18)):
            _box(story, (px2, py2, z_roof + 0.001), (px2 + 0.05, py2 + 0.05,
                 z_roof + 1.3), M_METAL)
        _box(story, (rx0 + 2.2, ry0 + 0.4, z_roof + 0.001),
             (rx0 + 2.26, ry0 + 0.46, z_roof + 2.1), M_METAL)
        _box(story, (rx0 + 1.85, ry0 + 0.40, z_roof + 1.95),
             (rx0 + 2.65, ry0 + 0.46, z_roof + 2.01), M_METAL)
    story_ob = story.to_object("LA_Dingbat_Story", mats)         if story._face_tags or story.bm.faces else None

    # ---- awnings + AC units -------------------------------------------------
    extras = _Shell()
    extras.tag = 'awnings'
    if p["awnings"]:
        for (a, b) in xjambs:
            if b - a < 1.30 - 1e-6:
                continue      # awnings were EXPENSIVE: wide windows only
            for (s, h) in upper_rows:
                if lg0 is not None and lg0 - 1e-6 <= a and b <= lg1 + 1e-6 \
                        and abs(s - upper_rows[-1][0]) < 1e-6:
                    continue  # loggia bay: no facade window there
                zh = h + 0.05
                extras.quad((a - 0.1, 0.0, zh), (b + 0.1, 0.0, zh),
                            (b + 0.1, -0.45, zh - 0.28),
                            (a - 0.1, -0.45, zh - 0.28), M_METAL)
                extras.quad((b + 0.1, -0.45, zh - 0.28),
                            (a - 0.1, -0.45, zh - 0.28),
                            (a - 0.1, -0.45, zh - 0.40),
                            (b + 0.1, -0.45, zh - 0.40), M_METAL)
    extras.tag = 'ac_units'
    # per-UNIT: the AC goes in the LIVING-ROOM window (the unit's door bay).
    # The bathroom is a windowless island, and this placement cannot even
    # express "AC in the bathroom" -- the unit's single AC serves the unit.
    for u2 in range(max(1, cols // 2)):
        bay = min(2 * u2, len(xjambs) - 1)
        (a, b) = xjambs[bay]
        if b - a < 0.78:
            continue          # sash too narrow for any AC chassis to fit
        for (sll, hh) in upper_rows:
            if lg0 is not None and lg0 - 1e-6 <= a and b <= lg1 + 1e-6 \
                    and abs(sll - upper_rows[-1][0]) < 1e-6:
                continue      # loggia bay: no facade window there
            if rng.random() < p["ac_units"]:
                cx = (a + b) / 2.0
                ac_hw = min(0.30, (b - a) / 2.0 - 0.09)
                ac_h = min(0.40, (hh - sll) - 0.25)
                _box(extras, (cx - ac_hw, -0.28, sll + 0.02),
                     (cx + ac_hw, 0.10, sll + 0.02 + ac_h), M_METAL)
    extras_ob = extras.to_object("LA_Dingbat_Extras", mats)

    # ---- engine tags --------------------------------------------------------
    out = [body, liner_ob, post_ob, stair_ob, walkway_ob, extras_ob, story_ob,
           loggia_platform] + interior_obs + stair_kit_obs
    out = [ob for ob in out if ob is not None]
    for ob in out:
        ob["ferrum_lightmap_res"] = 0 if ob in (extras_ob, story_ob) else 128
    return out


SPEC = [
    params.MODE_PARAM,
    dict(name="width", type='FLOAT', default=14.0, min=8.0, max=22.0,
         unit='LENGTH', desc="Building width"),
    dict(name="depth", type='FLOAT', default=9.0, min=6.0, max=14.0,
         unit='LENGTH', desc="Building depth"),
    dict(name="floors", type='INT', default=2, min=1, max=3),
    dict(name="carport_bays", type='INT', default=4, min=0, max=6),
    dict(name="window_cols", type='INT', default=4, min=2, max=8),
    dict(name="awnings", type='BOOL', default=True),
    dict(name="ac_units", type='FLOAT', default=0.4, min=0.0, max=1.0,
         desc="Window AC density"),
    dict(name="stair_style", type='ENUM', default='tower',
         items=('tower', 'switchback'),
         desc="Rear stair: A1 stringer tower or stacked kit switchbacks"),
    dict(name="stair_side", type='ENUM', default='left',
         items=('left', 'right')),
    dict(name="facade_style", type='ENUM', default='plain',
         items=('plain', 'starburst', 'mansard', 'tiki', 'script')),
    # monotony breaker (rule 2): no carport => the building sits on grade
    # with a ground window row + entry door.
    dict(name="carport", type='BOOL', default=True,
         desc="Tuck-under carport; off = building drops to grade"),
    dict(name="carport_sides", type='BOOL', default=True,
         desc="Walled carport sides; off = open sides on posts"),
    dict(name="loggia", type='ENUM', default='none',
         items=('none', 'right', 'left'),
         desc="Recessed top-floor loggia (platform + solid railing)"),
    dict(name="loggia_door", type='ENUM', default='walkup',
         items=('walkup', 'sliding'),
         desc="Loggia access: narrow walk-up door or wide sliding reveal"),
    dict(name="loggia_platform", type='BOOL', default=True,
         desc="Finished deck + solid railing; off = bare slab, but the "
              "steps get built anyway (they always were)"),
    dict(name="side_windows", type='ENUM', default='none',
         items=('none', 'left', 'right', 'both'),
         desc="Small high awning sashes on the gable ends (two per upper "
              "floor), per side"),
    # story options (rule 3) -- off by default, thematically coherent.
    dict(name="all_broken", type='BOOL', default=False,
         desc="Abandonment: every window shattered, many boarded"),
    dict(name="sealed_unit", type='BOOL', default=False,
         desc="Regime: one unit's openings swallowed in aberration resin"),
    dict(name="rooftop_roost", type='BOOL', default=False,
         desc="Resistance: pigeon-loft signal post on the roof"),
]

params.register_tool(idname="la_dingbat", label="Dingbat Apartment",
                     family="Residential", build=build_dingbat, spec=SPEC)


def smoke():
    """Build with defaults, audit every mesh, return summaries."""
    import random
    objs = build_dingbat(params.defaults("la_dingbat"), random.Random(0))
    out = {}
    for ob in objs:
        rep = topology.validate_object(ob)
        out[ob.name] = (topology.ok(rep), topology.summarize(rep))
    return objs, out
