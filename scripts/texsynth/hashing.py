"""Deterministic integer hashing for stateless, aperiodic tile placement.

The field sampler chooses a tile (and per-tile jitter) purely from integer grid
coordinates, so any cell is reproducible from its coordinates alone with no scan
state. We use a SplitMix64-style finaliser over a coordinate mix, computed in
pure Python ints (masked to 64 bits) so negative coordinates and overflow are
well defined and NumPy raises no warnings.

All functions are pure and side-effect free.
"""

_M64 = 0xFFFFFFFFFFFFFFFF


def _mix64(x):
    """SplitMix64 finaliser: avalanche a 64-bit integer to a 64-bit hash."""
    x &= _M64
    x ^= x >> 30
    x = (x * 0xBF58476D1CE4E5B9) & _M64
    x ^= x >> 27
    x = (x * 0x94D049BB133111EB) & _M64
    x ^= x >> 31
    return x & _M64


def hash2d(i, j, salt=0):
    """Hash two (possibly negative) integers and a salt to a 64-bit unsigned int.

    Deterministic across runs and platforms. Asymmetric in (i, j).
    """
    h = ((i & _M64) * 0x9E3779B97F4A7C15) & _M64
    h = (h ^ (((j & _M64) * 0xC2B2AE3D27D4EB4F) & _M64)) & _M64
    h = (h ^ (((salt & _M64) * 0x165667B19E3779F9) & _M64)) & _M64
    return _mix64(h)


def hash_unit(i, j, salt=0):
    """Hash to a float in [0, 1)."""
    return hash2d(i, j, salt) / (_M64 + 1)


def edge_color(i, j, colors, salt=0):
    """Colour (0..colors-1) assigned to the grid line identified by (i, j).

    Adjacent cells sharing that line read the same coordinate, so their shared
    edge colour matches automatically.
    """
    return hash2d(i, j, salt) % colors


def rand_offset(i, j, width, height, salt=0):
    """Deterministic (x, y) offset in [0,width) x [0,height) for cell (i, j)."""
    h = hash2d(i, j, salt ^ 0x5DEECE66D)
    return int(h % width), int((h >> 20) % height)
