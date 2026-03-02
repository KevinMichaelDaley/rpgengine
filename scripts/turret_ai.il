; turret_ai.il — Turret AI script for Aegis.
;
; Subscribes to overlap events on its trigger zone.
; When a player enters the zone:
;   1. Computes direction to player
;   2. Checks line-of-sight via async raycast
;   3. If visible: rotates toward player, signals trace_projectile!
;   4. Exits (re-armed by next overlap event)
;
; Register allocation:
;   r0  = scratch / builder handle
;   r1  = entity count / loop index
;   r2  = entity handle
;   r3  = entity type
;   r4  = player type constant (1 = sphere)
;   r5  = player handle
;   r6  = turret position (vec3)
;   r7  = player position (vec3)
;   r8  = direction vector (player - turret)
;   r9  = distance (float)
;   r10 = range threshold / scratch
;   r11 = comparison / condition result
;   r12 = raycast async handle
;   r13 = raycast result
;   r14 = ray status
;   r15 = topic hash (scratch)
;   r16 = normalized direction
;   r17 = turret entity id (0)
;   r19 = found flag

.topic turret_tick
.fuel 2000

; ── Phase 1: Find player entity ──
load_imm r4 1                  ; EDIT_ENTITY_TYPE_SPHERE (player)
load_imm r5 -1                 ; player handle = not found
load_imm r19 0                 ; found flag = false
entity_count r1                ; r1 = total entities
load_imm r0 0                  ; r0 = loop index

find_loop:
lt r11 r0 r1                   ; index < count?
jmp_if_not r11 no_player
entity_at r2 r0                ; r2 = entity at index
get_attr r3 r2 4               ; r3 = type attribute (key=4)
eq r11 r3 r4                   ; type == SPHERE?
jmp_if r11 found_player
load_imm r11 1
add r0 r0 r11                  ; index++
jmp find_loop

found_player:
mov r5 r2                      ; r5 = player handle
load_imm r19 1                 ; found = true

; ── Phase 2: Get positions, check range ──
load_imm r17 0                 ; turret entity_id = 0
query_entity r2 r17            ; r2 = turret handle
get_attr r6 r2 0               ; r6 = turret pos (key=0 = POS)
get_attr r7 r5 0               ; r7 = player pos
vec3_sub r8 r7 r6              ; r8 = player - turret
vec3_len r9 r8                 ; r9 = distance
load_imm r10 20.0              ; range threshold
gt r11 r9 r10                  ; distance > range?
jmp_if r11 out_of_range

; ── Phase 3: LOS raycast ──
vec3_norm r16 r8               ; r16 = normalized direction
vec3_scale r8 r16 r9           ; r8 = ray vector (dir * distance)
vis_test r12 r6 r8             ; submit async raycast
wait r13 r14 r12               ; wait for result (yields if pending)

; ── Phase 4: Check LOS ──
; r13.f32 = hit distance (-1 = miss = clear LOS)
load_imm r10 0.0
lt r11 r13 r10                 ; hit_dist < 0? (miss = clear)
jmp_if r11 los_clear
lt r11 r13 r9                  ; hit_dist < player_dist? (blocked)
jmp_if r11 los_blocked

los_clear:
; ── Phase 5: Push rotation update ──
build_update r0
target_entity r0 r17           ; target = turret (entity 0)
set_field r0 1 r16             ; key=1 = ROT, value = normalized dir
push_update r0

; ── Phase 6: Signal trace_projectile! ──
load_imm r15 999               ; topic hash for trace_projectile
signal r0 r15 r17              ; signal event
exit r0                        ; exit code 0 = fired

los_blocked:
load_imm r0 2                  ; exit code 2 = LOS blocked
exit r0

out_of_range:
load_imm r0 1                  ; exit code 1 = out of range
exit r0

no_player:
load_imm r0 3                  ; exit code 3 = no player found
exit r0
