CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -pthread -Iinclude
LDFLAGS ?= -lm
JOB_SRC := $(wildcard src/job/*.c)
MATH_SRC := $(wildcard src/math/*.c)
SRC := $(JOB_SRC) $(MATH_SRC)
BIN := build/p000_tests build/p001_tests

.PHONY: all test clean

all: $(BIN)

build/p000_tests: $(JOB_SRC) tests/p000_fiber_job_system_tests.c | build
	$(CC) $(CFLAGS) tests/p000_fiber_job_system_tests.c $(JOB_SRC) -o $@ $(LDFLAGS)

build/p001_tests: $(SRC) tests/p001_core_math_tests.c | build
	$(CC) $(CFLAGS) tests/p001_core_math_tests.c $(SRC) -o $@ $(LDFLAGS)

build:
	@mkdir -p build

test: $(BIN)
	./build/p000_tests && ./build/p001_tests

clean:
	$(RM) $(BIN)
