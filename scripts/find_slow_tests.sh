#!/bin/bash

# Script to find the slowest tests by sampling execution time

echo "Finding slowest tests..."
echo ""

# Get list of all tests
./build/unity-tests --list > /tmp/test_list.txt 2>&1

# Sample each test individually and measure time
slow_tests=()
while IFS= read -r test_name; do
    if [[ -z "$test_name" ]] || [[ "$test_name" == *"Tests registered"* ]] || [[ "$test_name" == *"---"* ]]; then
        continue
    fi
    
    # Run test and measure time
    start_time=$(date +%s.%N)
    ./build/unity-tests --test "$test_name" > /dev/null 2>&1
    end_time=$(date +%s.%N)
    
    duration=$(echo "$end_time - $start_time" | bc)
    
    # Only record tests that take more than 0.01 seconds
    if (( $(echo "$duration > 0.01" | bc -l) )); then
        slow_tests+=("$duration $test_name")
    fi
done < /tmp/test_list.txt

# Sort by duration (slowest first) and show top 20
echo "Top 20 slowest tests:"
echo "===================="
printf '%s\n' "${slow_tests[@]}" | sort -rn | head -20


