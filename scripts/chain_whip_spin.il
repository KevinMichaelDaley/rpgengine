; chain_whip_spin.il — Orbit kinematic root nodes with increasing speed.
;
; Infinite loop relying on fuel-based force-yield. Each iteration:
; reads clock, computes omega = 0.5 * t, theta = 0.25 * t^2,
; sets linear + angular velocity for 3 root nodes orbiting Y axis.
;
; Root entity IDs: root_0=21, root_1=62, root_2=103

.topic tick
.fuel 500

; ── Constants (loaded once) ──
load_imm r40 0.5
load_imm r41 0.25
load_imm r42 5.0
load_imm r43 0.0
load_imm r44 2.0944
load_imm r45 4.1888
load_imm r50 21
load_imm r51 62
load_imm r52 103

loop_top:

; ── Read simulation time ──
clock r1

; ── omega = 0.5 * t ──
fmul r2 r40 r1

; ── theta = 0.25 * t^2 ──
fmul r3 r1 r1
fmul r3 r41 r3

; ── Angular velocity vec3 (0, omega, 0) ──
mov r30 r43
mov r31 r2
mov r32 r43
vec3_pack r60 r30

; ── Root 0: phase = 0 ──
mov r4 r3
sin r10 r4
cos r11 r4
fmul r10 r10 r2
fmul r10 r10 r42
fneg r10 r10
fmul r11 r11 r2
fmul r11 r11 r42
mov r30 r10
mov r31 r43
mov r32 r11
vec3_pack r61 r30

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
fmul r10 r10 r2
fmul r10 r10 r42
fneg r10 r10
fmul r11 r11 r2
fmul r11 r11 r42
mov r30 r10
mov r31 r43
mov r32 r11
vec3_pack r61 r30

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
fmul r10 r10 r2
fmul r10 r10 r42
fneg r10 r10
fmul r11 r11 r2
fmul r11 r11 r42
mov r30 r10
mov r31 r43
mov r32 r11
vec3_pack r61 r30

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
