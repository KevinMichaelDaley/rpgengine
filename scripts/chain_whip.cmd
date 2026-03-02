# chain_whip.cmd — Chain whip stress test
# 3 chains of 40 capsules, ball-jointed tip-to-tail.
# Root nodes are kinematic spheres orbiting at radius 5.
# Root y=63.0, chain bottom ~0.8m spacing.
#
# Usage: source scripts/chain_whip.cmd

# Ground plane (halfspace: pos = point on plane, rot = normal)
entity_def ground
  type halfspace
  pos 0 0 0
  rot 0 1 0
  friction 0.8
  restitution 0.0
end

# Scattered boxes for collisions
entity_def box_0
  type box
  pos 4.2 0.5 -14.2
  scale 1.0 1.0 1.0
  mass 5
end

entity_def box_1
  type box
  pos -8.3 0.8 7.1
  scale 1.5 1.5 1.5
  mass 5
end

entity_def box_2
  type box
  pos 11.8 0.6 -12.4
  scale 1.2 1.2 1.2
  mass 5
end

entity_def box_3
  type box
  pos -14.1 0.7 -8.4
  scale 1.3 1.3 1.3
  mass 5
end

entity_def box_4
  type box
  pos -14.2 0.8 -9.0
  scale 1.5 1.5 1.5
  mass 5
end

entity_def box_5
  type box
  pos 1.3 0.7 -8.4
  scale 1.4 1.4 1.4
  mass 5
end

entity_def box_6
  type box
  pos 9.3 0.8 -14.8
  scale 1.7 1.7 1.7
  mass 5
end

entity_def box_7
  type box
  pos 5.9 0.4 -4.8
  scale 0.8 0.8 0.8
  mass 5
end

entity_def box_8
  type box
  pos 13.7 0.3 -4.9
  scale 0.7 0.7 0.7
  mass 5
end

entity_def box_9
  type box
  pos -12.1 0.7 10.4
  scale 1.4 1.4 1.4
  mass 5
end

entity_def box_10
  type box
  pos 9.2 0.7 6.9
  scale 1.4 1.4 1.4
  mass 5
end

entity_def box_11
  type box
  pos 14.2 0.7 -3.6
  scale 1.4 1.4 1.4
  mass 5
end

entity_def box_12
  type box
  pos 9.9 0.9 3.6
  scale 1.8 1.8 1.8
  mass 5
end

entity_def box_13
  type box
  pos 2.3 0.3 6.1
  scale 0.7 0.7 0.7
  mass 5
end

entity_def box_14
  type box
  pos -8.2 0.3 -6.3
  scale 0.7 0.7 0.7
  mass 5
end

entity_def box_15
  type box
  pos -8.0 0.5 -12.0
  scale 1.0 1.0 1.0
  mass 5
end

entity_def box_16
  type box
  pos 4.1 0.6 -4.1
  scale 1.1 1.1 1.1
  mass 5
end

entity_def box_17
  type box
  pos -8.7 0.9 -7.0
  scale 1.9 1.9 1.9
  mass 5
end

entity_def box_18
  type box
  pos 4.4 0.4 3.3
  scale 0.8 0.8 0.8
  mass 5
end

entity_def box_19
  type box
  pos 6.9 0.6 -10.1
  scale 1.1 1.1 1.1
  mass 5
end

# ── Extra boxes under chain sweep path ──
# Grid of boxes near origin where chains descend to y=18

entity_def sweep_box_0
  type box
  pos 0.0 0.5 0.0
  scale 1.0 1.0 1.0
  mass 3
end

entity_def sweep_box_1
  type box
  pos 3.0 0.5 0.0
  scale 1.0 1.0 1.0
  mass 3
end

entity_def sweep_box_2
  type box
  pos -3.0 0.5 0.0
  scale 1.0 1.0 1.0
  mass 3
end

entity_def sweep_box_3
  type box
  pos 0.0 0.5 3.0
  scale 1.0 1.0 1.0
  mass 3
