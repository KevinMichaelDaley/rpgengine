# chain_whip.cmd — Chain whip stress test
# 3 chains of 40 capsules, ball-jointed tip-to-tail.
# Root nodes are kinematic spheres orbiting at radius 5.
# Root y=33.0, chain bottom ~0.8m above ground.
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

# ── Chain 0: root at (5.0, 33.0, 0.0) ──

entity_def root_0
  type sphere
  pos 5.0 33.0 0.0
  scale 0.4 0.4 0.4
  mass 50
  kinematic
end

entity_def cap_0_0
  type capsule
  pos 5.0 32.4 0.0
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_0_1
  type capsule
  pos 5.0 31.6 0.0
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_0_2
  type capsule
  pos 5.0 30.8 0.0
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_0_3
  type capsule
  pos 5.0 30.0 0.0
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_0_4
  type capsule
  pos 5.0 29.2 0.0
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_0_5
  type capsule
  pos 5.0 28.4 0.0
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_0_6
  type capsule
  pos 5.0 27.6 0.0
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_0_7
  type capsule
  pos 5.0 26.8 0.0
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_0_8
  type capsule
  pos 5.0 26.0 0.0
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_0_9
  type capsule
  pos 5.0 25.2 0.0
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_0_10
  type capsule
  pos 5.0 24.4 0.0
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_0_11
  type capsule
  pos 5.0 23.6 0.0
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_0_12
  type capsule
  pos 5.0 22.8 0.0
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_0_13
  type capsule
  pos 5.0 22.0 0.0
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_0_14
  type capsule
  pos 5.0 21.2 0.0
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_0_15
  type capsule
  pos 5.0 20.4 0.0
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_0_16
  type capsule
  pos 5.0 19.6 0.0
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_0_17
  type capsule
  pos 5.0 18.8 0.0
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_0_18
  type capsule
  pos 5.0 18.0 0.0
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_0_19
  type capsule
  pos 5.0 17.2 0.0
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_0_20
  type capsule
  pos 5.0 16.4 0.0
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_0_21
  type capsule
  pos 5.0 15.6 0.0
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_0_22
  type capsule
  pos 5.0 14.8 0.0
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_0_23
  type capsule
  pos 5.0 14.0 0.0
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_0_24
  type capsule
  pos 5.0 13.2 0.0
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_0_25
  type capsule
  pos 5.0 12.4 0.0
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_0_26
  type capsule
  pos 5.0 11.6 0.0
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_0_27
  type capsule
  pos 5.0 10.8 0.0
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_0_28
  type capsule
  pos 5.0 10.0 0.0
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_0_29
  type capsule
  pos 5.0 9.2 0.0
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_0_30
  type capsule
  pos 5.0 8.4 0.0
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_0_31
  type capsule
  pos 5.0 7.6 0.0
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_0_32
  type capsule
  pos 5.0 6.8 0.0
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_0_33
  type capsule
  pos 5.0 6.0 0.0
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_0_34
  type capsule
  pos 5.0 5.2 0.0
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_0_35
  type capsule
  pos 5.0 4.4 0.0
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_0_36
  type capsule
  pos 5.0 3.6 0.0
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_0_37
  type capsule
  pos 5.0 2.8 0.0
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_0_38
  type capsule
  pos 5.0 2.0 0.0
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_0_39
  type capsule
  pos 5.0 1.2 0.0
  scale 0.2 0.6 0.2
  mass 0.5
end

joint ball root_0 cap_0_0 5.0 32.7 0.0 0 1 0

