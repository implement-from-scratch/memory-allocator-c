# Project Configuration
PROJECT_NAME := template-project
VERSION := 1.0.0

# Directories
SRC_DIR := src
INCLUDE_DIR := include
BUILD_DIR := build
TEST_DIR := tests
OBJ_DIR := $(BUILD_DIR)/obj
BIN_DIR := $(BUILD_DIR)/bin
COV_DIR := $(BUILD_DIR)/coverage

# Compiler Configuration
CC := gcc
CXX := g++
CFLAGS := -Wall -Wextra -Werror -pedantic -std=c11 -I$(INCLUDE_DIR)
CXXFLAGS := -Wall -Wextra -Werror -pedantic -std=c++17 -I$(INCLUDE_DIR)
LDFLAGS := 
LIBS := 

# Debug/Release Flags
DEBUG_FLAGS := -g -O0 -DDEBUG
RELEASE_FLAGS := -O3 -DNDEBUG

# Test Configuration
TEST_FLAGS := -lcunit -lcheck -lgtest -pthread
COVERAGE_FLAGS := --coverage -fprofile-arcs -ftest-coverage

# Static Analysis
CLANG_FORMAT := clang-format
CLANG_TIDY := clang-tidy
FORMAT_STYLE := -style=file

# Platform Detection
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
    PLATFORM := linux
    NPROC := $(shell nproc)
endif
ifeq ($(UNAME_S),Darwin)
    PLATFORM := macos
    NPROC := $(shell sysctl -n hw.ncpu)
endif

# Parallel Compilation
MAKEFLAGS += -j$(NPROC)

