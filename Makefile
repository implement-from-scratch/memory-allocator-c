# Memory Allocator - Simplified Build System
PROJECT_NAME = memory-allocator
VERSION = 1.0.0
BUILD_DIR = build
SRC_DIR = src
INCLUDE_DIR = include
TEST_DIR = tests

# Platform Detection
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    PLATFORM = macos
else
    PLATFORM = linux
endif

# Compiler Configuration
CC = gcc
CFLAGS = -std=c11 -Wall -Wextra
INCLUDES = -I$(INCLUDE_DIR)
LIBS = -lpthread -lm

# Build Mode Configuration
DEBUG ?= 0
ifeq ($(DEBUG), 1)
    CFLAGS += -g3 -O0 -DDEBUG -fno-omit-frame-pointer
    BUILD_TYPE = debug
else
    CFLAGS += -O3 -DNDEBUG -fomit-frame-pointer
    BUILD_TYPE = release
endif

# Sanitizer Support (for Linux CI)
ifdef SANITIZER
    ifeq ($(SANITIZER),address)
        CFLAGS += -fsanitize=address -fno-omit-frame-pointer
        LIBS += -fsanitize=address
    else ifeq ($(SANITIZER),undefined)
        CFLAGS += -fsanitize=undefined -fno-omit-frame-pointer
        LIBS += -fsanitize=undefined
    else ifeq ($(SANITIZER),thread)
        CFLAGS += -fsanitize=thread -fno-omit-frame-pointer
        LIBS += -fsanitize=thread
    endif
endif