end

entity_def sweep_box_4
  type box
  pos 0.0 0.5 -3.0
  scale 1.0 1.0 1.0
  mass 3
end

entity_def sweep_box_5
  type box
  pos 3.0 0.5 3.0
  scale 1.0 1.0 1.0
  mass 3
end

entity_def sweep_box_6
  type box
  pos -3.0 0.5 3.0
  scale 1.0 1.0 1.0
  mass 3
end

entity_def sweep_box_7
  type box
  pos 3.0 0.5 -3.0
  scale 1.0 1.0 1.0
  mass 3
end

entity_def sweep_box_8
  type box
  pos -3.0 0.5 -3.0
  scale 1.0 1.0 1.0
  mass 3
end

entity_def sweep_box_9
  type box
  pos 6.0 0.5 0.0
  scale 1.0 1.0 1.0
  mass 3
end

entity_def sweep_box_10
  type box
  pos -6.0 0.5 0.0
  scale 1.0 1.0 1.0
  mass 3
end

entity_def sweep_box_11
  type box
  pos 0.0 0.5 6.0
  scale 1.0 1.0 1.0
  mass 3
end

entity_def sweep_box_12
  type box
  pos 0.0 0.5 -6.0
  scale 1.0 1.0 1.0
  mass 3
end

entity_def sweep_box_13
  type box
  pos 6.0 0.5 3.0
  scale 1.0 1.0 1.0
  mass 3
end

entity_def sweep_box_14
  type box
  pos -6.0 0.5 3.0
  scale 1.0 1.0 1.0
  mass 3
end

entity_def sweep_box_15
  type box
  pos 6.0 0.5 -3.0
  scale 1.0 1.0 1.0
  mass 3
end

entity_def sweep_box_16
  type box
  pos -6.0 0.5 -3.0
  scale 1.0 1.0 1.0
  mass 3
end

entity_def sweep_box_17
  type box
  pos 3.0 0.5 6.0
  scale 1.0 1.0 1.0
  mass 3
end

entity_def sweep_box_18
  type box
  pos -3.0 0.5 6.0
  scale 1.0 1.0 1.0
  mass 3
end

entity_def sweep_box_19
  type box
  pos 3.0 0.5 -6.0
  scale 1.0 1.0 1.0
  mass 3
end

# ── Chain 0: root at (5.0, 33.0, 0.0) ──

entity_def root_0
  type sphere
  pos 5.0 63.0 0.0
  scale 0.4 0.4 0.4
  mass 50
  kinematic
end

