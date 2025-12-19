#!/usr/bin/env bash
# Run CTest in two passes.
#
# Pass 1: quiet run to quickly identify failing tests.
# Pass 2: re-run each failing test individually with full verbosity.
#
# Typical usage:
#   bash scripts/ctest_2pass.sh --build-dir build
#   bash scripts/ctest_2pass.sh --build-dir build -- -j8
#   bash scripts/ctest_2pass.sh --build-dir build --env GLOG_v=2 -- --timeout 120
#
# Notes:
# - This script relies on CTest producing Testing/Temporary/LastTestsFailed.log.
# - Extra arguments after "--" are passed to BOTH passes.

set -euo pipefail

# Default values
BUILD_DIR="build"
PAUSE=0
NO_SECOND_PASS=0
NO_CASE_PASS=0
declare -a ENV_VARS=()
declare -a CTEST_ARGS=()

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

usage() {
    cat <<EOF
Usage: $(basename "$0") [OPTIONS] [-- CTEST_ARGS...]

Run CTest in 2 passes: (1) quiet to identify failures;
(2) re-run each failed test individually with full verbosity.

OPTIONS:
    --build-dir DIR       CMake build directory (default: build)
    --pause               Wait for Enter between failed-test reruns
    --env KEY=VALUE       Set environment variable for BOTH passes (repeatable)
    --no-second-pass      Only run first pass and print failed test list
    --no-case-pass        Do not attempt case-only rerun
    -h, --help            Show this help message

CTEST_ARGS:
    Extra arguments passed to ctest for BOTH passes (after --)
EOF
}

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --build-dir)
            BUILD_DIR="$2"
            shift 2
            ;;
        --pause)
            PAUSE=1
            shift
            ;;
        --env)
            ENV_VARS+=("$2")
            shift 2
            ;;
        --no-second-pass)
            NO_SECOND_PASS=1
            shift
            ;;
        --no-case-pass)
            NO_CASE_PASS=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        --)
            shift
            CTEST_ARGS=("$@")
            break
            ;;
        *)
            echo "Error: Unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

# Validate build directory
if [[ ! -d "$BUILD_DIR" ]]; then
    echo "Error: Build dir does not exist: $BUILD_DIR" >&2
    exit 2
fi