# Source Files
SOURCES = $(wildcard $(SRC_DIR)/*.c)
HEADERS = $(wildcard $(INCLUDE_DIR)/*.h)
OBJECTS = $(SOURCES:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

# Test Files
TEST_SOURCES = $(wildcard $(TEST_DIR)/*.c)
TEST_OBJECTS = $(TEST_SOURCES:$(TEST_DIR)/%.c=$(BUILD_DIR)/test_%.o)

# Library Targets
STATIC_LIB = $(BUILD_DIR)/lib$(PROJECT_NAME).a
SHARED_LIB = $(BUILD_DIR)/lib$(PROJECT_NAME).so

# Default target
.PHONY: all
all: build

# Build targets
.PHONY: build
build: $(STATIC_LIB) $(SHARED_LIB)

# Create build directory
$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

# Compile source files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c $(HEADERS) | $(BUILD_DIR)
	@echo "Compiling $< ($(BUILD_TYPE))"
	@$(CC) $(CFLAGS) $(INCLUDES) -fPIC -c $< -o $@

# Create static library
$(STATIC_LIB): $(OBJECTS)
	@echo "Creating static library $@"
	@ar rcs $@ $^

# Create shared library
$(SHARED_LIB): $(OBJECTS)
	@echo "Creating shared library $@"
ifeq ($(PLATFORM),macos)
	@$(CC) -shared $(LDFLAGS) -o $@ $^ $(LIBS)
else
	@$(CC) -shared -Wl,-soname,lib$(PROJECT_NAME).so.1 $(LDFLAGS) -o $@ $^ $(LIBS)
endif

# Test compilation
$(BUILD_DIR)/test_%.o: $(TEST_DIR)/%.c $(HEADERS) | $(BUILD_DIR)
	@echo "Compiling test $<"
	@$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Test executables
$(BUILD_DIR)/test_%: $(BUILD_DIR)/test_%.o $(STATIC_LIB)
	@echo "Linking test executable $@"
	@$(CC) $(LDFLAGS) -o $@ $< $(STATIC_LIB) $(LIBS)

# Testing targets
.PHONY: test
test: test-unit

.PHONY: test-unit
test-unit: $(BUILD_DIR)/test_allocator
	@echo "Running unit tests..."
	@./$(BUILD_DIR)/test_allocator

.PHONY: integration-test
integration-test: test-unit
	@echo "Running integration tests..."
	@echo "Integration tests are currently the same as unit tests"
	@./$(BUILD_DIR)/test_allocator

# Memory analysis (skip valgrind on macOS as it's not well supported)
.PHONY: valgrind
valgrind: $(BUILD_DIR)/test_allocator
ifeq ($(PLATFORM),macos)
	@echo "Valgrind not well supported on macOS, skipping..."
else
	@echo "Running Valgrind memory check..."
	@valgrind --tool=memcheck --leak-check=full \
		--track-origins=yes --error-exitcode=1 \
		./$(BUILD_DIR)/test_allocator
endif

# Code coverage (Linux only, requires gcc and lcov)
.PHONY: coverage
coverage:
ifeq ($(PLATFORM),macos)
	@echo "Coverage analysis not configured for macOS, skipping..."
else
	@echo "Building with coverage instrumentation..."
	@$(MAKE) clean
	@$(MAKE) build DEBUG=1 CFLAGS="$(CFLAGS) --coverage -fprofile-arcs -ftest-coverage"
	@echo "Running tests with coverage..."
	@./$(BUILD_DIR)/test_allocator || true
	@echo "Generating coverage report..."
	@mkdir -p coverage
	@lcov --capture --directory $(BUILD_DIR) --output-file coverage/coverage.info --no-external
	@genhtml coverage/coverage.info --output-directory coverage/html
	@echo "Coverage report generated in coverage/html/index.html"
endif

# Code quality
.PHONY: format
format:
	@echo "Formatting code with clang-format..."
	@clang-format -i $(SOURCES) $(HEADERS) $(TEST_SOURCES)

.PHONY: format-check
format-check:
	@echo "Checking code formatting..."
	@if command -v clang-format >/dev/null 2>&1; then \
		for file in $(SOURCES) $(HEADERS) $(TEST_SOURCES); do \
			if [ -f "$$file" ]; then \
				if ! clang-format "$$file" | diff -q "$$file" - >/dev/null 2>&1; then \
					echo "[ERROR] $$file is not properly formatted"; \
					echo "Run 'make format' to fix formatting issues"; \
					exit 1; \
				fi; \
			fi; \
		done; \
		echo "[OK] All files are properly formatted"; \
	else \
		echo "[WARN] clang-format not found, skipping format check"; \
	fi

.PHONY: lint
lint:
	@echo "Running static analysis..."
	@if command -v clang-tidy >/dev/null 2>&1; then \
		echo "Running clang-tidy..."; \
		clang-tidy $(SOURCES) -- $(CFLAGS) $(INCLUDES) -std=c11 || true; \
	fi
	@if command -v cppcheck >/dev/null 2>&1; then \
		echo "Running cppcheck..."; \
		cppcheck --enable=warning --std=c11 $(INCLUDES) $(SRC_DIR) || true; \
	fi

# Installation
.PHONY: install
install: $(STATIC_LIB) $(SHARED_LIB)
	@echo "Installing allocator library..."
	@sudo cp $(STATIC_LIB) /usr/local/lib/
	@sudo cp $(SHARED_LIB) /usr/local/lib/
	@sudo cp $(INCLUDE_DIR)/*.h /usr/local/include/
ifeq ($(PLATFORM),linux)
	@sudo ldconfig
endif
	@echo "Installation completed"

.PHONY: uninstall
uninstall:
	@echo "Uninstalling allocator library..."
	@sudo rm -f /usr/local/lib/lib$(PROJECT_NAME).*
	@sudo rm -f /usr/local/include/allocator.h
ifeq ($(PLATFORM),linux)
	@sudo ldconfig
endif
	@echo "Uninstallation completed"

# Cleanup
.PHONY: clean
clean:
	@echo "Cleaning build artifacts..."
	@rm -rf $(BUILD_DIR)
	@echo "Clean completed"

# Comprehensive check
.PHONY: check
check: clean build test
	@echo "All checks passed successfully!"

# Help system
.PHONY: help
help:
	@echo "Memory Allocator Build System"
	@echo "============================="
	@echo ""
	@echo "Build Targets:"
	@echo "  build          - Build static and shared libraries"
	@echo "  test           - Run all tests"
	@echo "  clean          - Remove build artifacts"
	@echo "  check          - Full build and test cycle"
	@echo ""
	@echo "Options:"
	@echo "  DEBUG=1        - Enable debug build"
	@echo ""
	@echo "Examples:"
	@echo "  make build DEBUG=1    - Debug build"
	@echo "  make test             - Run tests"
	@echo "  make check            - Full verification"

# Debug target
.PHONY: debug
debug: DEBUG=1
debug: $(BUILD_DIR)/test_allocator
	@echo "Starting debugger..."
ifeq ($(PLATFORM),macos)
	@lldb ./$(BUILD_DIR)/test_allocator
else
	@gdb ./$(BUILD_DIR)/test_allocator
endif

.PHONY: print-vars
print-vars:
	@echo "CC=$(CC)"
	@echo "CFLAGS=$(CFLAGS)"
	@echo "BUILD_TYPE=$(BUILD_TYPE)"
	@echo "SOURCES=$(SOURCES)"
	@echo "OBJECTS=$(OBJECTS)"
	@echo "TEST_SOURCES=$(TEST_SOURCES)"
	@echo "PLATFORM=$(PLATFORM)"
