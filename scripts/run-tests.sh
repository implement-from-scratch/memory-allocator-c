#!/bin/bash

set -e

# Color codes
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# Configuration
BUILD_DIR="build"
TEST_RESULTS_DIR="$BUILD_DIR/test-results"
COVERAGE_DIR="$BUILD_DIR/coverage"

# Create directories
mkdir -p "$TEST_RESULTS_DIR"/{unit,integration,functional,performance,stress}
mkdir -p "$COVERAGE_DIR"

# Function to run tests
run_test_suite() {
    local suite_name=$1
    local test_command=$2
    
    echo -e "${BLUE}Running $suite_name tests...${NC}"
    
    if eval "$test_command"; then
        echo -e "${GREEN}[OK] $suite_name tests passed${NC}"
        return 0
    else
        echo -e "${RED}[FAIL] $suite_name tests failed${NC}"
        return 1
    fi
}

# Main test execution
main() {
    echo -e "${BLUE}=== Starting Test Suite ===${NC}\n"
    
    FAILED=0
    
    # Unit Tests
    run_test_suite "Unit" "make test-unit" || FAILED=1
    
    # Integration Tests
    run_test_suite "Integration" "make test-integration" || FAILED=1
    
    # Functional Tests
    run_test_suite "Functional" "make test-functional" || FAILED=1
    
    # Performance Tests
    run_test_suite "Performance" "make test-performance" || true
    
    # Generate Coverage
    if [ "$FAILED" -eq 0 ]; then
        echo -e "\n${BLUE}Generating coverage report...${NC}"
        make coverage
        echo -e "${GREEN}Coverage report generated at: $COVERAGE_DIR/index.html${NC}"
    fi
    
    # Summary
    echo -e "\n${BLUE}=== Test Suite Complete ===${NC}"
    if [ "$FAILED" -eq 0 ]; then
        echo -e "${GREEN}All tests passed successfully!${NC}"
        exit 0
    else
        echo -e "${RED}Some tests failed. Check the logs above.${NC}"
        exit 1
    fi
}

main "$@"
