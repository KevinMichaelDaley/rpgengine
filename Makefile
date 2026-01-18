CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -pthread -Iinclude
LDFLAGS ?= -lm
JOB_SRC := $(wildcard src/job/*.c)
MATH_SRC := $(wildcard src/math/*.c)
MEM_SRC := $(wildcard src/memory/*.c)
ECS_SRC := $(wildcard src/ecs/*.c)
RENDERER_SRC := $(wildcard src/renderer/*.c)
SRC := $(JOB_SRC) $(MATH_SRC) $(MEM_SRC) $(ECS_SRC) $(RENDERER_SRC)
BIN := build/p000_tests build/p001_tests build/p002_tests build/p003_tests build/p004_tests

.PHONY: all test clean

all: $(BIN)

build/p000_tests: $(JOB_SRC) tests/p000_fiber_job_system_tests.c | build
	$(CC) $(CFLAGS) tests/p000_fiber_job_system_tests.c $(JOB_SRC) -o $@ $(LDFLAGS)

build/p001_tests: $(SRC) tests/p001_core_math_tests.c | build
	$(CC) $(CFLAGS) tests/p001_core_math_tests.c $(SRC) -o $@ $(LDFLAGS)

build/p002_tests: $(SRC) tests/p002_memory_tests.c | build
	$(CC) $(CFLAGS) tests/p002_memory_tests.c $(SRC) -o $@ $(LDFLAGS)

build/p003_tests: $(SRC) tests/p003_ecs_tests.c | build
	$(CC) $(CFLAGS) tests/p003_ecs_tests.c $(SRC) -o $@ $(LDFLAGS)

build/p004_tests: $(SRC) tests/p004_renderer_gl_loader_tests.c | build
	$(CC) $(CFLAGS) tests/p004_renderer_gl_loader_tests.c $(SRC) -o $@ $(LDFLAGS)

build:
	@mkdir -p build

test: $(BIN)
	./build/p000_tests && ./build/p001_tests && ./build/p002_tests && ./build/p003_tests && ./build/p004_tests

clean:
	$(RM) $(BIN)
