#!/bin/bash
# Run tests by group to find hanging tests

cd "$(dirname "$0")"

TEST_BIN="./build/unit-tests"

if [ ! -x "$TEST_BIN" ]; then
    echo "❌ $TEST_BIN not found or not executable. Build it first (e.g. cmake --build build/ -t unit-tests)."
    exit 1
fi

set -o pipefail

TIMEOUT_BIN=""
if command -v timeout >/dev/null 2>&1; then
    TIMEOUT_BIN="timeout"
elif command -v gtimeout >/dev/null 2>&1; then
    TIMEOUT_BIN="gtimeout"
fi

run_with_timeout() {
    local seconds="$1"
    shift
    if [ -n "$TIMEOUT_BIN" ]; then
        "$TIMEOUT_BIN" "$seconds" "$@"
    else
        perl -e 'alarm shift; exec @ARGV' "$seconds" "$@"
    fi
}

# Default timeouts (seconds)
GROUP_TIMEOUT_DEFAULT=5
TEST_TIMEOUT_DEFAULT=3

group_timeout_for() {
    case "$1" in
        shared_test_loops) echo 20 ;;
        *) echo "$GROUP_TIMEOUT_DEFAULT" ;;
    esac
}

test_timeout_for() {
    case "$1" in
        shared_test_loops/for_large_sequence) echo 20 ;;
        *) echo "$TEST_TIMEOUT_DEFAULT" ;;
    esac
}

# Get all test groups (include shared_test_*)
TEST_GROUPS=$(
    "$TEST_BIN" --list 2>&1 | \
        awk -F/ '/^[[:space:]]*(test_|shared_test_)/ {gsub(/^[[:space:]]+/, "", $1); print $1}' | \
        sort -u
)

for group in $TEST_GROUPS; do
    echo "=========================================="
    echo "Testing group: $group"
    echo "=========================================="
    
    # Run tests for this group with timeout
    tmpfile=$(mktemp)
    group_timeout=$(group_timeout_for "$group")
    run_with_timeout "$group_timeout" "$TEST_BIN" --test "$group/*" >"$tmpfile" 2>&1
    status=$?
    tail -20 "$tmpfile"
    rm -f "$tmpfile"
    
    if [ "$status" -eq 124 ] || [ "$status" -eq 142 ]; then
        echo "❌ GROUP $group HUNG (timeout after 5 seconds)"
        echo "Running individual tests in this group..."
        
        # Get individual tests in this group
        TESTS=$(
            "$TEST_BIN" --list 2>&1 | \
                awk -v grp="$group" -F/ 'BEGIN{pat="^[[:space:]]*"grp"/"} $0 ~ pat {gsub(/^[[:space:]]+/, "", $0); print $0}'
        )
        
        for test in $TESTS; do
            echo "  Testing: $test"
            tmpfile=$(mktemp)
            test_timeout=$(test_timeout_for "$test")
            run_with_timeout "$test_timeout" "$TEST_BIN" --test "$test" >"$tmpfile" 2>&1
            status=$?
            tail -5 "$tmpfile"
            rm -f "$tmpfile"
            if [ "$status" -eq 124 ] || [ "$status" -eq 142 ]; then
                echo "    ❌ TEST $test HUNG"
            fi
        done
    else
        echo "✓ Group $group completed"
    fi
    
    echo ""
done