entity_def cap_0_0
  type capsule
  pos 5.0 62.0 0.0
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_0_1
  type capsule
  pos 5.0 61.0 0.0
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_0_2
  type capsule
  pos 5.0 60.0 0.0
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_0_3
  type capsule
  pos 5.0 59.0 0.0
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_0_4
  type capsule
  pos 5.0 58.0 0.0
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_0_5
  type capsule
  pos 5.0 57.0 0.0
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_0_6
  type capsule
  pos 5.0 56.0 0.0
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_0_7
  type capsule
  pos 5.0 55.0 0.0
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_0_8
  type capsule
  pos 5.0 54.0 0.0
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_0_9
  type capsule
  pos 5.0 53.0 0.0
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_0_10
  type capsule
  pos 5.0 52.0 0.0
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_0_11
  type capsule
  pos 5.0 51.0 0.0
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_0_12
  type capsule
  pos 5.0 50.0 0.0
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_0_13
  type capsule
  pos 5.0 49.0 0.0
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_0_14
  type capsule
  pos 5.0 48.0 0.0
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_0_15
  type capsule
  pos 5.0 47.0 0.0
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_0_16
  type capsule
  pos 5.0 46.0 0.0
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_0_17
  type capsule
  pos 5.0 45.0 0.0
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_0_18
  type capsule
  pos 5.0 44.0 0.0
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_0_19
  type capsule
  pos 5.0 43.0 0.0
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_0_20
  type capsule
  pos 5.0 42.0 0.0
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_0_21
  type capsule
  pos 5.0 41.0 0.0
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_0_22
  type capsule
  pos 5.0 40.0 0.0
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_0_23
  type capsule
  pos 5.0 39.0 0.0
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_0_24
  type capsule
  pos 5.0 38.0 0.0
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_0_25
  type capsule
  pos 5.0 37.0 0.0
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_0_26
  type capsule
  pos 5.0 36.0 0.0
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_0_27
  type capsule
  pos 5.0 35.0 0.0
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_0_28
  type capsule
  pos 5.0 34.0 0.0
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_0_29
  type capsule
  pos 5.0 33.0 0.0
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_0_30
  type capsule
  pos 5.0 32.0 0.0
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_0_31
  type capsule
  pos 5.0 31.0 0.0
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_0_32
  type capsule
  pos 5.0 30.0 0.0
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_0_33
  type capsule
  pos 5.0 29.0 0.0
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_0_34
  type capsule
  pos 5.0 28.0 0.0
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_0_35
  type capsule
  pos 5.0 27.0 0.0
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_0_36
  type capsule
  pos 5.0 26.0 0.0
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_0_37
  type capsule
  pos 5.0 25.0 0.0
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_0_38
  type capsule
  pos 5.0 24.0 0.0
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_0_39
  type capsule
  pos 5.0 23.0 0.0
  scale 0.4 0.6 0.4
  mass 3
end

joint ball root_0 cap_0_0 5.0 62.5 0.0 0 1 0

joint ball cap_0_0 cap_0_1 5.0 61.5 0.0 0 1 0
joint ball cap_0_1 cap_0_2 5.0 60.5 0.0 0 1 0
joint ball cap_0_2 cap_0_3 5.0 59.5 0.0 0 1 0
joint ball cap_0_3 cap_0_4 5.0 58.5 0.0 0 1 0
joint ball cap_0_4 cap_0_5 5.0 57.5 0.0 0 1 0
joint ball cap_0_5 cap_0_6 5.0 56.5 0.0 0 1 0
joint ball cap_0_6 cap_0_7 5.0 55.5 0.0 0 1 0
joint ball cap_0_7 cap_0_8 5.0 54.5 0.0 0 1 0
joint ball cap_0_8 cap_0_9 5.0 53.5 0.0 0 1 0
joint ball cap_0_9 cap_0_10 5.0 52.5 0.0 0 1 0
joint ball cap_0_10 cap_0_11 5.0 51.5 0.0 0 1 0
joint ball cap_0_11 cap_0_12 5.0 50.5 0.0 0 1 0
joint ball cap_0_12 cap_0_13 5.0 49.5 0.0 0 1 0
joint ball cap_0_13 cap_0_14 5.0 48.5 0.0 0 1 0
joint ball cap_0_14 cap_0_15 5.0 47.5 0.0 0 1 0
joint ball cap_0_15 cap_0_16 5.0 46.5 0.0 0 1 0
joint ball cap_0_16 cap_0_17 5.0 45.5 0.0 0 1 0
joint ball cap_0_17 cap_0_18 5.0 44.5 0.0 0 1 0
joint ball cap_0_18 cap_0_19 5.0 43.5 0.0 0 1 0
joint ball cap_0_19 cap_0_20 5.0 42.5 0.0 0 1 0
joint ball cap_0_20 cap_0_21 5.0 41.5 0.0 0 1 0
joint ball cap_0_21 cap_0_22 5.0 40.5 0.0 0 1 0
joint ball cap_0_22 cap_0_23 5.0 39.5 0.0 0 1 0
joint ball cap_0_23 cap_0_24 5.0 38.5 0.0 0 1 0
joint ball cap_0_24 cap_0_25 5.0 37.5 0.0 0 1 0
joint ball cap_0_25 cap_0_26 5.0 36.5 0.0 0 1 0
joint ball cap_0_26 cap_0_27 5.0 35.5 0.0 0 1 0
joint ball cap_0_27 cap_0_28 5.0 34.5 0.0 0 1 0
joint ball cap_0_28 cap_0_29 5.0 33.5 0.0 0 1 0
joint ball cap_0_29 cap_0_30 5.0 32.5 0.0 0 1 0
joint ball cap_0_30 cap_0_31 5.0 31.5 0.0 0 1 0
joint ball cap_0_31 cap_0_32 5.0 30.5 0.0 0 1 0
joint ball cap_0_32 cap_0_33 5.0 29.5 0.0 0 1 0
joint ball cap_0_33 cap_0_34 5.0 28.5 0.0 0 1 0
joint ball cap_0_34 cap_0_35 5.0 27.5 0.0 0 1 0
joint ball cap_0_35 cap_0_36 5.0 26.5 0.0 0 1 0
joint ball cap_0_36 cap_0_37 5.0 25.5 0.0 0 1 0
joint ball cap_0_37 cap_0_38 5.0 24.5 0.0 0 1 0
joint ball cap_0_38 cap_0_39 5.0 23.5 0.0 0 1 0

