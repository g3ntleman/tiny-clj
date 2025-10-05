#!/bin/bash

# CI/CD Benchmark Script
# Exits with error code if tests fail or benchmarks show regression

set -e  # Exit on any error

echo "=== CI/CD Benchmark Pipeline ==="

# Step 1: Run all tests
echo "Step 1: Running all tests..."
make -f Makefile.benchmark test-all

# Step 2: Run benchmarks
echo "Step 2: Running performance benchmarks..."
make test-benchmark-simple
./test-benchmark-simple > benchmark_output.txt

# Step 3: Extract key metrics
echo "Step 3: Extracting performance metrics..."
PRIMITIVE_OPS=$(grep "Primitive Object Creation" benchmark_output.txt | grep -o '[0-9]* ops/sec' | grep -o '[0-9]*')
TYPE_OPS=$(grep "Type Checking" benchmark_output.txt | grep -o '[0-9]* ops/sec' | grep -o '[0-9]*')
REFCOUNT_OPS=$(grep "Reference Counting" benchmark_output.txt | grep -o '[0-9]* ops/sec' | grep -o '[0-9]*')
VECTOR_OPS=$(grep "Vector Creation" benchmark_output.txt | grep -o '[0-9]* ops/sec' | grep -o '[0-9]*')
MAP_OPS=$(grep "Map Operations" benchmark_output.txt | grep -o '[0-9]* ops/sec' | grep -o '[0-9]*')
FUNCTION_OPS=$(grep "Function Calls" benchmark_output.txt | grep -o '[0-9]* ops/sec' | grep -o '[0-9]*')

echo "Performance Metrics:"
echo "  Primitive Object Creation: $PRIMITIVE_OPS ops/sec"
echo "  Type Checking: $TYPE_OPS ops/sec"
echo "  Reference Counting: $REFCOUNT_OPS ops/sec"
echo "  Vector Creation: $VECTOR_OPS ops/sec"
echo "  Map Operations: $MAP_OPS ops/sec"
echo "  Function Calls: $FUNCTION_OPS ops/sec"

# Step 4: Check for performance regression (example thresholds)
echo "Step 4: Checking for performance regression..."

# Example: Alert if type checking drops below 200M ops/sec
if [ "$TYPE_OPS" -lt 200000000 ]; then
    echo "ERROR: Type checking performance regression detected!"
    echo "Expected: >200M ops/sec, Got: $TYPE_OPS ops/sec"
    exit 1
fi

# Example: Alert if primitive creation drops below 3M ops/sec
if [ "$PRIMITIVE_OPS" -lt 3000000 ]; then
    echo "ERROR: Primitive object creation performance regression detected!"
    echo "Expected: >3M ops/sec, Got: $PRIMITIVE_OPS ops/sec"
    exit 1
fi

echo "✓ All performance checks passed!"
echo "✓ CI/CD benchmark pipeline completed successfully!"

# Cleanup
rm -f benchmark_output.txt
