#!/bin/bash

# Setup Git Hooks Script
# Installs automatic benchmark execution after commits

set -e

GIT_HOOKS_DIR=".git/hooks"
POST_COMMIT_HOOK="$GIT_HOOKS_DIR/post-commit"
AUTO_BENCHMARK_SCRIPT="scripts/auto_benchmark.sh"

# Colors for output
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${BLUE}=== Setting up Git Hooks for Auto Benchmarking ===${NC}"

# Check if we're in a git repository
if [ ! -d ".git" ]; then
    echo -e "${YELLOW}Warning: Not in a git repository. Creating one...${NC}"
    git init
fi

# Check if auto_benchmark script exists
if [ ! -f "$AUTO_BENCHMARK_SCRIPT" ]; then
    echo -e "${YELLOW}Error: $AUTO_BENCHMARK_SCRIPT not found!${NC}"
    exit 1
fi

# Make sure the benchmark script is executable
chmod +x "$AUTO_BENCHMARK_SCRIPT"

# Create post-commit hook
echo -e "${YELLOW}Creating post-commit hook...${NC}"
cat > "$POST_COMMIT_HOOK" << 'EOF'
#!/bin/bash

# Post-commit hook for automatic benchmarking
# Runs benchmarks after each commit to track performance

# Only run on main branch to avoid noise from feature branches
CURRENT_BRANCH=$(git branch --show-current)
if [ "$CURRENT_BRANCH" != "main" ] && [ "$CURRENT_BRANCH" != "master" ]; then
    echo "Skipping auto-benchmark on branch: $CURRENT_BRANCH"
    exit 0
fi

# Check if auto_benchmark script exists
if [ -f "scripts/auto_benchmark.sh" ]; then
    echo "Running automatic benchmarks..."
    ./scripts/auto_benchmark.sh
else
    echo "Warning: auto_benchmark.sh not found, skipping benchmarks"
fi
EOF

# Make the hook executable
chmod +x "$POST_COMMIT_HOOK"

echo -e "${GREEN}✓ Post-commit hook installed${NC}"

# Create a pre-commit hook for code quality checks
echo -e "${YELLOW}Creating pre-commit hook...${NC}"
cat > "$GIT_HOOKS_DIR/pre-commit" << 'EOF'
#!/bin/bash

# Pre-commit hook for code quality checks

echo "Running pre-commit checks..."

# Check for debug prints in production code
DEBUG_PRINTS=$(grep -r "printf.*DEBUG\|printf.*debug" src/ --exclude-dir=tests 2>/dev/null || true)
if [ -n "$DEBUG_PRINTS" ]; then
    echo "Warning: Debug prints found in production code:"
    echo "$DEBUG_PRINTS"
    echo "Consider removing debug prints before committing."
fi

# Check for TODO/FIXME comments
TODOS=$(grep -r "TODO\|FIXME" src/ 2>/dev/null || true)
if [ -n "$TODOS" ]; then
    echo "Found TODO/FIXME comments:"
    echo "$TODOS"
fi

# Run basic compilation check
if [ -f "CMakeLists.txt" ]; then
    echo "Checking compilation..."
    if ! cmake . > /dev/null 2>&1; then
        echo "Error: CMake configuration failed"
        exit 1
    fi
    
    if ! make > /dev/null 2>&1; then
        echo "Error: Compilation failed"
        exit 1
    fi
    echo "✓ Compilation successful"
fi

echo "✓ Pre-commit checks passed"
EOF

chmod +x "$GIT_HOOKS_DIR/pre-commit"

echo -e "${GREEN}✓ Pre-commit hook installed${NC}"

# Create a script to manually run benchmarks
cat > "scripts/run_benchmarks_now.sh" << 'EOF'
#!/bin/bash

# Manual benchmark runner
# Use this to run benchmarks without committing

echo "Running manual benchmark..."
./scripts/auto_benchmark.sh
EOF

chmod +x "scripts/run_benchmarks_now.sh"

echo -e "${GREEN}✓ Manual benchmark runner created${NC}"

echo ""
echo -e "${GREEN}=== Git Hooks Setup Complete ===${NC}"
echo ""
echo "Installed hooks:"
echo -e "  ${GREEN}✓${NC} post-commit: Runs benchmarks after each commit"
echo -e "  ${GREEN}✓${NC} pre-commit: Runs code quality checks before commit"
echo ""
echo "Usage:"
echo "  - Commit normally: git commit -m 'message' (benchmarks run automatically)"
echo "  - Manual benchmarks: ./scripts/run_benchmarks_now.sh"
echo "  - Disable auto-benchmark: mv .git/hooks/post-commit .git/hooks/post-commit.disabled"
echo "  - Re-enable auto-benchmark: mv .git/hooks/post-commit.disabled .git/hooks/post-commit"
echo ""
echo -e "${YELLOW}Note: Benchmarks only run on main/master branch to avoid noise${NC}"
