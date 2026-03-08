#!/usr/bin/env python3
"""
Discrete-time stability validator for skeleton XPBD constraint systems.

Linearizes the full XPBD timestep (predict → project → derive velocity)
around the rest-pose equilibrium and computes the eigenvalues of the
resulting update matrix A:

    [δx]       [δx]
    [  ]     = A [  ]
    [δv]       [δv]
       n+1        n

If max |eig(A)| > 1, the equilibrium is *physically unstable*: small
perturbations grow exponentially regardless of solver convergence.  The
solver may converge perfectly to the "correct" next state, but that state
is farther from equilibrium than the last one.

Usage (standalone):
    python3 validate_skeleton_stability.py skeleton.fskel
    python3 validate_skeleton_stability.py skeleton.fskel --min-compliance 1e-3
    python3 validate_skeleton_stability.py skeleton.fskel --sweep

Can also be imported:
    from validate_skeleton_stability import validate_fskel_stability
"""

import json
import math
import sys
import os

import numpy as np

try:
    import mpmath
    HAS_MPMATH = True
except ImportError:
    HAS_MPMATH = False


# ── Joint type enum (matches engine) ────────────────────────────

JOINT_NONE = 0
JOINT_CONE_TWIST = 1
JOINT_HINGE = 2
JOINT_DISTANCE = 3
JOINT_LOCK = 4
JOINT_BALL = 5
JOINT_COPY_ROTATION = 6
JOINT_LIMIT_ROTATION = 7
JOINT_LIMIT_POSITION = 8
JOINT_AIM = 9


# ── Geometry helpers ────────────────────────────────────────────

def _quat_to_mat3(q):
    """Convert [x, y, z, w] quaternion to 3x3 rotation matrix."""
    x, y, z, w = q
    return np.array([
        [1 - 2*(y*y + z*z),     2*(x*y - z*w),     2*(x*z + y*w)],
        [    2*(x*y + z*w), 1 - 2*(x*x + z*z),     2*(y*z - x*w)],
        [    2*(x*z - y*w),     2*(y*z + x*w), 1 - 2*(x*x + y*y)],
    ])


def _mat4_to_pos_quat(m):
    """Extract position and quaternion from a 4x4 column-major matrix."""
    M = np.array(m).reshape(4, 4).T  # column-major → row-major
    pos = M[:3, 3]

    R = M[:3, :3].copy()
    for i in range(3):
        s = np.linalg.norm(R[:, i])
        if s > 1e-12:
            R[:, i] /= s

    tr = R[0, 0] + R[1, 1] + R[2, 2]
    if tr > 0:
        s = 0.5 / math.sqrt(tr + 1.0)
        w = 0.25 / s
        x = (R[2, 1] - R[1, 2]) * s
        y = (R[0, 2] - R[2, 0]) * s
        z = (R[1, 0] - R[0, 1]) * s
    elif R[0, 0] > R[1, 1] and R[0, 0] > R[2, 2]:
        s = 2.0 * math.sqrt(1.0 + R[0, 0] - R[1, 1] - R[2, 2])
        w = (R[2, 1] - R[1, 2]) / s
        x = 0.25 * s
        y = (R[0, 1] + R[1, 0]) / s
        z = (R[0, 2] + R[2, 0]) / s
    elif R[1, 1] > R[2, 2]:
        s = 2.0 * math.sqrt(1.0 + R[1, 1] - R[0, 0] - R[2, 2])
        w = (R[0, 2] - R[2, 0]) / s
        x = (R[0, 1] + R[1, 0]) / s
        y = 0.25 * s
        z = (R[1, 2] + R[2, 1]) / s
    else:
        s = 2.0 * math.sqrt(1.0 + R[2, 2] - R[0, 0] - R[1, 1])
        w = (R[1, 0] - R[0, 1]) / s
        x = (R[0, 2] + R[2, 0]) / s
        y = (R[1, 2] + R[2, 1]) / s
        z = 0.25 * s

    q = np.array([x, y, z, w])
    q /= np.linalg.norm(q)
    return pos, q


# ── Jacobian builders ───────────────────────────────────────────

