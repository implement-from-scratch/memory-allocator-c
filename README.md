# Memory Allocator in C

A production-style, thread-safe memory allocator built from scratch, designed to demonstrate advanced systems programming concepts and memory management techniques.

## Architecture Overview

This allocator implements a hybrid memory sourcing strategy that combines the efficiency of `sbrk()` for small allocations with the flexibility of `mmap()` for larger requests. The system uses a sophisticated block management approach with immediate coalescing and First-Fit allocation strategy.

**Core Components:**
- **Memory Sourcing Module**: Handles kernel memory requests via `sbrk()` (<128KB) and `mmap()` (≥128KB)
- **Block Manager**: Maintains free/used block lists with 16-byte aligned headers
- **Allocation Engine**: Implements First-Fit search with immediate block splitting
- **Coalescing Engine**: Performs immediate adjacent block merging on free operations
- **Thread Safety Layer**: Global mutex protection with optional thread-local caches
- **Integrity Checker**: Magic number validation and corruption detection

## Memory Layout

```
Heap Memory Organization:
+------------------+
| Heap Metadata    |  <- Global heap state, free lists
| - free_head      |
| - used_head      |  
| - heap_start     |
| - heap_end       |
+------------------+
| Block Headers    |  <- 16-byte aligned headers
| [size|flags|magic] |
| [prev|next ptrs] |
+------------------+
| User Data        |  <- Application payload
| (aligned)        |
+------------------+
| Footer/Canary    |  <- Optional integrity checks
+------------------+

Block Header Layout (16 bytes):
Offset 0:  size_t size           (8 bytes)
Offset 8:  uint32_t is_free      (4 bytes)  
Offset 12: uint32_t magic        (4 bytes)

Free Block Extensions:
Offset 16: block_t* prev_free    (8 bytes)
Offset 24: block_t* next_free    (8 bytes)
```

## How it Works

The allocator follows a carefully orchestrated sequence for each operation:

### Allocation Flow (malloc)
1. **Size Alignment**: Round requested size to 16-byte boundary: `aligned_size = (size + 15) & ~15`
2. **Memory Source Selection**: Choose `sbrk()` if `aligned_size < 128KB`, otherwise `mmap()`
3. **Free Block Search**: Traverse free list using First-Fit algorithm
4. **Block Splitting**: If found block is larger than needed, split into allocated + free portions
5. **Header Initialization**: Set size, clear free flag, write magic number `0xDEADBEEF`
6. **List Management**: Remove from free list, add to used list if tracking enabled

### Deallocation Flow (free)
1. **Pointer Validation**: Verify pointer falls within managed heap boundaries
2. **Header Integrity**: Check magic number for corruption detection
3. **Double-Free Detection**: Verify `is_free` flag is not already set
4. **Immediate Coalescing**: Merge with adjacent free blocks (both directions)
5. **List Management**: Add coalesced block to free list head

### Advanced Operations
- **calloc**: Implements overflow-safe multiplication checking before zero-initialization
- **realloc**: Attempts in-place expansion before copy-and-free fallback

## Development Setup

### Prerequisites
- GCC/Clang compiler with C11 support
- POSIX-compliant system (Linux/macOS)
- Make build system
- Valgrind (for memory leak detection)
- GDB (for debugging)
- pthread library for threading support

### Quick Start
```bash
# Clone the repository
git clone <repo-url>
cd memory-allocator-c

# Build the project
make build

# Run comprehensive tests
make test

# Run with memory analysis
make valgrind

# Performance benchmarks
make benchmark

# Format code
make format

# Static analysis
make lint
```

## Development Workflow

### Before Committing
```bash
# Run the full verification suite
make check
```

This comprehensive check includes:
1. Code formatting verification (clang-format)
2. Static analysis (clang-tidy, cppcheck)
3. Build verification (debug and release)
4. Unit test suite
5. Integration tests
6. Memory leak detection (valgrind)
7. Thread safety tests
8. Performance regression tests

### Code Style
This project follows strict coding standards enforced by [.clang-format](.clang-format):
- 4-space indentation
- 100-character line limit
- Consistent pointer alignment
- Comprehensive commenting for complex algorithms

