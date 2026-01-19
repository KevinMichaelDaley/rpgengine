CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -pthread -Iinclude
LDFLAGS ?= -lm
JOB_SRC := $(wildcard src/job/*.c)
MATH_SRC := $(wildcard src/math/*.c)
MEM_SRC := $(wildcard src/memory/*.c)
ECS_SRC := $(wildcard src/ecs/*.c)
RENDERER_SRC := $(wildcard src/renderer/*.c) $(wildcard src/renderer/skinning/*.c)
SRC := $(JOB_SRC) $(MATH_SRC) $(MEM_SRC) $(ECS_SRC) $(RENDERER_SRC)

SDL2_CFLAGS := $(shell sdl2-config --cflags 2>/dev/null)
SDL2_LIBS := $(shell sdl2-config --libs 2>/dev/null)
GLEW_LIBS := $(shell pkg-config --libs glew 2>/dev/null)
GL_LIBS := -lGL
RENDERER_TEST_CFLAGS := $(SDL2_CFLAGS)
RENDERER_TEST_LIBS := $(SDL2_LIBS) $(GLEW_LIBS) -lSDL2 -lGLEW $(GL_LIBS)

BIN := build/p000_tests build/p001_tests build/p002_tests build/p003_tests \
build/p004_tests build/p004_shader_tests build/p004_buffer_tests \
build/p004_uniform_tests build/p004_palette_tests build/p004_pipeline_tests \
build/p004_skinning_tests build/p004_ecs_skinning_tests

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

build/p004_shader_tests: $(SRC) tests/p004_renderer_shader_tests.c | build
	$(CC) $(CFLAGS) $(RENDERER_TEST_CFLAGS) tests/p004_renderer_shader_tests.c \
$(SRC) -o $@ $(LDFLAGS) $(RENDERER_TEST_LIBS)

build/p004_buffer_tests: $(SRC) tests/p004_renderer_buffer_tests.c | build
	$(CC) $(CFLAGS) $(RENDERER_TEST_CFLAGS) tests/p004_renderer_buffer_tests.c \
$(SRC) -o $@ $(LDFLAGS) $(RENDERER_TEST_LIBS)

build/p004_uniform_tests: $(SRC) tests/p004_renderer_uniform_tests.c | build
	$(CC) $(CFLAGS) $(RENDERER_TEST_CFLAGS) tests/p004_renderer_uniform_tests.c \
$(SRC) -o $@ $(LDFLAGS) $(RENDERER_TEST_LIBS)

build/p004_palette_tests: $(SRC) tests/p004_renderer_palette_tests.c | build
	$(CC) $(CFLAGS) $(RENDERER_TEST_CFLAGS) tests/p004_renderer_palette_tests.c \
$(SRC) -o $@ $(LDFLAGS) $(RENDERER_TEST_LIBS)

build/p004_pipeline_tests: $(SRC) tests/p004_renderer_pipeline_tests.c | build
	$(CC) $(CFLAGS) tests/p004_renderer_pipeline_tests.c $(SRC) -o $@ $(LDFLAGS)

build/p004_skinning_tests: $(SRC) tests/p004_renderer_skinning_tests.c | build
	$(CC) $(CFLAGS) $(RENDERER_TEST_CFLAGS) tests/p004_renderer_skinning_tests.c \
$(SRC) -o $@ $(LDFLAGS) $(RENDERER_TEST_LIBS)

build/p004_ecs_skinning_tests: $(SRC) tests/p004_renderer_ecs_skinning_tests.c | build
	$(CC) $(CFLAGS) $(RENDERER_TEST_CFLAGS) tests/p004_renderer_ecs_skinning_tests.c \
$(SRC) -o $@ $(LDFLAGS) $(RENDERER_TEST_LIBS)

build:
	@mkdir -p build

test: $(BIN)
	./build/p000_tests && ./build/p001_tests && ./build/p002_tests && ./build/p003_tests \
&& ./build/p004_tests && ./build/p004_shader_tests && ./build/p004_buffer_tests \
&& ./build/p004_uniform_tests && ./build/p004_palette_tests && ./build/p004_pipeline_tests \
&& ./build/p004_skinning_tests && ./build/p004_ecs_skinning_tests

clean:
	$(RM) $(BIN)