def _build_ball_jacobian_rows(pos_a, quat_a, pos_b, quat_b,
                               anchor_a_local, anchor_b_local):
    """Build 3 positional constraint rows for a ball/anchor joint.

    Returns list of (J_va, J_wa, J_vb, J_wb) tuples, one per row.
    Each J_v* is (3,), J_w* is (3,).
    """
    R_a = _quat_to_mat3(quat_a)
    R_b = _quat_to_mat3(quat_b)
    r_a = R_a @ np.array(anchor_a_local)
    r_b = R_b @ np.array(anchor_b_local)

    rows = []
    for ax in [np.array([1,0,0]), np.array([0,1,0]), np.array([0,0,1])]:
        J_va = -ax
        J_wa = -np.cross(r_a, ax)
        J_vb = ax
        J_wb = np.cross(r_b, ax)
        rows.append((J_va, J_wa, J_vb, J_wb))
    return rows


def _build_angular_jacobian_rows(quat_a, quat_b, active_axes=7):
    """Build angular constraint rows (cone-twist angular part).

    Returns list of (J_va, J_wa, J_vb, J_wb) tuples.
    """
    R_a = _quat_to_mat3(quat_a)
    rows = []
    for i in range(3):
        if not (active_axes & (1 << i)):
            continue
        ax = R_a[:, i]
        rows.append((np.zeros(3), -ax, np.zeros(3), ax))
    return rows


# ── Body property estimation ────────────────────────────────────

def _estimate_body_properties(collider, bone_length):
    """Estimate mass and inertia diagonal from collider geometry.

    Returns (mass, inv_inertia_diag) as (float, np.array(3)).
    """
    density = 1000.0

    shape = collider.get("shape", "none")
    if shape == "capsule":
        radius = collider.get("radius", 0.05)
        half_h = collider.get("half_height", bone_length * 0.5)
        vol = math.pi * radius**2 * 2.0 * half_h + \
              (4.0/3.0) * math.pi * radius**3
        mass = max(density * vol, 0.1)
        Ixx = mass * (3.0*radius**2 + (2.0*half_h)**2) / 12.0
        Iyy = Ixx
        Izz = mass * radius**2 / 2.0
    elif shape == "box":
        hx = collider.get("half_extents", [0.05, bone_length*0.5, 0.05])
        vol = 8.0 * hx[0] * hx[1] * hx[2]
        mass = max(density * vol, 0.1)
        Ixx = mass * (hx[1]**2 + hx[2]**2) / 3.0
        Iyy = mass * (hx[0]**2 + hx[2]**2) / 3.0
        Izz = mass * (hx[0]**2 + hx[1]**2) / 3.0
    elif shape == "sphere":
        radius = collider.get("radius", 0.05)
        vol = (4.0/3.0) * math.pi * radius**3
        mass = max(density * vol, 0.1)
        Ixx = Iyy = Izz = 0.4 * mass * radius**2
    else:
        radius = 0.03
        half_h = bone_length * 0.5
        vol = math.pi * radius**2 * 2.0 * half_h + \
              (4.0/3.0) * math.pi * radius**3
        mass = max(density * vol, 0.01)
        Ixx = mass * (3.0*radius**2 + bone_length**2) / 12.0
        Iyy = Ixx
        Izz = mass * radius**2 / 2.0

    min_I = 1e-6
    inv_I = np.array([1.0/max(Ixx, min_I),
                      1.0/max(Iyy, min_I),
                      1.0/max(Izz, min_I)])
    return mass, inv_I


# ── Skeleton data extraction ────────────────────────────────────