# ── Chain 1: root at (-2.5, 33.0, 4.3) ──

entity_def root_1
  type sphere
  pos -2.5 63.0 4.3
  scale 0.4 0.4 0.4
  mass 50
  kinematic
end

entity_def cap_1_0
  type capsule
  pos -2.5 62.0 4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_1_1
  type capsule
  pos -2.5 61.0 4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_1_2
  type capsule
  pos -2.5 60.0 4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_1_3
  type capsule
  pos -2.5 59.0 4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_1_4
  type capsule
  pos -2.5 58.0 4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_1_5
  type capsule
  pos -2.5 57.0 4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_1_6
  type capsule
  pos -2.5 56.0 4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_1_7
  type capsule
  pos -2.5 55.0 4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_1_8
  type capsule
  pos -2.5 54.0 4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_1_9
  type capsule
  pos -2.5 53.0 4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_1_10
  type capsule
  pos -2.5 52.0 4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_1_11
  type capsule
  pos -2.5 51.0 4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_1_12
  type capsule
  pos -2.5 50.0 4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_1_13
  type capsule
  pos -2.5 49.0 4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_1_14
  type capsule
  pos -2.5 48.0 4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_1_15
  type capsule
  pos -2.5 47.0 4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_1_16
  type capsule
  pos -2.5 46.0 4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_1_17
  type capsule
  pos -2.5 45.0 4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_1_18
  type capsule
  pos -2.5 44.0 4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_1_19
  type capsule
  pos -2.5 43.0 4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_1_20
  type capsule
  pos -2.5 42.0 4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_1_21
  type capsule
  pos -2.5 41.0 4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_1_22
  type capsule
  pos -2.5 40.0 4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_1_23
  type capsule
  pos -2.5 39.0 4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_1_24
  type capsule
  pos -2.5 38.0 4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_1_25
  type capsule
  pos -2.5 37.0 4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_1_26
  type capsule
  pos -2.5 36.0 4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_1_27
  type capsule
  pos -2.5 35.0 4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_1_28
  type capsule
  pos -2.5 34.0 4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_1_29
  type capsule
  pos -2.5 33.0 4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_1_30
  type capsule
  pos -2.5 32.0 4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_1_31
  type capsule
  pos -2.5 31.0 4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_1_32
  type capsule
  pos -2.5 30.0 4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_1_33
  type capsule
  pos -2.5 29.0 4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_1_34
  type capsule
  pos -2.5 28.0 4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_1_35
  type capsule
  pos -2.5 27.0 4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_1_36
  type capsule
  pos -2.5 26.0 4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_1_37
  type capsule
  pos -2.5 25.0 4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_1_38
  type capsule
  pos -2.5 24.0 4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_1_39
  type capsule
  pos -2.5 23.0 4.3
  scale 0.4 0.6 0.4
  mass 3
