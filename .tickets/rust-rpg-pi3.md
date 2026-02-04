---
id: rust-rpg-pi3
status: closed
deps: [rust-rpg-ml2]
links: []
created: 2026-02-01T15:02:30.066545872-08:00
type: task
priority: 2
---
# P_008: Headless perf + accuracy harness

Create a comprehensive headless integration/performance test harness that exercises all networking features implemented so far.

Scope
- Launch/drive N simulated clients (or multiple processes) that:
  - reliable JOIN/SPAWN
  - receive unreliable STATE updates for (N-1) remote cubes
  - track accuracy: positional/orientation error vs server truth (bounds based on quantization)
- Measure performance: tick time, encode/decode time, packets/sec, bytes/sec, end-to-end latency estimate.
- Use job system to simulate multiple network jobs for serialization/validation.

Deliverables
- One or more test executables under tests/ (e.g. p008_net_perf_server_tests + p008_net_perf_client_tests).
- Command-line flags for N clients, duration, tick rate, packet loss/jitter profiles (reuse net/test/link if possible).
- README with two-machine instructions.

Acceptance
- Can run on two hosts; produces a deterministic summary report; exits non-zero if accuracy/perf thresholds violated.



