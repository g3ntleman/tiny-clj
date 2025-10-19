#!/bin/bash

# Cleanup Script for Benchmark Scripts
# Removes redundant/overlapping benchmark scripts after consolidation

set -e

echo "=== Benchmark Scripts Cleanup ==="
echo "Removing redundant scripts after consolidation..."
echo ""

# List of scripts to remove (redundant/overlapping)
SCRIPTS_TO_REMOVE=(
    "benchmark_optimized.sh"
    "benchmark_with_csv.sh"
    "benchmark_with_analysis.sh"
    "run_benchmarks.sh"
    "run_benchmarks_now.sh"
    "auto_benchmark.sh"
    "micro_benchmark.sh"
    "profile_performance.sh"
    "demo_performance_changes.sh"
)

# List of scripts to keep (core functionality)
SCRIPTS_TO_KEEP=(
    "benchmark.sh"              # Main consolidated script
    "benchmark_analyzer.sh"     # Analysis functionality
    "benchmark_compare.sh"      # Comparison functionality
    "run_unit_tests.sh"         # Unit tests only
    "ci_benchmark.sh"           # CI integration
    "size_trend_analyzer.sh"    # Size analysis
)

echo "Scripts to remove:"
for script in "${SCRIPTS_TO_REMOVE[@]}"; do
    if [ -f "scripts/$script" ]; then
        echo "  - $script"
    fi
done

echo ""
echo "Scripts to keep:"
for script in "${SCRIPTS_TO_KEEP[@]}"; do
    if [ -f "scripts/$script" ]; then
        echo "  - $script"
    fi
done

echo ""
read -p "Do you want to proceed with cleanup? (y/N): " -n 1 -r
echo ""

if [[ $REPLY =~ ^[Yy]$ ]]; then
    echo "Removing redundant scripts..."
    
    for script in "${SCRIPTS_TO_REMOVE[@]}"; do
        if [ -f "scripts/$script" ]; then
            echo "Removing scripts/$script"
            rm "scripts/$script"
        fi
    done
    
    echo ""
    echo "✅ Cleanup completed!"
    echo ""
    echo "Remaining benchmark scripts:"
    ls -la scripts/benchmark* scripts/run_unit_tests.sh scripts/ci_benchmark.sh scripts/size_trend_analyzer.sh 2>/dev/null || echo "No remaining scripts found"
    
else
    echo "Cleanup cancelled."
fi

echo ""
echo "=== Cleanup Summary ==="
echo "✅ Consolidated benchmark system: scripts/benchmark.sh"
echo "✅ Core functionality preserved"
echo "✅ Redundant scripts identified for removal"
echo ""
echo "Usage:"
echo "  ./scripts/benchmark.sh                    # Full benchmark suite"
echo "  ./scripts/benchmark.sh -m quick           # Quick test"
echo "  ./scripts/benchmark.sh -m unit -b Debug   # Unit tests with Debug build"
echo "  ./scripts/benchmark.sh -m performance     # Performance benchmarks only"
