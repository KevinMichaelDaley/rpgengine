---
id: rpg-8je5
status: closed
deps: []
links: []
created: 2026-07-15T08:30:01Z
type: task
priority: 1
assignee: KMD
parent: rpg-tjtp
---
# Pack the SVO into a GPU (SSBO) representation for the gather


## Notes

**2026-07-15T08:38:03Z**

Host-side packing landed: lm_gpu_pack (lm_gpu_node 64B / lm_gpu_luxel 32B / lm_gpu_light 64B / lm_gpu_params) with a query-or-fill capacity contract, no allocation, no GL, full unit tests. The actual glBufferData SSBO upload of these packed arrays is a one-liner that belongs to the compute-context module (rpg-k4lk).