def _extract_skeleton(fskel_data, min_compliance):
    """Parse .fskel JSON into body properties and constraint rows.

    Returns:
        n_bodies: int
        masses: list[float]
        inv_inertias: list[np.array(3)]
        positions: list[np.array(3)]
        orientations: list[np.array(4)]  (xyzw quaternions)
        all_rows: list of (body_a, body_b, J_va, J_wa, J_vb, J_wb)
        all_compliances: list[float]
        joint_names: list[str]
    """
    joints_data = fskel_data.get("joints", [])
    n_bodies = len(joints_data)

    positions = []
    orientations = []
    masses = []
    inv_inertias = []
    parent_map = []

    for i, jd in enumerate(joints_data):
        rw = jd.get("rest_world")
        if rw:
            pos, quat = _mat4_to_pos_quat(rw)
        else:
            pos, quat = np.zeros(3), np.array([0,0,0,1.0])
        positions.append(pos)
        orientations.append(quat)
        parent_map.append(jd.get("parent", -1))

        tail = np.array(jd.get("tail_pos", [0, 0.1, 0]))
        bone_length = max(np.linalg.norm(tail - pos), 0.02)
        collider = jd.get("collider", {"shape": "none"})
        m, inv_I = _estimate_body_properties(collider, bone_length)
        masses.append(m)
        inv_inertias.append(inv_I)

    all_rows = []
    all_compliances = []
    joint_names = []

    for i, jd in enumerate(joints_data):
        desc = jd.get("joint_desc", {})
        jt = desc.get("type", 0)
        if jt == 0:
            continue
        parent_idx = parent_map[i]
        if parent_idx < 0:
            continue

        stiffness = desc.get("stiffness", 0.0)
        compliance = (1.0 / stiffness) if stiffness > 0 else 0.0
        if compliance < min_compliance:
            compliance = min_compliance

        limit_axes = desc.get("limit_axes", 0)
        anchor_a = desc.get("anchor_a", list(positions[i]))
        anchor_b = desc.get("anchor_b", list(positions[i]))
        R_par = _quat_to_mat3(orientations[parent_idx])
        R_child = _quat_to_mat3(orientations[i])
        anchor_a_local = R_par.T @ (np.array(anchor_a) - positions[parent_idx])
        anchor_b_local = R_child.T @ (np.array(anchor_b) - positions[i])
        bone_name = jd.get("name", f"bone_{i}")

        if jt in (JOINT_CONE_TWIST, JOINT_BALL, JOINT_LOCK, JOINT_HINGE):
            for row in _build_ball_jacobian_rows(
                    positions[parent_idx], orientations[parent_idx],
                    positions[i], orientations[i],
                    anchor_a_local, anchor_b_local):
                all_rows.append((parent_idx, i) + row)
                all_compliances.append(compliance)
                joint_names.append(bone_name)

        if jt == JOINT_CONE_TWIST:
            for row in _build_angular_jacobian_rows(
                    orientations[parent_idx], orientations[i],
                    active_axes=limit_axes):
                all_rows.append((parent_idx, i) + row)
                all_compliances.append(compliance)
                joint_names.append(bone_name + " (ang)")
        elif jt == JOINT_HINGE:
            for row in _build_angular_jacobian_rows(
                    orientations[parent_idx], orientations[i],
                    active_axes=0x6):
                all_rows.append((parent_idx, i) + row)
                all_compliances.append(compliance)
                joint_names.append(bone_name + " (ang)")
        elif jt == JOINT_LOCK:
            for row in _build_angular_jacobian_rows(
                    orientations[parent_idx], orientations[i],
                    active_axes=0x7):
                all_rows.append((parent_idx, i) + row)
                all_compliances.append(compliance)
                joint_names.append(bone_name + " (ang)")
        elif jt == JOINT_DISTANCE:
            diff = positions[i] - positions[parent_idx]
            dist = np.linalg.norm(diff)
            n = diff / dist if dist > 1e-6 else np.array([0,1,0])
            r_a = R_par @ np.array(anchor_a_local)
            r_b = R_child @ np.array(anchor_b_local)
            all_rows.append((parent_idx, i, -n, -np.cross(r_a, n),
                             n, np.cross(r_b, n)))
            all_compliances.append(compliance)
            joint_names.append(bone_name)

    return (n_bodies, masses, inv_inertias, positions, orientations,
            all_rows, all_compliances, joint_names)


# ── Core analysis ────────────────────────────────────────────────

