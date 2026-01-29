#!/bin/bash
# Run tests by group to find hanging tests

cd "$(dirname "$0")"

TEST_BIN="./build/unit-tests"

if [ ! -x "$TEST_BIN" ]; then
    echo "❌ $TEST_BIN not found or not executable. Build it first (e.g. cmake --build build/ -t unit-tests)."
    exit 1
fi

# Get all test groups
GROUPS=$(
    "$TEST_BIN" --list 2>&1 | \
        sed -n 's/^[[:space:]]*\(test_[^/[:space:]]*\)\/.*$/\1/p' | \
        sort -u
)

for group in $GROUPS; do
    echo "=========================================="
    echo "Testing group: $group"
    echo "=========================================="
    
    # Run tests for this group with timeout
    timeout 5 "$TEST_BIN" --test "$group/*" 2>&1 | tail -20
    
    if [ $? -eq 124 ]; then
        echo "❌ GROUP $group HUNG (timeout after 5 seconds)"
        echo "Running individual tests in this group..."
        
        # Get individual tests in this group
        TESTS=$(
            "$TEST_BIN" --list 2>&1 | \
                sed -n "s/^[[:space:]]*\(${group}\/[^[:space:]]*\)$/\\1/p"
        )
        
        for test in $TESTS; do
            echo "  Testing: $test"
            timeout 3 "$TEST_BIN" --test "$test" 2>&1 | tail -5
            if [ $? -eq 124 ]; then
                echo "    ❌ TEST $test HUNG"
            fi
        done
    else
        echo "✓ Group $group completed"
    fi
    
    echo ""
done


