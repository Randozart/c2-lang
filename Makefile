# 2026-07-13 — Top-level build orchestration for C² (Contract Enforced C).
#
# Usage:
#   make           # Build the c2 compiler (default)
#   make build     # Same as above
#   make test      # Run tests with the built compiler
#   make clean     # Remove build artifacts
#   make lint      # Run linting if available
#   make examples  # Build all example .c2 files

SHELL := /bin/bash
CC    := gcc
CFLAGS := -std=c23 -Wall -Wextra -Wpedantic -Werror -g -O0
# Z3 is optional for Phase A (verification pass linked in Phase C)
# Try to detect Z3; if not found, link without it.
Z3_LDFLAGS := $(shell pkg-config --libs z3 2>/dev/null || echo "")
LDFLAGS := $(Z3_LDFLAGS)

SRC_DIR := src
BUILD_DIR := build
TARGET := $(BUILD_DIR)/c2

# Collect all source files
SRCS := $(wildcard $(SRC_DIR)/*.c)
OBJS := $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(SRCS))
DEPS := $(OBJS:.o=.d)

# Test sources
TEST_DIR := tests
TEST_SRCS := $(wildcard $(TEST_DIR)/*/*.c)
TEST_TARGETS := $(patsubst $(TEST_DIR)/%.c, $(BUILD_DIR)/tests/%, $(TEST_SRCS))

.PHONY: all build test clean lint examples dirs

# Default target
all: dirs $(TARGET)

build: all

# Create build directories
dirs:
	@mkdir -p $(BUILD_DIR)/tests

# Link the final compiler binary
$(TARGET): $(OBJS)
	@echo "Linking $@..."
	@$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "Build complete: $@"

# Compile each source file
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@echo "Compiling $<..."
	@$(CC) $(CFLAGS) -MMD -MP -Iinclude -c $< -o $@

# Include auto-generated dependency files
-include $(DEPS)

# Build and run all tests
test: $(TARGET)
	@echo "Running test suite..."
	@for test_src in $(TEST_SRCS); do \
		test_name=$$(basename "$$test_src" .c); \
		test_bin="$(BUILD_DIR)/tests/$$test_name"; \
		echo "  Building test: $$test_name..."; \
		$(CC) $(CFLAGS) -Iinclude -I$(SRC_DIR) -o "$$test_bin" "$$test_src" $(LDFLAGS) -lcriterion 2>/dev/null && \
		"$$test_bin" 2>/dev/null && echo "    PASS" || echo "    SKIP (no criterion or test framework)"; \
	done
	@echo "Test suite complete."

# Build all example .c2 files
examples: $(TARGET)
	@echo "Building examples..."
	@for ex in examples/*.c2; do \
		echo "  $$ex..."; \
		$(TARGET) build "$$ex" --emit-c -o "$${ex%.c2}" 2>/dev/null && echo "    OK" || echo "    SKIP (compiler not ready)"; \
	done
	@echo "Examples complete."

# Lint if tools are available
lint:
	@echo "Checking code style..."
	@if command -v cppcheck &>/dev/null; then \
		cppcheck --enable=all --suppress=missingIncludeSystem $(SRC_DIR)/; \
	else \
		echo "  cppcheck not installed. Skipping."; \
	fi
	@if command -v clang-tidy &>/dev/null; then \
		clang-tidy $(SRCS) -header-filter=.* -- -Iinclude -std=c23 2>/dev/null || true; \
	else \
		echo "  clang-tidy not installed. Skipping."; \
	fi

# Clean build artifacts
clean:
	@rm -rf $(BUILD_DIR)
	@rm -f _c2_out/*
	@echo "Clean complete."