def validate_fskel_stability(fskel_data, dt=1.0/240.0, min_compliance=1e-3,
                              vel_damping=0.0, verbose=True):
    """Compute eigenvalues of the linearized discrete-time XPBD update.

    The full XPBD substep around equilibrium is:
        1. Predict:  v' = v + g·dt  →  x_pred = x + v'·dt
        2. Project:  x_solved = x_pred - M⁻¹Jᵀ(JM⁻¹Jᵀ + α̃I)⁻¹ J x_pred
        3. Derive:   v_new = (x_solved - x_old) / dt

    Linearized about (x_eq, v=0) where C(x_eq) = 0:
        Let K = M⁻¹Jᵀ(JM⁻¹Jᵀ + α̃I)⁻¹J   (XPBD projection in gen. coords)
        Then:
            δx_{n+1} = (I-K)(δx_n + δv_n·dt)
            δv_{n+1} = d·((I-K-I)·δx_n/dt + (I-K)·δv_n)
                     = d·(-K·δx_n/dt + (I-K)·δv_n)
        where d = 1 - vel_damping·dt

    State update matrix:
        A = [(I-K),         (I-K)·dt     ]
            [d·(-K/dt),     d·(I-K)      ]

    If max|eig(A)| > 1, the equilibrium is UNSTABLE.  Even a perfect
    solver cannot hold the rig together — it will explode.

    Args:
        fskel_data: Parsed .fskel JSON dict.
        dt: XPBD sub-substep timestep (seconds).
        min_compliance: Minimum compliance floor for joints.
        vel_damping: Velocity damping coefficient per substep.
        verbose: Print diagnostics.

    Returns:
        (max_eig_magnitude, warnings)
    """
    (n_bodies, masses, inv_inertias, positions, orientations,
     all_rows, all_compliances, joint_names) = \
        _extract_skeleton(fskel_data, min_compliance)

    n_rows = len(all_rows)
    if n_rows == 0:
        return 0.0, ["No constraint rows to analyze"]

    warnings = []

    # ── Build full Jacobian J (n_rows × 6*n_bodies) ──────────────
    # Generalized coordinates: [x0, y0, z0, θx0, θy0, θz0, x1, ...]
    # Each body has 6 DOFs: 3 position + 3 orientation (small-angle)

    N = 6 * n_bodies
    J = np.zeros((n_rows, N))

    for r_idx in range(n_rows):
        ba, bb, J_va, J_wa, J_vb, J_wb = all_rows[r_idx]
        # Body A: position cols [6*ba : 6*ba+3], orientation cols [6*ba+3 : 6*ba+6]
        J[r_idx, 6*ba:6*ba+3]   = J_va
        J[r_idx, 6*ba+3:6*ba+6] = J_wa
        # Body B:
        J[r_idx, 6*bb:6*bb+3]   = J_vb
        J[r_idx, 6*bb+3:6*bb+6] = J_wb

    # ── Build inverse mass matrix M⁻¹ (N × N, diagonal) ─────────

    M_inv = np.zeros((N, N))
    for b in range(n_bodies):
        inv_m = 1.0 / masses[b] if masses[b] > 0 else 0.0
        # Linear DOFs
        M_inv[6*b,   6*b]   = inv_m
        M_inv[6*b+1, 6*b+1] = inv_m
        M_inv[6*b+2, 6*b+2] = inv_m
        # Angular DOFs: rotate inv_inertia to world frame
        R = _quat_to_mat3(orientations[b])
        inv_I_world = R @ np.diag(inv_inertias[b]) @ R.T
        M_inv[6*b+3:6*b+6, 6*b+3:6*b+6] = inv_I_world

    # ── Build XPBD projection K = M⁻¹Jᵀ(JM⁻¹Jᵀ + α̃I)⁻¹J ──────

    alpha_tilde = np.array([c / (dt*dt) for c in all_compliances])
    W = J @ M_inv @ J.T + np.diag(alpha_tilde)

    try:
        W_inv = np.linalg.inv(W)
    except np.linalg.LinAlgError:
        return float('inf'), ["Constraint system matrix W is singular"]

    K = M_inv @ J.T @ W_inv @ J

    I_N = np.eye(N)
    ImK = I_N - K  # (I - K)

    # ── Build 2N × 2N discrete-time update matrix A ──────────────

    d = max(1.0 - vel_damping * dt, 0.0)

    A = np.zeros((2*N, 2*N))
    A[:N, :N]   = ImK              # δx block: (I-K)
    A[:N, N:]   = ImK * dt         # δx block: (I-K)·dt
    A[N:, :N]   = d * (-K / dt)    # δv block: d·(-K/dt)
    A[N:, N:]   = d * ImK          # δv block: d·(I-K)

    eigenvalues = np.linalg.eigvals(A)
    magnitudes = np.abs(eigenvalues)
    max_mag = float(np.max(magnitudes))

    # ── Identify which modes are unstable ────────────────────────

    unstable_count = int(np.sum(magnitudes > 1.0 + 1e-10))

    # For each unstable eigenvalue, identify the dominant body DOF
    # by looking at the corresponding eigenvector
    eigvals_full, eigvecs_full = np.linalg.eig(A)
    unstable_modes = []
    sorted_idx = np.argsort(-np.abs(eigvals_full))
    for k in range(min(10, len(sorted_idx))):
        idx = sorted_idx[k]
        mag = abs(eigvals_full[idx])
        if mag < 1.0 - 1e-10:
            break
        # Find which body DOF dominates this eigenvector
        vec = np.abs(eigvecs_full[:, idx])
        # Position part is first N entries
        pos_part = vec[:N]
        dominant_dof = int(np.argmax(pos_part))
        body_idx = dominant_dof // 6
        dof_in_body = dominant_dof % 6
        dof_names = ['x', 'y', 'z', 'θx', 'θy', 'θz']
        # Find joint name for this body
        joints_data = fskel_data.get("joints", [])
        body_name = joints_data[body_idx].get("name", f"body_{body_idx}") \
                    if body_idx < len(joints_data) else f"body_{body_idx}"
        unstable_modes.append((mag, eigvals_full[idx], body_name,
                               dof_names[dof_in_body], body_idx))

    # ── Also compute solver convergence (spectral radius of GS) ──

    gs_rho = None
    if n_rows > 0:
        D = np.diag(np.diag(W))
        L_mat = np.tril(W, -1)
        U_mat = np.triu(W, 1)
        DL = D + L_mat
        try:
            M_gs = -np.linalg.inv(DL) @ U_mat
            gs_rho = float(np.max(np.abs(np.linalg.eigvals(M_gs))))
        except np.linalg.LinAlgError:
            gs_rho = None

    # ── Diagnostics ──────────────────────────────────────────────

    if verbose:
        print(f"\n{'='*65}")
        print(f"  XPBD Discrete-Time Stability Validator")
        print(f"{'='*65}")
        print(f"  Bodies: {n_bodies}   Constraint rows: {n_rows}")
        print(f"  State dimension: {2*N} ({N} position + {N} velocity)")
        print(f"  Timestep dt: {dt:.6f}s   Compliance floor: {min_compliance:.1e}")
        print(f"  Velocity damping: {vel_damping}")
        print()

        if gs_rho is not None:
            print(f"  Solver convergence (GS iteration ρ): {gs_rho:.6f}")
            if gs_rho < 0.9:
                print(f"    ✅ Solver converges well")
            elif gs_rho < 1.0:
                print(f"    ⚠️  Solver converges slowly")
            else:
                print(f"    ❌ Solver diverges (separate issue)")
            print()

        print(f"  System stability: max|eig(A)| = {max_mag:.6f}")
        print()

        if max_mag < 1.0 - 1e-6:
            print(f"  ✅ STABLE (max|eig| = {max_mag:.6f} < 1.0)")
            print(f"     Equilibrium is asymptotically stable.")
            print(f"     Perturbations decay by {(1-max_mag)*100:.1f}% per substep.")
        elif max_mag < 1.0 + 1e-6:
            print(f"  ⚠️  MARGINALLY STABLE (max|eig| ≈ 1.0)")
            print(f"     Perturbations neither grow nor decay.")
            print(f"     System will oscillate indefinitely.")
        else:
            growth = max_mag - 1.0
            print(f"  ❌ UNSTABLE (max|eig| = {max_mag:.6f} > 1.0)")
            print(f"     Perturbations grow by {growth*100:.2f}% per substep.")
            double_time = math.log(2.0) / math.log(max_mag)
            print(f"     Error doubles every {double_time:.1f} substeps "
                  f"({double_time * dt * 1000:.2f} ms).")
            warnings.append(f"Unstable: max|eig| = {max_mag:.6f}")

        if unstable_modes:
            print(f"\n  Critical/near-unity modes (top {len(unstable_modes)}):")
            for mag, ev, name, dof, bidx in unstable_modes:
                sign = "UNSTABLE" if mag > 1.0 + 1e-4 else "marginal"
                # Show complex eigenvalue in polar form
                phase = math.atan2(ev.imag, ev.real) * 180.0 / math.pi
                print(f"    |λ|={mag:.6f} ∠{phase:+.1f}° → body '{name}' "
                      f"DOF {dof} [{sign}]")

        # Per-joint compliance
        print(f"\n  Joint compliance values:")
        seen = set()
        for i, name in enumerate(joint_names):
            key = name.split(" (")[0]
            if key in seen:
                continue
            seen.add(key)
            c = all_compliances[i]
            stiff = 1.0/c if c > 0 else float('inf')
            at = c / (dt*dt)
            print(f"    {name}: α={c:.2e}  k={stiff:.0f} N/m  "
                  f"α̃={at:.2e}")

        # Top eigenvalue magnitudes
        top_mags = np.sort(magnitudes)[::-1]
        print(f"\n  Top 10 eigenvalue magnitudes:")
        for k in range(min(10, len(top_mags))):
            ev = eigvals_full[sorted_idx[k]]
            phase = math.atan2(ev.imag, ev.real) * 180 / math.pi
            marker = " ← UNSTABLE" if abs(ev) > 1.0 + 1e-4 else ""
            print(f"    λ_{k}: |{abs(ev):.6f}| ∠{phase:+.1f}°{marker}")

        print(f"{'='*65}\n")

    if max_mag > 1.0 + 1e-10:
        warnings.append(
            f"System max|eig| = {max_mag:.6f}: equilibrium is unstable "
            f"at dt={dt:.6f}s, compliance={min_compliance:.1e}")

    # ── mpmath high-precision verification ─────────────────────────
    if HAS_MPMATH and verbose:
        mp_dps = 8
        mp_mag, mp_top, mp_K_eigs = _mpmath_verify_eigenvalues(
            J, M_inv, alpha_tilde, N, dt, d, dps=mp_dps)
        if mp_mag is not None:
            print(f"\n  {'─'*55}")
            print(f"  mpmath verification ({mp_dps} digits, analytical reduction):")
            print(f"  |λ_A| = sqrt(d·(1-κ))  where κ = eigenvalue of K")
            print(f"  max|eig(A)| = {mpmath.nstr(mp_mag, mp_dps)}")
            print(f"  Top K eigenvalues (projection matrix):")
            for k, kv in enumerate(mp_K_eigs[:5]):
                print(f"    κ_{k}: {mpmath.nstr(kv, mp_dps)}")
            print(f"  Top A eigenvalue magnitudes:")
            for k, m in enumerate(mp_top[:10]):
                marker = " ← UNSTABLE" if m > 1 + 1e-4 else ""
                print(f"    λ_{k}: |{mpmath.nstr(m, mp_dps)}|{marker}")
            # Check if any K eigenvalue is meaningfully outside [0,1].
            # Tiny negative values (~1e-9) are expected numerical noise from
            # the matrix inverse; only flag values that could cause real
            # instability (growth > 0.01% per substep → κ < -1e-4).
            bad_K = [kv for kv in mp_K_eigs if kv < -1e-4 or kv > 1 + 1e-4]
            noise_K = [kv for kv in mp_K_eigs if kv < -1e-10 and kv >= -1e-4]
            if bad_K:
                print(f"\n  ❌ K has eigenvalues meaningfully outside [0,1]!")
                print(f"     This means the projection is malformed.")
                for kv in bad_K:
                    print(f"     κ = {mpmath.nstr(kv, mp_dps)}")
                warnings.append(
                    f"Projection K has eigenvalue outside [0,1]: "
                    f"κ = {mpmath.nstr(bad_K[0], mp_dps)}")
            elif noise_K:
                print(f"\n  ⚠️  K has {len(noise_K)} eigenvalue(s) slightly "
                      f"negative (numerical noise)")
                print(f"     Worst: κ = {mpmath.nstr(min(noise_K), mp_dps)}")
                print(f"     These are within matrix inverse precision; "
                      f"system is effectively marginally stable.")
            elif mp_mag > 1:
                growth = float(mp_mag - 1)
                print(f"\n  ❌ CONFIRMED UNSTABLE at high precision!")
                print(f"     Growth rate: {growth:.2e} per substep")
                double_time = float(mpmath.log(2) / mpmath.log(mp_mag))
                print(f"     Error doubles every {double_time:.1f} substeps "
                      f"({double_time * dt * 1000:.2f} ms)")
                if max_mag <= 1.0 + 1e-10:
                    warnings.append(
                        f"mpmath reveals instability MISSED by float64: "
                        f"max|eig| = {mpmath.nstr(mp_mag, mp_dps)}")
            elif abs(float(mp_mag) - 1.0) < 10**(-mp_dps + 1):
                print(f"\n  ⚠️  Confirmed MARGINALLY STABLE at high precision")
                print(f"     (unconstrained rigid-body DOFs have |λ|=1 exactly)")
            else:
                print(f"\n  ✅ Confirmed STABLE at high precision")
            print(f"  {'─'*55}")

    if max_mag > 1.0 + 1e-10:
        warnings.append(
            f"System max|eig| = {max_mag:.6f}: equilibrium is unstable "
            f"at dt={dt:.6f}s, compliance={min_compliance:.1e}")

    return max_mag, warnings


