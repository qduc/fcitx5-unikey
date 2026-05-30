#!/usr/bin/env bash
# Simple test runner
# Usage: Just run it from anywhere in the project
#   scripts/test.sh           # Brief summary mode
#   scripts/test.sh -v        # Verbose mode (show all output)
#
# Re-run a single failing case with debug output:
#   ./bin/<testname> --case <N>

set -euo pipefail

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# Parse flags
VERBOSE=0
while [[ $# -gt 0 ]]; do
    case $1 in
        -v|--verbose)
            VERBOSE=1
            shift
            ;;
        *)
            break
            ;;
    esac
done

# Auto-detect build directory
if [[ -f "CTestTestfile.cmake" ]] || [[ -f "CMakeCache.txt" ]]; then
    BUILD_DIR="$PWD"
elif [[ -d "build" ]] && [[ -f "build/CTestTestfile.cmake" || -f "build/CMakeCache.txt" ]]; then
    BUILD_DIR="$PWD/build"
else
    echo "Error: Cannot find build directory. Run from project root or build directory." >&2
    exit 1
fi

echo "Build directory: $BUILD_DIR"
cd "$BUILD_DIR"

LOGS_DIR="$BUILD_DIR/.test-logs"
mkdir -p "$LOGS_DIR"

# Run tests
echo
echo "Running tests..."
if ctest --output-on-failure "$@"; then
    echo
    echo -e "${GREEN}All tests passed!${NC}"
    exit 0
fi

echo
echo -e "${YELLOW}Some tests failed. Analyzing...${NC}"

# Parse failed tests
mapfile -t failed_tests < <(
    if [[ -f "Testing/Temporary/LastTestsFailed.log" ]]; then
        while IFS= read -r line; do
            if [[ "$line" =~ :(.+)$ ]]; then
                echo "${BASH_REMATCH[1]}"
            fi
        done < "Testing/Temporary/LastTestsFailed.log"
    fi
)

if [[ ${#failed_tests[@]} -eq 0 ]]; then
    echo "Could not determine which tests failed."
    exit 1
fi

echo
echo
for test_name in "${failed_tests[@]}"; do
    echo -e "  ${RED}✗${NC} $test_name"
done

echo
echo "To rerun a specific failing test with full output:"
for test_name in "${failed_tests[@]}"; do
    exec_name="$test_name"
    env_prefix=""
    if [[ "$exec_name" == *-immediate ]]; then
        exec_name="${exec_name%-immediate}"
        env_prefix="FCITX_UNIKEY_TEST_FORCE_IMMEDIATE_COMMIT=1 "
    fi
    if [[ -x "bin/$exec_name" ]]; then
        echo -e "  ${BLUE}${env_prefix}./bin/$exec_name${NC}"
        echo "    (add --case <N> to run a specific case with full debug output)"
    fi
done

echo
echo -e "${RED}Tests failed: ${#failed_tests[@]}${NC}"
exit 1
