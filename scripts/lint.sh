#!/bin/bash

# Color codes
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

# Check if clang-tidy is installed
if ! command -v clang-tidy &> /dev/null; then
    echo -e "${RED}Error: clang-tidy is not installed${NC}"
    echo "Install it using: sudo apt-get install clang-tidy"
    exit 1
fi

# Find all C/C++ source files
find_source_files() {
    find src tests -type f \( -name "*.c" -o -name "*.cpp" \) 2>/dev/null
}

# Generate compile_commands.json if not exists
if [ ! -f "compile_commands.json" ]; then
    echo "Generating compile_commands.json..."
    bear -- make build 2>/dev/null || echo -e "${YELLOW}Warning: Could not generate compile_commands.json${NC}"
fi

echo "Running static analysis..."
FAILED=0

while IFS= read -r file; do
    echo -e "${YELLOW}Analyzing: $file${NC}"
    if clang-tidy "$file" -- -I./include 2>&1 | grep -q "warning:"; then
        echo -e "${RED}[FAIL] Issues found in $file${NC}"
        clang-tidy "$file" -- -I./include
        FAILED=1
    else
        echo -e "${GREEN}[OK] $file${NC}"
    fi
done < <(find_source_files)

if [ $FAILED -eq 1 ]; then
    echo -e "${RED}Static analysis found issues${NC}"
    exit 1
fi

echo -e "${GREEN}Static analysis passed${NC}"