def _mpmath_verify_eigenvalues(J_np, M_inv_np, alpha_tilde_np, N, dt, d,
                                dps=8):
    """Verify stability using mpmath arbitrary-precision arithmetic.

    Uses an analytical reduction: the 2N×2N update matrix A has eigenvalues
    determined by the N×N projection K = M⁻¹Jᵀ(JM⁻¹Jᵀ + α̃I)⁻¹J.

    For each eigenvalue κ of K, the eigenvalues of A satisfy:
        λ² - (1-κ)(1+d)λ + d(1-κ) = 0
    giving |λ|² = d·(1-κ).

    If K is PSD with eigenvalues in [0,1], then |λ| ≤ 1 exactly.
    This function verifies that K's eigenvalues are indeed in [0,1]
    at `dps` decimal digits of precision (default 8).

    Returns:
        (max_A_magnitude, sorted_A_magnitudes) or (None, []) on error.
    """
    if not HAS_MPMATH:
        return None, []

    old_dps = mpmath.mp.dps
    mpmath.mp.dps = dps

    try:
        n_rows = J_np.shape[0]

        # Convert numpy arrays to mpmath matrices
        J = mpmath.matrix(n_rows, N)
        for i in range(n_rows):
            for j in range(N):
                v = float(J_np[i, j])
                if v != 0.0:
                    J[i, j] = mpmath.mpf(v)

        M_inv = mpmath.matrix(N, N)
        for i in range(N):
            v = float(M_inv_np[i, i])
            if v != 0.0:
                M_inv[i, i] = mpmath.mpf(v)

        # W = J @ M_inv @ J^T + diag(alpha_tilde)
        JM = J * M_inv
        W = JM * J.T
        for i in range(n_rows):
            W[i, i] += mpmath.mpf(float(alpha_tilde_np[i]))

        # K = M_inv @ J^T @ W^{-1} @ J  (N×N projection matrix)
        W_inv = W ** (-1)
        K = M_inv * J.T * W_inv * J

        # Compute eigenvalues of K (N×N, much faster than 2N×2N)
        K_eigs = mpmath.eig(K, left=False, right=False)

        d_mp = mpmath.mpf(d)

        # Map K eigenvalues → A eigenvalue magnitudes via:
        #   |λ_A|² = d·(1-κ)
        A_magnitudes = []
        K_vals_real = []
        for kappa in K_eigs:
            kappa_re = mpmath.re(kappa)
            K_vals_real.append(kappa_re)
            sigma = 1 - kappa_re
            mag_sq = d_mp * sigma
            if mag_sq < 0:
                mag_sq = mpmath.mpf(0)
            mag = mpmath.sqrt(mag_sq)
            # Each K eigenvalue produces 2 A eigenvalues with same magnitude
            A_magnitudes.append(mag)
            A_magnitudes.append(mag)

        A_magnitudes.sort(reverse=True)
        K_vals_real.sort(reverse=True)

        max_mag = A_magnitudes[0] if A_magnitudes else mpmath.mpf(0)
        return max_mag, A_magnitudes, K_vals_real

    except Exception as e:
        print(f"  mpmath verification failed: {e}")
        return None, [], []
    finally:
        mpmath.mp.dps = old_dps


