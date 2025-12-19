#!/usr/bin/env bash
# Simple test runner with automatic case-level retry on failure
# Usage: Just run it from anywhere in the project
#   scripts/test.sh           # Brief summary mode
#   scripts/test.sh -v        # Verbose mode (show all output)

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
echo "Failed tests: ${#failed_tests[@]}"
declare -a case_logs=()

# Try to isolate failing cases
for test_name in "${failed_tests[@]}"; do
    echo -e "${BLUE}→ Analyzing: $test_name${NC}"

    # Get the test log
    log_file="Testing/Temporary/LastTest.log"
    if [[ ! -f "$log_file" ]]; then
        echo "  No log file found"
        continue
    fi

    # Detect failing case number (pattern: "testname: Case N")
    case_id=""
    while IFS= read -r line; do
        if [[ "$line" =~ $test_name:[[:space:]]*Case[[:space:]]+([0-9]+) ]]; then
            case_id="${BASH_REMATCH[1]}"
        fi
    done < "$log_file"

    if [[ -z "$case_id" ]]; then
        echo "  Could not detect specific failing case"
        continue
    fi

    echo "  Detected failing case: $case_id"

    # Find test executable
    if [[ ! -f "bin/$test_name" ]]; then
        echo "  Warning: Cannot find test executable at bin/$test_name"
        continue
    fi

    # Run the specific case and save to log
    case_log="$LOGS_DIR/${test_name}.case${case_id}.log"
    echo "  Rerunning case $case_id..."

    if [[ $VERBOSE -eq 1 ]]; then
        echo
        ./bin/"$test_name" --case "$case_id" 2>&1 | tee "$case_log" || true
        echo
    else
        ./bin/"$test_name" --case "$case_id" > "$case_log" 2>&1 || true
        echo "  Log saved: $case_log"

        # Show just the failure line
        if grep -q "failed\|FAILED\|Failed" "$case_log"; then
            echo -e "  ${RED}✗ Case $case_id failed${NC}"
            grep -m1 "failed\|FAILED\|Failed" "$case_log" | sed 's/^/    /' || true
        fi
    fi

    case_logs+=("$case_log")
done

echo
if [[ ${#case_logs[@]} -gt 0 ]]; then
    echo -e "${YELLOW}Detailed logs saved in:${NC} $LOGS_DIR"
    echo
    echo "To rerun a specific failing case with full output:"
    for log in "${case_logs[@]}"; do
        # Extract test name and case number from log filename
        # Format: testsurroundingtext.case1.log
        basename_log=$(basename "$log")
        if [[ "$basename_log" =~ ^(.+)\.case([0-9]+)\.log$ ]]; then
            test_name="${BASH_REMATCH[1]}"
            case_num="${BASH_REMATCH[2]}"
            echo -e "  ${BLUE}./bin/$test_name --case $case_num${NC}"
        fi
    done
    echo
    echo "To view saved logs:"
    for log in "${case_logs[@]}"; do
        echo "  cat $LOGS_DIR/$(basename "$log")"
    done
    echo
    if [[ $VERBOSE -eq 0 ]]; then
        echo "Or run with -v to see full output immediately: scripts/test.sh -v"
    fi
fi

echo
echo -e "${RED}Tests failed.${NC}"
exit 1