### Pull Request Process
1. Fork the repository
2. Create a feature branch (`git checkout -b feature/advanced-coalescing`)
3. Implement changes following chapter-based development
4. Add corresponding tests and documentation
5. Run `make check` to ensure all verifications pass
6. Commit with descriptive messages (`git commit -m 'Implement bidirectional coalescing with boundary checks'`)
7. Push to branch (`git push origin feature/advanced-coalescing`)
8. Open Pull Request with detailed description

All PRs require:
- Approval from @mohitmishra786
- All CI checks passing (build, test, lint)
- Code coverage maintained above 90%
- Memory leak free verification
- Performance impact analysis

## Testing

### Test Categories
```bash
# Core functionality tests
make test-unit

# Multi-threaded stress tests  
make test-threading

# Memory integrity verification
make test-corruption

# Performance benchmarking
make test-performance

# Compatibility with standard malloc
make test-compliance
```

### Memory Analysis
```bash
# Detect memory leaks
make valgrind

# Thread safety analysis
make helgrind

# Cache performance profiling
make cachegrind

# Generate detailed coverage report
make coverage
```

## Continuous Integration

GitHub Actions pipeline includes:
- **Multi-compiler builds**: GCC 9-12, Clang 10-15
- **Platform testing**: Ubuntu 20.04/22.04, macOS 11/12
- **Sanitizer verification**: AddressSanitizer, ThreadSanitizer, UndefinedBehaviorSanitizer
- **Performance regression detection**: Automated benchmarking against baseline
- **Security scanning**: Static analysis for common vulnerabilities
- **Documentation generation**: LaTeX formula rendering and API docs

## Documentation Structure

The implementation follows incremental learning chapters:

- **[Chapter 01](docs/01-memory-fundamentals.md)**: Memory Fundamentals and Architecture Design
- **[Chapter 02](docs/02-block-structure.md)**: Block Headers and Alignment Mathematics  
- **[Chapter 03](docs/03-memory-sourcing.md)**: Kernel Memory Acquisition (sbrk vs mmap)
- **[Chapter 04](docs/04-basic-malloc.md)**: Basic malloc Implementation
- **[Chapter 05](docs/05-coalescing-free.md)**: Advanced Free with Coalescing
- **[Chapter 06](docs/06-allocation-strategies.md)**: First-Fit vs Best-Fit Analysis
- **[Chapter 07](docs/07-error-handling.md)**: Heap Integrity and Corruption Detection
- **[Chapter 08](docs/08-standard-compliance.md)**: calloc and realloc Implementation
- **[Chapter 09](docs/09-thread-safety.md)**: Thread Safety with Global Mutex
- **[Chapter 10](docs/10-thread-local-optimization.md)**: Thread-Local Caches for Performance
- **[Chapter 11](docs/11-performance-analysis.md)**: Benchmarking and Production Readiness

## Troubleshooting

### Common Issues

**Compilation fails with "undefined reference to pthread"**
```bash
# Install pthread development libraries
sudo apt-get install libc6-dev
# Ensure linking with -lpthread flag
make clean && make build
```

**Segmentation fault during tests**
```bash
# Run with debug symbols and core dump analysis
make build DEBUG=1
gdb ./build/test_allocator core
```

**Performance degradation detected**
```bash
# Profile with detailed memory access patterns
make profile
# Check for excessive fragmentation
./build/allocator_analyzer --fragmentation-report
```

**Thread safety violations**
```bash
# Run comprehensive thread analysis
make helgrind
# Enable detailed thread debugging
export ALLOCATOR_DEBUG_THREADS=1
```

### Performance Characteristics

| Operation | Time Complexity | Space Overhead | Thread Safety |
|-----------|----------------|----------------|---------------|
| malloc()  | O(n) worst-case | 16 bytes/block | Mutex protected |
| free()    | O(1) amortized  | 0 bytes        | Mutex protected |
| calloc()  | O(n) + memset   | 16 bytes/block | Mutex protected |
| realloc() | O(n) worst-case | 16 bytes/block | Mutex protected |

**Memory Efficiency**: Typical overhead of 16 bytes per allocation with immediate coalescing maintaining <5% fragmentation under normal workloads.

**Thread Performance**: Global mutex contention becomes bottleneck beyond 8 concurrent threads, resolved via thread-local caches in Chapter 10.