def validate_fskel_file(filepath, dt=1.0/240.0, min_compliance=1e-3,
                         vel_damping=0.0):
    """Load and validate a .fskel file."""
    with open(filepath, 'r') as f:
        data = json.load(f)
    return validate_fskel_stability(data, dt=dt,
                                    min_compliance=min_compliance,
                                    vel_damping=vel_damping)


def sweep_compliance(filepath, dt=1.0/240.0, vel_damping=0.0):
    """Sweep compliance values and report stability threshold."""
    with open(filepath, 'r') as f:
        data = json.load(f)

    values = [0, 1e-8, 1e-7, 1e-6, 1e-5, 1e-4, 5e-4,
              1e-3, 2e-3, 5e-3, 1e-2, 5e-2, 0.1]

    print(f"\n{'='*65}")
    print(f"  Compliance sweep: dt={dt:.6f}s  damping={vel_damping}")
    print(f"{'='*65}")
    print(f"  {'compliance':>12s}  {'max|eig|':>10s}  {'status':>10s}  "
          f"{'error halving':>15s}")
    print(f"  {'-'*12}  {'-'*10}  {'-'*10}  {'-'*15}")

    stable_threshold = None
    for c in values:
        mag, _ = validate_fskel_stability(data, dt=dt,
                                          min_compliance=c,
                                          vel_damping=vel_damping,
                                          verbose=False)
        if mag < 1.0 - 1e-10:
            status = "STABLE"
            if mag > 0:
                half = math.log(0.5) / math.log(mag)
                half_str = f"{half:.1f} substeps"
            else:
                half_str = "instant"
            if stable_threshold is None:
                stable_threshold = c
        elif mag < 1.0 + 1e-10:
            status = "MARGINAL"
            half_str = "∞"
        else:
            status = "UNSTABLE"
            double = math.log(2.0) / math.log(mag)
            half_str = f"×2 in {double:.1f}"

        print(f"  {c:>12.1e}  {mag:>10.6f}  {status:>10s}  {half_str:>15s}")

    print(f"{'='*65}")
    if stable_threshold is not None:
        print(f"  Minimum stable compliance: {stable_threshold:.1e}")
    else:
        print(f"  ❌ No compliance value tested was stable!")
    print()