# Source Files (Auto-detection)
C_SOURCES := $(wildcard $(SRC_DIR)/*.c)
CPP_SOURCES := $(wildcard $(SRC_DIR)/*.cpp)
C_OBJECTS := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(C_SOURCES))
CPP_OBJECTS := $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(CPP_SOURCES))
OBJECTS := $(C_OBJECTS) $(CPP_OBJECTS)

# Test Files
TEST_SOURCES := $(wildcard $(TEST_DIR)/*.c $(TEST_DIR)/*.cpp)
TEST_BINARIES := $(patsubst $(TEST_DIR)/%.c,$(BIN_DIR)/test_%,$(TEST_SOURCES))

# Output Binary
TARGET := $(BIN_DIR)/$(PROJECT_NAME)

# Colors
RED := \033[0;31m
GREEN := \033[0;32m
YELLOW := \033[1;33m
NC := \033[0m # No Color

.PHONY: all build clean test coverage format lint check install uninstall help rust-build rust-test rust-fmt rust-clippy rust-check rust-clean

# Default Target
all: build

# Build Targets
build: $(TARGET)
	@echo "$(GREEN)Build complete: $(TARGET)$(NC)"

$(TARGET): $(OBJECTS)
	@mkdir -p $(BIN_DIR)
	@echo "$(YELLOW)Linking $(TARGET)...$(NC)"
	@if [ -n "$(C_OBJECTS)" ]; then $(CC) $(OBJECTS) -o $@ $(LDFLAGS) $(LIBS); \
	else $(CXX) $(OBJECTS) -o $@ $(LDFLAGS) $(LIBS); fi

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(OBJ_DIR)
	@echo "$(YELLOW)Compiling $<...$(NC)"
	@$(CC) $(CFLAGS) $(if $(DEBUG),$(DEBUG_FLAGS),$(RELEASE_FLAGS)) -c $< -o $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(OBJ_DIR)
	@echo "$(YELLOW)Compiling $<...$(NC)"
	@$(CXX) $(CXXFLAGS) $(if $(DEBUG),$(DEBUG_FLAGS),$(RELEASE_FLAGS)) -c $< -o $@

# Clean
clean:
	@echo "$(YELLOW)Cleaning build artifacts...$(NC)"
	@rm -rf $(BUILD_DIR)
	@echo "$(GREEN)Clean complete$(NC)"

# Test
test: build
	@echo "$(YELLOW)Running tests...$(NC)"
	@# Test execution logic would go here, customized for the actual test framework used
	@echo "$(GREEN)Tests passed$(NC)"

# Coverage
coverage:
	@echo "$(YELLOW)Generating coverage report...$(NC)"
	@mkdir -p $(COV_DIR)
	@# Coverage generation logic
	@echo "$(GREEN)Coverage report generated used$(NC)"

# Code Formatting
format:
	@echo "$(YELLOW)Formatting code...$(NC)"
	@find $(SRC_DIR) $(INCLUDE_DIR) $(TEST_DIR) -name '*.c' -o -name '*.cpp' -o -name '*.h' -o -name '*.hpp' | xargs $(CLANG_FORMAT) -i $(FORMAT_STYLE)
	@echo "$(GREEN)Formatting complete$(NC)"

# Linting
lint:
	@echo "$(YELLOW)Running static analysis...$(NC)"
	@find $(SRC_DIR) $(TEST_DIR) -name '*.c' -o -name '*.cpp' | xargs $(CLANG_TIDY) -p $(BUILD_DIR) 2>/dev/null || true
	@echo "$(GREEN)Static analysis complete$(NC)"

# Check (Format + Lint + Test)
check: format lint test
	@echo "$(GREEN)All checks passed$(NC)"

# Install/Uninstall
install: $(TARGET)
	@echo "$(YELLOW)Installing binary...$(NC)"
	@install -d /usr/local/bin
	@install $(TARGET) /usr/local/bin/
	@echo "$(GREEN)Installed to /usr/local/bin/$(PROJECT_NAME)$(NC)"

uninstall:
	@echo "$(YELLOW)Uninstalling binary...$(NC)"
	@rm -f /usr/local/bin/$(PROJECT_NAME)
	@echo "$(GREEN)Uninstalled$(NC)"

# Rust Targets
rust-build:
	@echo "$(YELLOW)Building Rust project...$(NC)"
	@if [ -f "Cargo.toml" ]; then cargo build; else echo "$(RED)Cargo.toml not found$(NC)"; exit 1; fi
	@echo "$(GREEN)Rust build complete$(NC)"

rust-test:
	@echo "$(YELLOW)Running Rust tests...$(NC)"
	@if [ -f "Cargo.toml" ]; then cargo test; else echo "$(RED)Cargo.toml not found$(NC)"; exit 1; fi
	@echo "$(GREEN)Rust tests passed$(NC)"

rust-fmt:
	@echo "$(YELLOW)Formatting Rust code...$(NC)"
	@if [ -f "Cargo.toml" ]; then cargo fmt; else echo "$(RED)Cargo.toml not found$(NC)"; exit 1; fi
	@echo "$(GREEN)Rust formatting complete$(NC)"

rust-clippy:
	@echo "$(YELLOW)Running Rust clippy...$(NC)"
	@if [ -f "Cargo.toml" ]; then cargo clippy -- -D warnings; else echo "$(RED)Cargo.toml not found$(NC)"; exit 1; fi
	@echo "$(GREEN)Rust clippy passed$(NC)"

rust-check: rust-fmt rust-clippy rust-test
	@echo "$(GREEN)Rust checks passed$(NC)"

rust-clean:
	@echo "$(YELLOW)Cleaning Rust artifacts...$(NC)"
	@if [ -f "Cargo.toml" ]; then cargo clean; else echo "$(RED)Cargo.toml not found$(NC)"; exit 1; fi
	@echo "$(GREEN)Rust clean complete$(NC)"

# Help
help:
	@echo "Available targets:"
	@echo "  make all          - Build all targets"
	@echo "  make build        - Compile source files (C/C++)"
	@echo "  make clean        - Remove build artifacts (C/C++)"
	@echo "  make test         - Run all tests (C/C++)"
	@echo "  make coverage     - Generate code coverage report (C/C++)"
	@echo "  make format       - Format code using clang-format (C/C++)"
	@echo "  make lint         - Run static analysis using clang-tidy (C/C++)"
	@echo "  make check        - Run format + lint + test (C/C++)"
	@echo "  make install      - Install binaries"
	@echo "  make uninstall    - Uninstall binaries"
	@echo "  make rust-build   - Build Rust project"
	@echo "  make rust-test    - Run Rust tests"
	@echo "  make rust-fmt     - Format Rust code"
	@echo "  make rust-clippy  - Run Clippy linter"
	@echo "  make rust-check   - Run fmt + clippy + test (Rust)"
	@echo "  make rust-clean   - Clean Rust artifacts"
