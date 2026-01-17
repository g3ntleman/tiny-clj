#!/bin/bash

# Script to profile test execution using macOS sample tool

if [ $# -eq 0 ]; then
    echo "Usage: $0 <test_name>"
    echo ""
    echo "Examples:"
    echo "  $0 test_namespace/require_nested_path"
    exit 1
fi

TEST_NAME="$1"
DURATION=10  # Fixed 10 seconds sampling duration

# Use build directory for unit-tests
UNIT_TESTS_BIN="./build/unit-tests"
if [ ! -f "$UNIT_TESTS_BIN" ]; then
    echo "Warning: $UNIT_TESTS_BIN not found, searching for unit-tests"
    UNIT_TESTS_BIN=$(find . -name "unit-tests" -type f -executable 2>/dev/null | head -1)
    if [ -z "$UNIT_TESTS_BIN" ]; then
        echo "Error: unit-tests not found. Please build the project first."
        exit 1
    fi
fi

echo "Profiling test: $TEST_NAME"
echo "Using binary: $UNIT_TESTS_BIN"
echo "Sampling duration: ${DURATION} seconds"
echo ""

OUTPUT_FILE="sample_${TEST_NAME//\//_}_$(date +%Y%m%d_%H%M%S).txt"
echo "Output file: $OUTPUT_FILE"
echo ""

# Start sample in background, waiting for unit-tests process
echo "Starting sample (waiting for unit-tests process)..."
sample "$UNIT_TESTS_BIN" $DURATION -wait -mayDie -f "$OUTPUT_FILE" > /dev/null 2>&1 &
SAMPLE_PID=$!

# Give sample a moment to start waiting
sleep 0.2

# Now run the test repeatedly for the sampling duration
echo "Running test repeatedly for ${DURATION} seconds..."
START_TIME=$(date +%s)
TEST_EXIT_CODE=0

while [ $(($(date +%s) - START_TIME)) -lt $DURATION ]; do
    $UNIT_TESTS_BIN --test "$TEST_NAME" > /dev/null 2>&1
    TEST_EXIT_CODE=$?
    if [ $TEST_EXIT_CODE -ne 0 ]; then
        break
    fi
done

# Wait for sample to finish
wait $SAMPLE_PID 2>/dev/null

echo ""
if [ $TEST_EXIT_CODE -eq 0 ]; then
    echo "✓ Test completed successfully"
else
    echo "✗ Test failed with exit code $TEST_EXIT_CODE"
fi

echo ""
echo "Sample output saved to: $OUTPUT_FILE"
echo "View with: less $OUTPUT_FILE"
echo "Or: open -a TextEdit $OUTPUT_FILE"