end

joint ball root_1 cap_1_0 -2.5 62.5 4.3 0 1 0

joint ball cap_1_0 cap_1_1 -2.5 61.5 4.3 0 1 0
joint ball cap_1_1 cap_1_2 -2.5 60.5 4.3 0 1 0
joint ball cap_1_2 cap_1_3 -2.5 59.5 4.3 0 1 0
joint ball cap_1_3 cap_1_4 -2.5 58.5 4.3 0 1 0
joint ball cap_1_4 cap_1_5 -2.5 57.5 4.3 0 1 0
joint ball cap_1_5 cap_1_6 -2.5 56.5 4.3 0 1 0
joint ball cap_1_6 cap_1_7 -2.5 55.5 4.3 0 1 0
joint ball cap_1_7 cap_1_8 -2.5 54.5 4.3 0 1 0
joint ball cap_1_8 cap_1_9 -2.5 53.5 4.3 0 1 0
joint ball cap_1_9 cap_1_10 -2.5 52.5 4.3 0 1 0
joint ball cap_1_10 cap_1_11 -2.5 51.5 4.3 0 1 0
joint ball cap_1_11 cap_1_12 -2.5 50.5 4.3 0 1 0
joint ball cap_1_12 cap_1_13 -2.5 49.5 4.3 0 1 0
joint ball cap_1_13 cap_1_14 -2.5 48.5 4.3 0 1 0
joint ball cap_1_14 cap_1_15 -2.5 47.5 4.3 0 1 0
joint ball cap_1_15 cap_1_16 -2.5 46.5 4.3 0 1 0
joint ball cap_1_16 cap_1_17 -2.5 45.5 4.3 0 1 0
joint ball cap_1_17 cap_1_18 -2.5 44.5 4.3 0 1 0
joint ball cap_1_18 cap_1_19 -2.5 43.5 4.3 0 1 0
joint ball cap_1_19 cap_1_20 -2.5 42.5 4.3 0 1 0
joint ball cap_1_20 cap_1_21 -2.5 41.5 4.3 0 1 0
joint ball cap_1_21 cap_1_22 -2.5 40.5 4.3 0 1 0
joint ball cap_1_22 cap_1_23 -2.5 39.5 4.3 0 1 0
joint ball cap_1_23 cap_1_24 -2.5 38.5 4.3 0 1 0
joint ball cap_1_24 cap_1_25 -2.5 37.5 4.3 0 1 0
joint ball cap_1_25 cap_1_26 -2.5 36.5 4.3 0 1 0
joint ball cap_1_26 cap_1_27 -2.5 35.5 4.3 0 1 0
joint ball cap_1_27 cap_1_28 -2.5 34.5 4.3 0 1 0
joint ball cap_1_28 cap_1_29 -2.5 33.5 4.3 0 1 0
joint ball cap_1_29 cap_1_30 -2.5 32.5 4.3 0 1 0
joint ball cap_1_30 cap_1_31 -2.5 31.5 4.3 0 1 0
joint ball cap_1_31 cap_1_32 -2.5 30.5 4.3 0 1 0
joint ball cap_1_32 cap_1_33 -2.5 29.5 4.3 0 1 0
joint ball cap_1_33 cap_1_34 -2.5 28.5 4.3 0 1 0
joint ball cap_1_34 cap_1_35 -2.5 27.5 4.3 0 1 0
joint ball cap_1_35 cap_1_36 -2.5 26.5 4.3 0 1 0
joint ball cap_1_36 cap_1_37 -2.5 25.5 4.3 0 1 0
joint ball cap_1_37 cap_1_38 -2.5 24.5 4.3 0 1 0
joint ball cap_1_38 cap_1_39 -2.5 23.5 4.3 0 1 0

# ── Chain 2: root at (-2.5, 33.0, -4.3) ──

