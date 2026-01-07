# [System Name]

[Brief 1-sentence summary of what this system does.]

## Architecture Overview

[High-level description of the system components. Explain the main modules, their responsibilities, and how they interact with each other.]

## Memory Layout

[This is a mandatory section. Use a text-based diagram (ASCII) to show:
- Pointer relationships
- Data structure padding
- Segment usage (stack, heap, etc.)
- Memory alignment considerations

Example format:
```
+------------------+
| Stack Frame      |
| - local vars     |
| - return addr    |
+------------------+
| Heap             |
| - malloc'd data  |
+------------------+
```
]

## How it Works

[Step-by-step walkthrough of the primary logic flow. For example:
- "From Socket Listen to Request Handle"
- "From Input Parsing to Output Generation"
- "From Memory Allocation to Deallocation"

Break down the flow into clear, numbered steps that explain the educational core of the system.]

## Development Setup

### Prerequisites
- GCC/Clang compiler
- Make
- CMake (optional)
- Rust toolchain (if applicable)
- Testing frameworks (CUnit, Check, Google Test)

### Quick Start
```bash
# Clone the repository
git clone <repo-url>
cd <repo-name>

# Build the project
make build

# Run tests
make test

# Format code
make format

# Run static analysis
make lint
```

## Development Workflow

### Before Committing
```bash
# Run the full check suite
make check
```

This will run:
1. Code formatting check
2. Static analysis (clang-tidy)
3. Build
4. All tests

### Code Style
This project follows the [coding style](.clang-format) enforced by clang-format.
Run `make format` before committing.

### Pull Request Process
1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Make your changes
4. Run `make check` to ensure all checks pass
5. Commit your changes (`git commit -m 'Add amazing feature'`)
6. Push to the branch (`git push origin feature/amazing-feature`)
7. Open a Pull Request

All PRs require:
- Approval from @mohitmishra786
- All CI checks to pass
- Code coverage maintained or improved

## Testing

### Running Tests
```bash
# Run all tests
make test

# Run specific test suites
make test-unit           # Unit tests only
make test-integration    # Integration tests only
make test-functional     # Functional tests only

# Run with memory leak detection
make valgrind

# Generate coverage report
make coverage
```

## Continuous Integration

This project uses GitHub Actions for CI/CD with the following checks:
- Code formatting (clang-format)
- Static analysis (clang-tidy, cppcheck)
- Multi-compiler builds (GCC, Clang)
- Multi-platform testing (Linux, macOS)
- Sanitizer tests (AddressSanitizer, UndefinedBehaviorSanitizer)
- Code coverage reporting
- Security scanning

## Troubleshooting

### Common Issues

**Build fails with "command not found"**
- Install missing dependencies: `sudo apt-get install build-essential`

**Tests fail**
- Ensure all test dependencies are installed
- Run `make clean` and rebuild

**Coverage report not generated**
- Install lcov: `sudo apt-get install lcov`
- Build with DEBUG=1: `make build DEBUG=1`
