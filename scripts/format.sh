#!/bin/bash

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Find all C/C++ files
find_source_files() {
    find src include tests -type f \( -name "*.c" -o -name "*.cpp" -o -name "*.h" -o -name "*.hpp" \) 2>/dev/null
}

# Check if clang-format is installed
if ! command -v clang-format &> /dev/null; then
    echo -e "${RED}Error: clang-format is not installed${NC}"
    echo "Install it using: sudo apt-get install clang-format"
    exit 1
fi

# Check mode: format or check
MODE="${1:-format}"

if [ "$MODE" == "check" ]; then
    echo "Checking code formatting..."
    FAILED=0
    while IFS= read -r file; do
        if ! clang-format --dry-run --Werror "$file" 2>/dev/null; then
            echo -e "${RED}[FAIL] $file needs formatting${NC}"
            FAILED=1
        else
            echo -e "${GREEN}[OK] $file${NC}"
        fi
    done < <(find_source_files)
    
    if [ $FAILED -eq 1 ]; then
        echo -e "${RED}Format check failed. Run 'make format' to fix.${NC}"
        exit 1
    fi
    echo -e "${GREEN}All files are properly formatted${NC}"
else
    echo "Formatting code..."
    while IFS= read -r file; do
        clang-format -i "$file"
        echo -e "${GREEN}Formatted: $file${NC}"
    done < <(find_source_files)
    echo -e "${GREEN}Code formatting complete${NC}"
fi