joint ball cap_0_0 cap_0_1 5.0 32.0 0.0 0 1 0
joint ball cap_0_1 cap_0_2 5.0 31.2 0.0 0 1 0
joint ball cap_0_2 cap_0_3 5.0 30.4 0.0 0 1 0
joint ball cap_0_3 cap_0_4 5.0 29.6 0.0 0 1 0
joint ball cap_0_4 cap_0_5 5.0 28.8 0.0 0 1 0
joint ball cap_0_5 cap_0_6 5.0 28.0 0.0 0 1 0
joint ball cap_0_6 cap_0_7 5.0 27.2 0.0 0 1 0
joint ball cap_0_7 cap_0_8 5.0 26.4 0.0 0 1 0
joint ball cap_0_8 cap_0_9 5.0 25.6 0.0 0 1 0
joint ball cap_0_9 cap_0_10 5.0 24.8 0.0 0 1 0
joint ball cap_0_10 cap_0_11 5.0 24.0 0.0 0 1 0
joint ball cap_0_11 cap_0_12 5.0 23.2 0.0 0 1 0
joint ball cap_0_12 cap_0_13 5.0 22.4 0.0 0 1 0
joint ball cap_0_13 cap_0_14 5.0 21.6 0.0 0 1 0
joint ball cap_0_14 cap_0_15 5.0 20.8 0.0 0 1 0
joint ball cap_0_15 cap_0_16 5.0 20.0 0.0 0 1 0
joint ball cap_0_16 cap_0_17 5.0 19.2 0.0 0 1 0
joint ball cap_0_17 cap_0_18 5.0 18.4 0.0 0 1 0
joint ball cap_0_18 cap_0_19 5.0 17.6 0.0 0 1 0
joint ball cap_0_19 cap_0_20 5.0 16.8 0.0 0 1 0
joint ball cap_0_20 cap_0_21 5.0 16.0 0.0 0 1 0
joint ball cap_0_21 cap_0_22 5.0 15.2 0.0 0 1 0
joint ball cap_0_22 cap_0_23 5.0 14.4 0.0 0 1 0
joint ball cap_0_23 cap_0_24 5.0 13.6 0.0 0 1 0
joint ball cap_0_24 cap_0_25 5.0 12.8 0.0 0 1 0
joint ball cap_0_25 cap_0_26 5.0 12.0 0.0 0 1 0
joint ball cap_0_26 cap_0_27 5.0 11.2 0.0 0 1 0
joint ball cap_0_27 cap_0_28 5.0 10.4 0.0 0 1 0
joint ball cap_0_28 cap_0_29 5.0 9.6 0.0 0 1 0
joint ball cap_0_29 cap_0_30 5.0 8.8 0.0 0 1 0
joint ball cap_0_30 cap_0_31 5.0 8.0 0.0 0 1 0
joint ball cap_0_31 cap_0_32 5.0 7.2 0.0 0 1 0
joint ball cap_0_32 cap_0_33 5.0 6.4 0.0 0 1 0
joint ball cap_0_33 cap_0_34 5.0 5.6 0.0 0 1 0
joint ball cap_0_34 cap_0_35 5.0 4.8 0.0 0 1 0
joint ball cap_0_35 cap_0_36 5.0 4.0 0.0 0 1 0
joint ball cap_0_36 cap_0_37 5.0 3.2 0.0 0 1 0
joint ball cap_0_37 cap_0_38 5.0 2.4 0.0 0 1 0
joint ball cap_0_38 cap_0_39 5.0 1.6 0.0 0 1 0

# ── Chain 1: root at (-2.5, 33.0, 4.3) ──

entity_def root_1
  type sphere
  pos -2.5 33.0 4.3
  scale 0.4 0.4 0.4
  mass 50
  kinematic
end

entity_def cap_1_0
  type capsule
  pos -2.5 32.4 4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_1_1
  type capsule
  pos -2.5 31.6 4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_1_2
  type capsule
  pos -2.5 30.8 4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_1_3
  type capsule
  pos -2.5 30.0 4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_1_4
  type capsule
  pos -2.5 29.2 4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_1_5
  type capsule
  pos -2.5 28.4 4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_1_6
  type capsule
  pos -2.5 27.6 4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_1_7
  type capsule
  pos -2.5 26.8 4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_1_8
  type capsule
  pos -2.5 26.0 4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_1_9
  type capsule
  pos -2.5 25.2 4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_1_10
  type capsule
  pos -2.5 24.4 4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_1_11
  type capsule
  pos -2.5 23.6 4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_1_12
  type capsule
  pos -2.5 22.8 4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_1_13
  type capsule
  pos -2.5 22.0 4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_1_14
  type capsule
  pos -2.5 21.2 4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_1_15
  type capsule
  pos -2.5 20.4 4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_1_16
  type capsule
  pos -2.5 19.6 4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_1_17
  type capsule
  pos -2.5 18.8 4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_1_18
  type capsule
  pos -2.5 18.0 4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_1_19
  type capsule
  pos -2.5 17.2 4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_1_20
  type capsule
  pos -2.5 16.4 4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_1_21
  type capsule
  pos -2.5 15.6 4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_1_22
  type capsule
  pos -2.5 14.8 4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_1_23
  type capsule
  pos -2.5 14.0 4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_1_24
  type capsule
  pos -2.5 13.2 4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_1_25
  type capsule
  pos -2.5 12.4 4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_1_26
  type capsule
  pos -2.5 11.6 4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_1_27
  type capsule
  pos -2.5 10.8 4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_1_28
  type capsule
  pos -2.5 10.0 4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_1_29
  type capsule
  pos -2.5 9.2 4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_1_30
  type capsule
  pos -2.5 8.4 4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_1_31
  type capsule
  pos -2.5 7.6 4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_1_32
  type capsule
  pos -2.5 6.8 4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_1_33
  type capsule
  pos -2.5 6.0 4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_1_34
  type capsule
  pos -2.5 5.2 4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_1_35
  type capsule
  pos -2.5 4.4 4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_1_36
  type capsule
  pos -2.5 3.6 4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_1_37
  type capsule
  pos -2.5 2.8 4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_1_38
  type capsule
  pos -2.5 2.0 4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_1_39
  type capsule
  pos -2.5 1.2 4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

