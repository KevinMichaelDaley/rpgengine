---
id: rpg-8ot1
status: open
deps: [rpg-g169, rpg-d3ue]
links: [rpg-d3ue, rpg-h66f, rpg-zryp, rpg-hezs, rpg-9y61]
created: 2026-03-04T02:56:33Z
type: task
priority: 2
assignee: KMD
---
# Phase 5 visual test: skeletal animation, IK, and ragdoll

End-to-end graphical test for Phase 5 animation system. Renders animated skeletal meshes with blend trees, XPBD-driven IK constraints, and ragdoll transitions to verify the full animation pipeline from clip evaluation through GPU skinning.

Test verifies:
- Animation clip playback: a simple walk cycle loops smoothly
- Blend tree: crossfade between two clips (idle → walk) over 0.5s transition
- Additive animation layer: breathing overlay on top of base pose
- XPBD bone constraints: IK target moves a hand to a world-space position
- Ragdoll activation: mid-animation transition to ragdoll on selected bones
- Per-bone ragdoll: upper body goes ragdoll while legs continue animating
- Bone hierarchy matches entity scene graph (bones as entities)
- Dynamic CCD: fast-moving bone (weapon sweep) does not tunnel through geometry
- Skinning shader: bone palette uploads correct joint matrices per frame

Scene layout: Three characters — (1) walking in a loop with blend transitions, (2) standing with IK hand reaching toward a moving target sphere, (3) standing then transitioning to full ragdoll at t=2s. Ground plane for ragdoll collisions. Output: tests/output/phase5_animation.mp4

File: tests/visual/p004_visual_animation.c
Duration: 5 seconds at 30fps (150 frames) — longer to show ragdoll transition
Exit: PASS if animations play, ragdoll activates, IK reaches target, frame count >= 150, no GL errors.