entity_def root_2
  type sphere
  pos -2.5 63.0 -4.3
  scale 0.4 0.4 0.4
  mass 50
  kinematic
end

entity_def cap_2_0
  type capsule
  pos -2.5 62.0 -4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_2_1
  type capsule
  pos -2.5 61.0 -4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_2_2
  type capsule
  pos -2.5 60.0 -4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_2_3
  type capsule
  pos -2.5 59.0 -4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_2_4
  type capsule
  pos -2.5 58.0 -4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_2_5
  type capsule
  pos -2.5 57.0 -4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_2_6
  type capsule
  pos -2.5 56.0 -4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_2_7
  type capsule
  pos -2.5 55.0 -4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_2_8
  type capsule
  pos -2.5 54.0 -4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_2_9
  type capsule
  pos -2.5 53.0 -4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_2_10
  type capsule
  pos -2.5 52.0 -4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_2_11
  type capsule
  pos -2.5 51.0 -4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_2_12
  type capsule
  pos -2.5 50.0 -4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_2_13
  type capsule
  pos -2.5 49.0 -4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_2_14
  type capsule
  pos -2.5 48.0 -4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_2_15
  type capsule
  pos -2.5 47.0 -4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_2_16
  type capsule
  pos -2.5 46.0 -4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_2_17
  type capsule
  pos -2.5 45.0 -4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_2_18
  type capsule
  pos -2.5 44.0 -4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_2_19
  type capsule
  pos -2.5 43.0 -4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_2_20
  type capsule
  pos -2.5 42.0 -4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_2_21
  type capsule
  pos -2.5 41.0 -4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_2_22
  type capsule
  pos -2.5 40.0 -4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_2_23
  type capsule
  pos -2.5 39.0 -4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_2_24
  type capsule
  pos -2.5 38.0 -4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_2_25
  type capsule
  pos -2.5 37.0 -4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_2_26
  type capsule
  pos -2.5 36.0 -4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_2_27
  type capsule
  pos -2.5 35.0 -4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_2_28
  type capsule
  pos -2.5 34.0 -4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_2_29
  type capsule
  pos -2.5 33.0 -4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_2_30
  type capsule
  pos -2.5 32.0 -4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_2_31
  type capsule
  pos -2.5 31.0 -4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_2_32
  type capsule
  pos -2.5 30.0 -4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_2_33
  type capsule
  pos -2.5 29.0 -4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_2_34
  type capsule
  pos -2.5 28.0 -4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_2_35
  type capsule
  pos -2.5 27.0 -4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_2_36
  type capsule
  pos -2.5 26.0 -4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_2_37
  type capsule
  pos -2.5 25.0 -4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_2_38
  type capsule
  pos -2.5 24.0 -4.3
  scale 0.4 0.6 0.4
  mass 3
end

entity_def cap_2_39
  type capsule
  pos -2.5 23.0 -4.3
  scale 0.4 0.6 0.4
  mass 3
end

joint ball root_2 cap_2_0 -2.5 62.5 -4.3 0 1 0

