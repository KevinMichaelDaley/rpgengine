"""High-level texture-field synthesis: exemplar in, large aperiodic field out."""

from .patch_synth import synth_patchwork


def synth_field(exemplar, width, height, patch=192, overlap=None, seed=0,
                engine="dp"):
    """Synthesise a (height, width[, C]) seamless, aperiodic field from *exemplar*.

    Places overlapping exemplar patches merged with irregular minimum-error
    seams, so the field has no square-grid structure. Deterministic in *seed*.
    engine: 'dp' (fast, default) or 'graphcut' (slower, fully polygonal).
    """
    return synth_patchwork(exemplar, width, height, patch=patch, overlap=overlap,
                           seed=seed, engine=engine)