joint ball root_1 cap_1_0 -2.5 32.7 4.3 0 1 0

joint ball cap_1_0 cap_1_1 -2.5 32.0 4.3 0 1 0
joint ball cap_1_1 cap_1_2 -2.5 31.2 4.3 0 1 0
joint ball cap_1_2 cap_1_3 -2.5 30.4 4.3 0 1 0
joint ball cap_1_3 cap_1_4 -2.5 29.6 4.3 0 1 0
joint ball cap_1_4 cap_1_5 -2.5 28.8 4.3 0 1 0
joint ball cap_1_5 cap_1_6 -2.5 28.0 4.3 0 1 0
joint ball cap_1_6 cap_1_7 -2.5 27.2 4.3 0 1 0
joint ball cap_1_7 cap_1_8 -2.5 26.4 4.3 0 1 0
joint ball cap_1_8 cap_1_9 -2.5 25.6 4.3 0 1 0
joint ball cap_1_9 cap_1_10 -2.5 24.8 4.3 0 1 0
joint ball cap_1_10 cap_1_11 -2.5 24.0 4.3 0 1 0
joint ball cap_1_11 cap_1_12 -2.5 23.2 4.3 0 1 0
joint ball cap_1_12 cap_1_13 -2.5 22.4 4.3 0 1 0
joint ball cap_1_13 cap_1_14 -2.5 21.6 4.3 0 1 0
joint ball cap_1_14 cap_1_15 -2.5 20.8 4.3 0 1 0
joint ball cap_1_15 cap_1_16 -2.5 20.0 4.3 0 1 0
joint ball cap_1_16 cap_1_17 -2.5 19.2 4.3 0 1 0
joint ball cap_1_17 cap_1_18 -2.5 18.4 4.3 0 1 0
joint ball cap_1_18 cap_1_19 -2.5 17.6 4.3 0 1 0
joint ball cap_1_19 cap_1_20 -2.5 16.8 4.3 0 1 0
joint ball cap_1_20 cap_1_21 -2.5 16.0 4.3 0 1 0
joint ball cap_1_21 cap_1_22 -2.5 15.2 4.3 0 1 0
joint ball cap_1_22 cap_1_23 -2.5 14.4 4.3 0 1 0
joint ball cap_1_23 cap_1_24 -2.5 13.6 4.3 0 1 0
joint ball cap_1_24 cap_1_25 -2.5 12.8 4.3 0 1 0
joint ball cap_1_25 cap_1_26 -2.5 12.0 4.3 0 1 0
joint ball cap_1_26 cap_1_27 -2.5 11.2 4.3 0 1 0
joint ball cap_1_27 cap_1_28 -2.5 10.4 4.3 0 1 0
joint ball cap_1_28 cap_1_29 -2.5 9.6 4.3 0 1 0
joint ball cap_1_29 cap_1_30 -2.5 8.8 4.3 0 1 0
joint ball cap_1_30 cap_1_31 -2.5 8.0 4.3 0 1 0
joint ball cap_1_31 cap_1_32 -2.5 7.2 4.3 0 1 0
joint ball cap_1_32 cap_1_33 -2.5 6.4 4.3 0 1 0
joint ball cap_1_33 cap_1_34 -2.5 5.6 4.3 0 1 0
joint ball cap_1_34 cap_1_35 -2.5 4.8 4.3 0 1 0
joint ball cap_1_35 cap_1_36 -2.5 4.0 4.3 0 1 0
joint ball cap_1_36 cap_1_37 -2.5 3.2 4.3 0 1 0
joint ball cap_1_37 cap_1_38 -2.5 2.4 4.3 0 1 0
joint ball cap_1_38 cap_1_39 -2.5 1.6 4.3 0 1 0

