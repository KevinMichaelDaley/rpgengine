; chain_whip_spin.il — Orbit kinematic root nodes with increasing speed.
;
; Infinite loop relying on fuel-based force-yield. Each iteration:
; reads clock, computes omega, theta, lowers Y height over time,
; sets position + linear + angular velocity for 3 root nodes.
;
; Root entity IDs: root_0=21, root_1=62, root_2=103
; Orbit radius: starts at 2.0, grows by 0.2/s, caps at 8.0
; Y height: starts at 63.0, drops by 3.0/s, clamps at 18.0
; Omega: 0.8 * t (angular velocity ramps up over time)

.topic tick
.fuel 500

; ── Constants ──
load_imm r40 0.8       ; omega multiplier (was 0.5)
load_imm r41 0.4       ; theta multiplier (0.5 * omega_mult)
load_imm r42 2.0       ; initial orbit radius
load_imm r43 0.0       ; zero
load_imm r44 2.0944    ; 2*pi/3 phase offset
load_imm r45 4.1888    ; 4*pi/3 phase offset
load_imm r46 63.0      ; initial Y height
load_imm r47 3.0       ; Y descent rate (units/s)
load_imm r48 18.0      ; minimum Y height
load_imm r49 0.2       ; radius growth rate (units/s)
load_imm r53 8.0       ; maximum orbit radius
load_imm r50 41        ; root_0 entity ID
load_imm r51 82        ; root_1 entity ID
load_imm r52 123       ; root_2 entity ID

loop_top:

; ── Read simulation time ──
clock r1

; ── omega = 0.8 * t ──
fmul r2 r40 r1

; ── radius = 2.0 + 0.2 * t, clamped to 8.0 ──
fmul r7 r49 r1
fadd r7 r42 r7
fgt r6 r7 r53
jmp_if r6 clamp_r
jmp after_clamp_r
clamp_r:
mov r7 r53
after_clamp_r:

; ── theta = 0.4 * t^2 ──
fmul r3 r1 r1
fmul r3 r41 r3

; ── Compute Y height: y = 63.0 - 3.0 * t, clamp to 18.0 ──
fmul r5 r47 r1
fsub r5 r46 r5
flt r6 r5 r48
jmp_if r6 clamp_y
jmp after_clamp
clamp_y:
mov r5 r48
after_clamp:

; ── Angular velocity vec3 (0, omega, 0) ──
mov r30 r43
mov r31 r2
mov r32 r43
vec3_pack r60 r30

; ── Root 0: phase = 0 ──
; Position: (radius*sin(theta), y, radius*cos(theta))
sin r10 r3
cos r11 r3
fmul r12 r10 r7
fmul r13 r11 r7
mov r30 r12
mov r31 r5
mov r32 r13
vec3_pack r62 r30

; Velocity: tangential = omega * radius in (-cos, 0, sin) direction
fmul r10 r10 r2
fmul r10 r10 r7
fneg r10 r10
fmul r11 r11 r2
fmul r11 r11 r7
mov r30 r10
mov r31 r43
mov r32 r11
vec3_pack r61 r30

; Push position, angular vel, linear vel for root_0
build_update r0
target_entity r0 r50
set_field r0 0 r62
push_update r0
build_update r0
target_entity r0 r50
set_field r0 10 r60
push_update r0
build_update r0
target_entity r0 r50
set_field r0 11 r61
push_update r0

; ── Root 1: phase = 2*pi/3 ──
fadd r4 r3 r44
sin r10 r4
cos r11 r4
fmul r12 r10 r7
fmul r13 r11 r7
mov r30 r12
mov r31 r5
mov r32 r13
vec3_pack r62 r30

fmul r10 r10 r2
fmul r10 r10 r7
fneg r10 r10
fmul r11 r11 r2
fmul r11 r11 r7
mov r30 r10
mov r31 r43
mov r32 r11
vec3_pack r61 r30

build_update r0
target_entity r0 r51
set_field r0 0 r62
push_update r0
build_update r0
target_entity r0 r51
set_field r0 10 r60
push_update r0
build_update r0
target_entity r0 r51
set_field r0 11 r61
push_update r0

; ── Root 2: phase = 4*pi/3 ──
fadd r4 r3 r45
sin r10 r4
cos r11 r4
fmul r12 r10 r7
fmul r13 r11 r7
mov r30 r12
mov r31 r5
mov r32 r13
vec3_pack r62 r30

fmul r10 r10 r2
fmul r10 r10 r7
fneg r10 r10
fmul r11 r11 r2
fmul r11 r11 r7
mov r30 r10
mov r31 r43
mov r32 r11
vec3_pack r61 r30

build_update r0
target_entity r0 r52
set_field r0 0 r62
push_update r0
build_update r0
target_entity r0 r52
set_field r0 10 r60
push_update r0
build_update r0
target_entity r0 r52
set_field r0 11 r61
push_update r0

; Force-yield will interrupt here; runtime refuels and resumes.
jmp loop_top