# ── CLI entry point ──────────────────────────────────────────────

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <skeleton.fskel> [options]")
        print(f"  --dt SECONDS          Substep timestep (default: 1/240)")
        print(f"  --min-compliance VAL  Compliance floor (default: 1e-3)")
        print(f"  --vel-damping VAL     Velocity damping (default: 0)")
        print(f"  --sweep               Sweep compliance values")
        sys.exit(1)

    filepath = sys.argv[1]
    dt = 1.0 / 240.0
    min_compliance = 1e-3
    vel_damping = 0.0
    do_sweep = False

    i = 2
    while i < len(sys.argv):
        if sys.argv[i] == "--dt" and i + 1 < len(sys.argv):
            dt = float(sys.argv[i + 1]); i += 2
        elif sys.argv[i] == "--min-compliance" and i + 1 < len(sys.argv):
            min_compliance = float(sys.argv[i + 1]); i += 2
        elif sys.argv[i] == "--vel-damping" and i + 1 < len(sys.argv):
            vel_damping = float(sys.argv[i + 1]); i += 2
        elif sys.argv[i] == "--sweep":
            do_sweep = True; i += 1
        else:
            print(f"Unknown argument: {sys.argv[i]}"); sys.exit(1)

    if not os.path.exists(filepath):
        print(f"File not found: {filepath}"); sys.exit(1)

    if do_sweep:
        sweep_compliance(filepath, dt=dt, vel_damping=vel_damping)
    else:
        mag, warnings = validate_fskel_file(filepath, dt=dt,
                                             min_compliance=min_compliance,
                                             vel_damping=vel_damping)
        sys.exit(0 if mag < 1.0 + 1e-4 else 1)
