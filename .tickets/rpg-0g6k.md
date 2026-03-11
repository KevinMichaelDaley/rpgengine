---
id: rpg-0g6k
status: closed
deps: [rpg-tm6g]
links: []
created: 2026-03-11T02:20:04Z
type: task
priority: 2
assignee: KMD
parent: rpg-xgo2
tags: [physics, drivers]
---
# Servo driver (PD controller)

PD controller driving a joint to a target angle or position. Parameters: target_angle/position, kp (proportional gain), kd (derivative gain), max_torque/force. Bias = kp * error + kd * error_rate. Suitable for robotic arms, turrets, steering.