# ── Chain 2: root at (-2.5, 33.0, -4.3) ──

entity_def root_2
  type sphere
  pos -2.5 33.0 -4.3
  scale 0.4 0.4 0.4
  mass 50
  kinematic
end

entity_def cap_2_0
  type capsule
  pos -2.5 32.4 -4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_2_1
  type capsule
  pos -2.5 31.6 -4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_2_2
  type capsule
  pos -2.5 30.8 -4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_2_3
  type capsule
  pos -2.5 30.0 -4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_2_4
  type capsule
  pos -2.5 29.2 -4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_2_5
  type capsule
  pos -2.5 28.4 -4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_2_6
  type capsule
  pos -2.5 27.6 -4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_2_7
  type capsule
  pos -2.5 26.8 -4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_2_8
  type capsule
  pos -2.5 26.0 -4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_2_9
  type capsule
  pos -2.5 25.2 -4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_2_10
  type capsule
  pos -2.5 24.4 -4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_2_11
  type capsule
  pos -2.5 23.6 -4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_2_12
  type capsule
  pos -2.5 22.8 -4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_2_13
  type capsule
  pos -2.5 22.0 -4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_2_14
  type capsule
  pos -2.5 21.2 -4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_2_15
  type capsule
  pos -2.5 20.4 -4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_2_16
  type capsule
  pos -2.5 19.6 -4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_2_17
  type capsule
  pos -2.5 18.8 -4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_2_18
  type capsule
  pos -2.5 18.0 -4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_2_19
  type capsule
  pos -2.5 17.2 -4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_2_20
  type capsule
  pos -2.5 16.4 -4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_2_21
  type capsule
  pos -2.5 15.6 -4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_2_22
  type capsule
  pos -2.5 14.8 -4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_2_23
  type capsule
  pos -2.5 14.0 -4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_2_24
  type capsule
  pos -2.5 13.2 -4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_2_25
  type capsule
  pos -2.5 12.4 -4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_2_26
  type capsule
  pos -2.5 11.6 -4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_2_27
  type capsule
  pos -2.5 10.8 -4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_2_28
  type capsule
  pos -2.5 10.0 -4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_2_29
  type capsule
  pos -2.5 9.2 -4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_2_30
  type capsule
  pos -2.5 8.4 -4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_2_31
  type capsule
  pos -2.5 7.6 -4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_2_32
  type capsule
  pos -2.5 6.8 -4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_2_33
  type capsule
  pos -2.5 6.0 -4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_2_34
  type capsule
  pos -2.5 5.2 -4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_2_35
  type capsule
  pos -2.5 4.4 -4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_2_36
  type capsule
  pos -2.5 3.6 -4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_2_37
  type capsule
  pos -2.5 2.8 -4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_2_38
  type capsule
  pos -2.5 2.0 -4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

entity_def cap_2_39
  type capsule
  pos -2.5 1.2 -4.3
  scale 0.2 0.6 0.2
  mass 0.5
end

joint ball root_2 cap_2_0 -2.5 32.7 -4.3 0 1 0

