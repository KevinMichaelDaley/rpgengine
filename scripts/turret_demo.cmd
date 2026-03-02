# turret_demo.cmd — Spawn turret entities with trigger zones and load the turret AI script.
#
# Usage: source scripts/turret_demo.cmd
#
# Each turret is a heavy base (box resting on the ground) with a barrel
# (capsule) on top connected by a hinge joint around the vertical axis.
# Trigger spheres (invisible) detect player proximity.

# Ground plane (large static box)
entity_def ground
  type box
  pos 0 -0.5 0
  scale 100 1 100
  static
end

# ── Turret 0 (-10, 0, -10) ──────────────────────────────────────────

entity_def base_0
  type box
  pos -10 0.75 -10
  scale 2 1.5 2
  mass 100
  setattr 257 true
end

entity_def barrel_0
  type capsule
  pos -10 2.25 -10
  scale 0.5 1.0 0.5
  mass 5
end

joint hinge base_0 barrel_0 -10 1.5 -10 0 1 0

# ── Turret 1 (10, 0, -10) ───────────────────────────────────────────

entity_def base_1
  type box
  pos 10 0.75 -10
  scale 2 1.5 2
  mass 100
  setattr 257 true
end

entity_def barrel_1
  type capsule
  pos 10 2.25 -10
  scale 0.5 1.0 0.5
  mass 5
end

joint hinge base_1 barrel_1 10 1.5 -10 0 1 0

# ── Turret 2 (-10, 0, 10) ───────────────────────────────────────────

entity_def base_2
  type box
  pos -10 0.75 10
  scale 2 1.5 2
  mass 100
  setattr 257 true
end

entity_def barrel_2
  type capsule
  pos -10 2.25 10
  scale 0.5 1.0 0.5
  mass 5
end

joint hinge base_2 barrel_2 -10 1.5 10 0 1 0

# ── Turret 3 (10, 0, 10) ────────────────────────────────────────────

entity_def base_3
  type box
  pos 10 0.75 10
  scale 2 1.5 2
  mass 100
  setattr 257 true
end

entity_def barrel_3
  type capsule
  pos 10 2.25 10
  scale 0.5 1.0 0.5
  mass 5
end

joint hinge base_3 barrel_3 10 1.5 10 0 1 0

# ── Trigger zones (invisible spheres for proximity detection) ────────

entity_def trigger_0
  type sphere
  pos -10 1 -10
  scale 40 40 40
  static
  setattr 256 true
end

entity_def trigger_1
  type sphere
  pos 10 1 -10
  scale 40 40 40
  static
  setattr 256 true
end

entity_def trigger_2
  type sphere
  pos -10 1 10
  scale 40 40 40
  static
  setattr 256 true
end

entity_def trigger_3
  type sphere
  pos 10 1 10
  scale 40 40 40
  static
  setattr 256 true
end

# Resume physics so bodies are simulated and snapshots flow to clients
physics_resume

# Load the turret AI script
script load turret_ai scripts/turret_ai.il
