---
id: phys-301
status: closed
deps: [phys-117]
links: [phys-300]
created: 2026-02-06T11:09:00.000000000-08:00
type: task
priority: 1
---
# Step 3.1: Physics Job Infrastructure


**Parent Epic:** phys-300 (Phase 3: Parallel Jobs)

## Description

Create the physics-specific job dispatch layer that maps pipeline stages
to the engine's job system.

## Files

- `include/ferrum/physics/phys_jobs.h`
- `src/physics/jobs/phys_job_dispatch.c`

## API

```c
typedef struct phys_job_context_t {
    job_system_t *job_sys;
    job_counter_t counters[PHYS_STAGE_COUNT];
} phys_job_context_t;

void phys_job_context_init(phys_job_context_t *ctx, job_system_t *sys);
void phys_dispatch_stage(phys_job_context_t *ctx, phys_stage_id_t stage,
                         void *args, uint32_t count);
void phys_wait_stage(phys_job_context_t *ctx, phys_stage_id_t stage);
```

## Acceptance Criteria

- [ ] Job context wraps engine job system
- [ ] Stage dispatch splits work into jobs with configurable batch size
- [ ] Wait blocks until all jobs for a stage complete