joint ball cap_2_0 cap_2_1 -2.5 32.0 -4.3 0 1 0
joint ball cap_2_1 cap_2_2 -2.5 31.2 -4.3 0 1 0
joint ball cap_2_2 cap_2_3 -2.5 30.4 -4.3 0 1 0
joint ball cap_2_3 cap_2_4 -2.5 29.6 -4.3 0 1 0
joint ball cap_2_4 cap_2_5 -2.5 28.8 -4.3 0 1 0
joint ball cap_2_5 cap_2_6 -2.5 28.0 -4.3 0 1 0
joint ball cap_2_6 cap_2_7 -2.5 27.2 -4.3 0 1 0
joint ball cap_2_7 cap_2_8 -2.5 26.4 -4.3 0 1 0
joint ball cap_2_8 cap_2_9 -2.5 25.6 -4.3 0 1 0
joint ball cap_2_9 cap_2_10 -2.5 24.8 -4.3 0 1 0
joint ball cap_2_10 cap_2_11 -2.5 24.0 -4.3 0 1 0
joint ball cap_2_11 cap_2_12 -2.5 23.2 -4.3 0 1 0
joint ball cap_2_12 cap_2_13 -2.5 22.4 -4.3 0 1 0
joint ball cap_2_13 cap_2_14 -2.5 21.6 -4.3 0 1 0
joint ball cap_2_14 cap_2_15 -2.5 20.8 -4.3 0 1 0
joint ball cap_2_15 cap_2_16 -2.5 20.0 -4.3 0 1 0
joint ball cap_2_16 cap_2_17 -2.5 19.2 -4.3 0 1 0
joint ball cap_2_17 cap_2_18 -2.5 18.4 -4.3 0 1 0
joint ball cap_2_18 cap_2_19 -2.5 17.6 -4.3 0 1 0
joint ball cap_2_19 cap_2_20 -2.5 16.8 -4.3 0 1 0
joint ball cap_2_20 cap_2_21 -2.5 16.0 -4.3 0 1 0
joint ball cap_2_21 cap_2_22 -2.5 15.2 -4.3 0 1 0
joint ball cap_2_22 cap_2_23 -2.5 14.4 -4.3 0 1 0
joint ball cap_2_23 cap_2_24 -2.5 13.6 -4.3 0 1 0
joint ball cap_2_24 cap_2_25 -2.5 12.8 -4.3 0 1 0
joint ball cap_2_25 cap_2_26 -2.5 12.0 -4.3 0 1 0
joint ball cap_2_26 cap_2_27 -2.5 11.2 -4.3 0 1 0
joint ball cap_2_27 cap_2_28 -2.5 10.4 -4.3 0 1 0
joint ball cap_2_28 cap_2_29 -2.5 9.6 -4.3 0 1 0
joint ball cap_2_29 cap_2_30 -2.5 8.8 -4.3 0 1 0
joint ball cap_2_30 cap_2_31 -2.5 8.0 -4.3 0 1 0
joint ball cap_2_31 cap_2_32 -2.5 7.2 -4.3 0 1 0
joint ball cap_2_32 cap_2_33 -2.5 6.4 -4.3 0 1 0
joint ball cap_2_33 cap_2_34 -2.5 5.6 -4.3 0 1 0
joint ball cap_2_34 cap_2_35 -2.5 4.8 -4.3 0 1 0
joint ball cap_2_35 cap_2_36 -2.5 4.0 -4.3 0 1 0
joint ball cap_2_36 cap_2_37 -2.5 3.2 -4.3 0 1 0
joint ball cap_2_37 cap_2_38 -2.5 2.4 -4.3 0 1 0
joint ball cap_2_38 cap_2_39 -2.5 1.6 -4.3 0 1 0

# Loose capsules for settling test (y-aligned, dropped from height)
entity_def loose_cap_0
  type capsule
  pos 10.0 5.0 10.0
  rot 15 0 0
  scale 0.3 0.8 0.3
  mass 1.0
  friction 0.6
  restitution 0.0
end

entity_def loose_cap_1
  type capsule
  pos 12.0 5.0 10.0
  rot 0 0 20
  scale 0.3 0.8 0.3
  mass 1.0
  friction 0.6
  restitution 0.0
end

entity_def loose_cap_2
  type capsule
  pos 14.0 5.0 10.0
  rot -10 0 15
  scale 0.3 0.8 0.3
  mass 1.0
  friction 0.6
  restitution 0.0
end

entity_def loose_cap_3
  type capsule
  pos 10.0 5.0 12.0
  rot 20 0 -10
  scale 0.3 0.8 0.3
  mass 1.0
  friction 0.6
  restitution 0.0
end

entity_def loose_cap_4
  type capsule
  pos 12.0 5.0 12.0
  rot -15 0 -20
  scale 0.3 0.8 0.3
  mass 1.0
  friction 0.6
  restitution 0.0
end
