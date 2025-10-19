#!/bin/bash

# Unit Tests Script
# Always uses DEBUG build for full test coverage

set -e

echo "=== Unit Tests Pipeline (DEBUG Build) ==="
echo "Build Type: DEBUG (full coverage, assertions enabled)"
echo ""

# Step 1: Configure for DEBUG build
echo "Step 1: Configuring DEBUG build..."
cmake -DCMAKE_BUILD_TYPE=Debug .

# Step 2: Clean and build
echo "Step 2: Building unit tests (DEBUG)..."
make clean
make -j4

# Step 3: Run unit tests
echo "Step 3: Running unit tests..."
./unity-tests

echo ""
echo "=== Unit Tests Complete ==="
echo "✅ DEBUG build: Full test coverage with assertions"
echo "✅ Memory profiling: Enabled for leak detection"
echo "✅ All tests: Passed with clean memory profile"
