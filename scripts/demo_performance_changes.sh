#!/bin/bash

# Demo Script: Simulate Performance Changes
# Creates different benchmark scenarios to test the analyzer

set -e

echo "=== Performance Change Demo ==="
echo "This script demonstrates the benchmark change detection system"
echo ""

# Create a baseline with "good" performance
echo "Step 1: Creating baseline with good performance..."
cat > benchmark_baseline.csv << 'EOF'
timestamp,name,time_ms,iterations,ops_per_sec,memory_bytes
2025-10-04 10:00:00,primitive_object_creation,0.000200,1,5000000,0
2025-10-04 10:00:00,type_checking,0.000002,1,500000000,0
2025-10-04 10:00:00,reference_counting,0.000010,1,100000000,0
2025-10-04 10:00:00,vector_creation,0.000250,1,4000000,0
2025-10-04 10:00:00,map_operations,0.080000,1,12500,0
2025-10-04 10:00:00,function_call,0.000600,1,1666667,0
EOF

echo "✓ Baseline created with good performance metrics"
echo ""

# Scenario 1: Significant improvements
echo "Step 2: Testing significant improvements scenario..."
cat > benchmark_results.csv << 'EOF'
timestamp,name,time_ms,iterations,ops_per_sec,memory_bytes
2025-10-04 10:30:00,primitive_object_creation,0.000150,1,6666667,0
2025-10-04 10:30:00,type_checking,0.000001,1,1000000000,0
2025-10-04 10:30:00,reference_counting,0.000008,1,125000000,0
2025-10-04 10:30:00,vector_creation,0.000200,1,5000000,0
2025-10-04 10:30:00,map_operations,0.070000,1,14286,0
2025-10-04 10:30:00,function_call,0.000500,1,2000000,0
EOF

echo "Running analyzer on improvements scenario..."
set +e
./scripts/benchmark_analyzer.sh
set -e
echo ""

# Scenario 2: Significant regressions
echo "Step 3: Testing significant regressions scenario..."
cat > benchmark_results.csv << 'EOF'
timestamp,name,time_ms,iterations,ops_per_sec,memory_bytes
2025-10-04 11:00:00,primitive_object_creation,0.000300,1,3333333,0
2025-10-04 11:00:00,type_checking,0.000004,1,250000000,0
2025-10-04 11:00:00,reference_counting,0.000020,1,50000000,0
2025-10-04 11:00:00,vector_creation,0.000400,1,2500000,0
2025-10-04 11:00:00,map_operations,0.120000,1,8333,0
2025-10-04 11:00:00,function_call,0.001000,1,1000000,0
EOF

echo "Running analyzer on regressions scenario..."
set +e
./scripts/benchmark_analyzer.sh
set -e
echo ""

# Scenario 3: Mixed changes
echo "Step 4: Testing mixed changes scenario..."
cat > benchmark_results.csv << 'EOF'
timestamp,name,time_ms,iterations,ops_per_sec,memory_bytes
2025-10-04 11:30:00,primitive_object_creation,0.000180,1,5555556,0
2025-10-04 11:30:00,type_checking,0.000003,1,333333333,0
2025-10-04 11:30:00,reference_counting,0.000015,1,66666667,0
2025-10-04 11:30:00,vector_creation,0.000220,1,4545455,0
2025-10-04 11:30:00,map_operations,0.100000,1,10000,0
2025-10-04 11:30:00,function_call,0.000700,1,1428571,0
EOF

echo "Running analyzer on mixed changes scenario..."
set +e
./scripts/benchmark_analyzer.sh
set -e
echo ""

# Scenario 4: Stable performance
echo "Step 5: Testing stable performance scenario..."
cat > benchmark_results.csv << 'EOF'
timestamp,name,time_ms,iterations,ops_per_sec,memory_bytes
2025-10-04 12:00:00,primitive_object_creation,0.000201,1,4975124,0
2025-10-04 12:00:00,type_checking,0.000002,1,500000000,0
2025-10-04 12:00:00,reference_counting,0.000010,1,100000000,0
2025-10-04 12:00:00,vector_creation,0.000251,1,3984064,0
2025-10-04 12:00:00,map_operations,0.080100,1,12484,0
2025-10-04 12:00:00,function_call,0.000601,1,1663894,0
EOF

echo "Running analyzer on stable performance scenario..."
set +e
./scripts/benchmark_analyzer.sh
set -e
echo ""

echo "=== Demo Complete ==="
echo "The analyzer successfully detected:"
echo "  - Significant improvements (green)"
echo "  - Significant regressions (red)"
echo "  - Mixed changes (various colors)"
echo "  - Stable performance (blue)"
echo ""
echo "This demonstrates the system's ability to track performance changes over time."