joint ball cap_2_0 cap_2_1 -2.5 61.5 -4.3 0 1 0
joint ball cap_2_1 cap_2_2 -2.5 60.5 -4.3 0 1 0
joint ball cap_2_2 cap_2_3 -2.5 59.5 -4.3 0 1 0
joint ball cap_2_3 cap_2_4 -2.5 58.5 -4.3 0 1 0
joint ball cap_2_4 cap_2_5 -2.5 57.5 -4.3 0 1 0
joint ball cap_2_5 cap_2_6 -2.5 56.5 -4.3 0 1 0
joint ball cap_2_6 cap_2_7 -2.5 55.5 -4.3 0 1 0
joint ball cap_2_7 cap_2_8 -2.5 54.5 -4.3 0 1 0
joint ball cap_2_8 cap_2_9 -2.5 53.5 -4.3 0 1 0
joint ball cap_2_9 cap_2_10 -2.5 52.5 -4.3 0 1 0
joint ball cap_2_10 cap_2_11 -2.5 51.5 -4.3 0 1 0
joint ball cap_2_11 cap_2_12 -2.5 50.5 -4.3 0 1 0
joint ball cap_2_12 cap_2_13 -2.5 49.5 -4.3 0 1 0
joint ball cap_2_13 cap_2_14 -2.5 48.5 -4.3 0 1 0
joint ball cap_2_14 cap_2_15 -2.5 47.5 -4.3 0 1 0
joint ball cap_2_15 cap_2_16 -2.5 46.5 -4.3 0 1 0
joint ball cap_2_16 cap_2_17 -2.5 45.5 -4.3 0 1 0
joint ball cap_2_17 cap_2_18 -2.5 44.5 -4.3 0 1 0
joint ball cap_2_18 cap_2_19 -2.5 43.5 -4.3 0 1 0
joint ball cap_2_19 cap_2_20 -2.5 42.5 -4.3 0 1 0
joint ball cap_2_20 cap_2_21 -2.5 41.5 -4.3 0 1 0
joint ball cap_2_21 cap_2_22 -2.5 40.5 -4.3 0 1 0
joint ball cap_2_22 cap_2_23 -2.5 39.5 -4.3 0 1 0
joint ball cap_2_23 cap_2_24 -2.5 38.5 -4.3 0 1 0
joint ball cap_2_24 cap_2_25 -2.5 37.5 -4.3 0 1 0
joint ball cap_2_25 cap_2_26 -2.5 36.5 -4.3 0 1 0
joint ball cap_2_26 cap_2_27 -2.5 35.5 -4.3 0 1 0
joint ball cap_2_27 cap_2_28 -2.5 34.5 -4.3 0 1 0
joint ball cap_2_28 cap_2_29 -2.5 33.5 -4.3 0 1 0
joint ball cap_2_29 cap_2_30 -2.5 32.5 -4.3 0 1 0
joint ball cap_2_30 cap_2_31 -2.5 31.5 -4.3 0 1 0
joint ball cap_2_31 cap_2_32 -2.5 30.5 -4.3 0 1 0
joint ball cap_2_32 cap_2_33 -2.5 29.5 -4.3 0 1 0
joint ball cap_2_33 cap_2_34 -2.5 28.5 -4.3 0 1 0
joint ball cap_2_34 cap_2_35 -2.5 27.5 -4.3 0 1 0
joint ball cap_2_35 cap_2_36 -2.5 26.5 -4.3 0 1 0
joint ball cap_2_36 cap_2_37 -2.5 25.5 -4.3 0 1 0
joint ball cap_2_37 cap_2_38 -2.5 24.5 -4.3 0 1 0
joint ball cap_2_38 cap_2_39 -2.5 23.5 -4.3 0 1 0

# Loose capsules for settling test (y-aligned, dropped from height)
entity_def loose_cap_0
  type capsule
  pos 10.0 35.0 10.0
  rot 15 0 0
  scale 0.3 0.8 0.3
  mass 1.0
  friction 0.6
  restitution 0.0
end

entity_def loose_cap_1
  type capsule
  pos 12.0 35.0 10.0
  rot 0 0 20
  scale 0.3 0.8 0.3
  mass 1.0
  friction 0.6
  restitution 0.0
end

entity_def loose_cap_2
  type capsule
  pos 14.0 35.0 10.0
  rot -10 0 15
  scale 0.3 0.8 0.3
  mass 1.0
  friction 0.6
  restitution 0.0
end

entity_def loose_cap_3
  type capsule
  pos 10.0 35.0 12.0
  rot 20 0 -10
  scale 0.3 0.8 0.3
  mass 1.0
  friction 0.6
  restitution 0.0
end

entity_def loose_cap_4
  type capsule
  pos 12.0 35.0 12.0
  rot -15 0 -20
  scale 0.3 0.8 0.3
  mass 1.0
  friction 0.6
  restitution 0.0
end