if [[ ! -f "$BUILD_DIR/CTestTestfile.cmake" && ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
    echo "Error: Not a CMake/CTest build dir (missing CTestTestfile.cmake / CMakeCache.txt): $BUILD_DIR" >&2
    exit 2
fi

BUILD_DIR=$(cd "$BUILD_DIR" && pwd)
LOGS_DIR="$BUILD_DIR/.ctest-2pass"
mkdir -p "$LOGS_DIR"

# Set environment variables
for env_var in "${ENV_VARS[@]}"; do
    if [[ ! "$env_var" =~ = ]]; then
        echo "Error: Invalid --env value '$env_var'. Expected KEY=VALUE" >&2
        exit 2
    fi
    export "$env_var"
done

# Read failed tests from LastTestsFailed.log
read_failed_tests_file() {
    local failed_file="$BUILD_DIR/Testing/Temporary/LastTestsFailed.log"
    declare -a tests=()

    if [[ ! -f "$failed_file" ]]; then
        return
    fi

    while IFS= read -r line; do
        line="${line#"${line%%[![:space:]]*}"}" # trim leading whitespace
        line="${line%"${line##*[![:space:]]}"}" # trim trailing whitespace

        if [[ -z "$line" ]]; then
            continue
        fi

        # Format: "2:testname" or "testname"
        if [[ "$line" =~ :(.+)$ ]]; then
            local test_name="${BASH_REMATCH[1]}"
            test_name="${test_name#"${test_name%%[![:space:]]*}"}"
            if [[ -n "$test_name" ]]; then
                tests+=("$test_name")
            fi
        else
            tests+=("$line")
        fi
    done < "$failed_file"

    # Remove duplicates while preserving order
    declare -A seen
    for test in "${tests[@]}"; do
        if [[ -z "${seen[$test]:-}" ]]; then
            echo "$test"
            seen[$test]=1
        fi
    done
}

# Parse failed tests from ctest output
parse_failed_tests_from_output() {
    local output="$1"
    declare -a tests=()
    local in_section=0

    while IFS= read -r line; do
        if [[ "$line" =~ ^The\ following\ tests\ FAILED: ]]; then
            in_section=1
            continue
        fi

        if [[ $in_section -eq 0 ]]; then
            continue
        fi

        # Format: "  2 - testsurroundingtext (Failed)"
        if [[ "$line" =~ ^[[:space:]]*[0-9]+[[:space:]]*-[[:space:]]*([^[:space:]].+)[[:space:]]*\(Failed\) ]]; then
            local test_name="${BASH_REMATCH[1]}"
            test_name="${test_name%"${test_name##*[![:space:]]}"}" # trim trailing
            tests+=("$test_name")
        elif [[ $in_section -eq 1 && -z "${line//[[:space:]]/}" ]]; then
            # Empty line after section
            break
        fi
    done <<< "$output"

    # Remove duplicates while preserving order
    declare -A seen
    for test in "${tests[@]}"; do
        if [[ -z "${seen[$test]:-}" ]]; then
            echo "$test"
            seen[$test]=1
        fi
    done
}

# Detect last case ID from test output
detect_last_case_id() {
    local output="$1"
    local test_name="$2"
    local last_case=""

    # Pattern: "testsurroundingtext: Case 20" or "testkeyhandling: Case 4 - ..."
    while IFS= read -r line; do
        if [[ "$line" =~ $test_name:[[:space:]]*Case[[:space:]]+([0-9]+) ]]; then
            last_case="${BASH_REMATCH[1]}"
        fi
    done <<< "$output"

    echo "$last_case"
}

# Get test command from CTest
get_test_command() {
    local test_name="$1"
    local regex="^${test_name}\$"

    local output
    output=$(cd "$BUILD_DIR" && ctest -N -V -R "$regex" 2>&1 || true)

    local cmd_line=""
    local work_dir=""

    while IFS= read -r line; do
        if [[ "$line" =~ ^Test\ command: ]]; then
            cmd_line="${line#*: }"
            cmd_line="${cmd_line#"${cmd_line%%[![:space:]]*}"}"
        elif [[ "$line" =~ ^Working\ Directory: ]]; then
            work_dir="${line#*: }"
            work_dir="${work_dir#"${work_dir%%[![:space:]]*}"}"
        fi
    done <<< "$output"

    # Handle multi-line test command
    if [[ -z "$cmd_line" ]]; then
        local found=0
        while IFS= read -r line; do
            if [[ "$line" =~ ^Test\ command: ]]; then
                found=1
                cmd_line="${line#*: }"
                cmd_line="${cmd_line#"${cmd_line%%[![:space:]]*}"}"
                continue
            fi
            if [[ $found -eq 1 && -z "$cmd_line" ]]; then
                line="${line#"${line%%[![:space:]]*}"}"
                if [[ -n "$line" ]]; then
                    cmd_line="$line"
                    break
                fi
            fi
        done <<< "$output"
    fi

    if [[ -z "$work_dir" ]]; then
        if [[ -d "$BUILD_DIR/test" ]]; then
            work_dir="$BUILD_DIR/test"
        else
            work_dir="$BUILD_DIR"
        fi
    fi

    if [[ -z "$cmd_line" ]]; then
        # Fallback
        cmd_line="$BUILD_DIR/bin/$test_name"
    fi

    echo "$work_dir|$cmd_line"
}

# Run command and tee output to log file
run_tee() {
    local log_path="$1"
    shift
    local cmd=("$@")

    echo "$ ${cmd[*]}" > "$log_path"
    "${cmd[@]}" 2>&1 | tee -a "$log_path"
    return "${PIPEFAIL[@]}"
}

# ========== PASS 1: Quick scan ==========
echo "== PASS 1: running CTest (quiet) to identify failures =="
pass1_cmd=(ctest -Q "${CTEST_ARGS[@]}")
echo "$ ${pass1_cmd[*]}"

pass1_output=$(cd "$BUILD_DIR" && "${pass1_cmd[@]}" 2>&1) || pass1_rc=$?
pass1_rc=${pass1_rc:-0}
echo "$pass1_output" > "$LOGS_DIR/pass1.log"

if [[ $pass1_rc -eq 0 ]]; then
    echo
    echo "PASS 1 succeeded: no failing tests."
    exit 0
fi

# Identify failures
mapfile -t failed_tests < <(read_failed_tests_file)

if [[ ${#failed_tests[@]} -eq 0 ]]; then
    mapfile -t failed_tests < <(parse_failed_tests_from_output "$pass1_output")
fi

if [[ ${#failed_tests[@]} -eq 0 ]]; then
    echo >&2
    echo "Error: PASS 1 failed, but could not determine failed test list." >&2
    echo "Saved PASS 1 output to: $LOGS_DIR/pass1.log" >&2
    echo "Tip: check $BUILD_DIR/Testing/Temporary/LastTest.log, or rerun with: ctest --output-on-failure -VV" >&2
    exit "${pass1_rc:-1}"
fi

echo
echo "Failing tests:"
for i in "${!failed_tests[@]}"; do
    echo "  $((i+1)). ${failed_tests[$i]}"
done

if [[ $NO_SECOND_PASS -eq 1 ]]; then
    echo
    echo "Skipping PASS 2 (--no-second-pass). Logs: $LOGS_DIR"
    exit "${pass1_rc:-1}"
fi

# ========== PASS 2: Rerun each failed test ==========
overall_rc=0
echo
echo "== PASS 2: rerunning each failed test individually (-VV --output-on-failure) =="

for idx in "${!failed_tests[@]}"; do
    test_name="${failed_tests[$idx]}"
    num=$((idx+1))
    total=${#failed_tests[@]}

    if [[ $PAUSE -eq 1 ]]; then
        echo
        read -r -p "[$num/$total] Press Enter to rerun: $test_name "
    else
        echo
        echo "[$num/$total] Rerunning: $test_name"
    fi

    regex="^${test_name}\$"
    log_path="$LOGS_DIR/pass2.${test_name}.log"

    pass2_cmd=(ctest -R "$regex" -VV --output-on-failure "${CTEST_ARGS[@]}")

    set +e
    (cd "$BUILD_DIR" && run_tee "$log_path" "${pass2_cmd[@]}")
    rc=$?
    set -e

    if [[ $rc -ne 0 ]]; then
        overall_rc=$rc
        echo
        echo -e "${RED}[FAILED]${NC} $test_name (exit=$rc)"

        if [[ $NO_CASE_PASS -eq 0 ]]; then
            output=$(<"$log_path")
            case_id=$(detect_last_case_id "$output" "$test_name")

            if [[ -z "$case_id" ]]; then
                echo "Could not detect failing case id from output; skipping case-only rerun."
            else
                echo "Detected failing case: $test_name case $case_id"

                # Get test command
                info=$(get_test_command "$test_name")
                work_dir="${info%%|*}"
                cmd_line="${info#*|}"

                # Parse command line (simple split on spaces, good enough for this use case)
                read -ra case_cmd <<< "$cmd_line"
                case_cmd+=("--case" "$case_id")

                case_log="$LOGS_DIR/pass2.${test_name}.case${case_id}.log"
                echo "$ ${case_cmd[*]}"

                set +e
                (cd "$work_dir" && run_tee "$case_log" "${case_cmd[@]}")
                rc2=$?
                set -e

                if [[ $rc2 -ne 0 ]]; then
                    echo -e "${RED}[FAILED]${NC} case-only rerun also failed: $test_name case $case_id (exit=$rc2)"
                else
                    echo -e "${GREEN}[OK]${NC} case-only rerun passed: $test_name case $case_id"
                fi
                echo "Case log saved: $case_log"
            fi
        fi
    else
        echo
        echo -e "${GREEN}[OK]${NC} $test_name"
    fi
    echo "Log saved: $log_path"
done

echo
echo "Done. PASS 1 log: $LOGS_DIR/pass1.log"
exit "${overall_rc:-1}"
