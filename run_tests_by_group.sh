#!/bin/bash
# Run tests by group to find hanging tests

cd "$(dirname "$0")"

# Get all test groups
GROUPS=$(./build/unity-tests --list 2>&1 | grep -E "^test_" | cut -d'/' -f1 | sort -u)

for group in $GROUPS; do
    echo "=========================================="
    echo "Testing group: $group"
    echo "=========================================="
    
    # Run tests for this group with timeout
    timeout 5 ./build/unity-tests "$group/*" 2>&1 | tail -20
    
    if [ $? -eq 124 ]; then
        echo "❌ GROUP $group HUNG (timeout after 5 seconds)"
        echo "Running individual tests in this group..."
        
        # Get individual tests in this group
        TESTS=$(./build/unity-tests --list 2>&1 | grep "^$group/")
        
        for test in $TESTS; do
            echo "  Testing: $test"
            timeout 3 ./build/unity-tests "$test" 2>&1 | tail -5
            if [ $? -eq 124 ]; then
                echo "    ❌ TEST $test HUNG"
            fi
        done
    else
        echo "✓ Group $group completed"
    fi
    
    echo ""
done


