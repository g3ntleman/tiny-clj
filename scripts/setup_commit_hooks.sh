#!/bin/bash
# Setup Git hooks for automatic code change analysis

echo "Setting up Git hooks for code change analysis..."

# Create .git/hooks directory if it doesn't exist
mkdir -p .git/hooks

# Create post-commit hook for code change analysis
cat > .git/hooks/post-commit << 'EOF'
#!/bin/bash
# Post-commit hook: Analyze code changes after each commit

echo "=== Code Change Analysis ==="
./scripts/count_code_changes.sh HEAD

# Optional: Add to commit message or log file
echo "Code change analysis completed at $(date)" >> .git/logs/code_changes.log
EOF

# Create pre-commit hook for code quality checks
cat > .git/hooks/pre-commit << 'EOF'
#!/bin/bash
# Pre-commit hook: Run basic code quality checks

echo "Running pre-commit checks..."

# Check if there are any C/H files being committed
if git diff --cached --name-only | grep -E '\.(c|h)$' | grep -v '^src/tests/' > /dev/null; then
    echo "C/H production code changes detected."
    
    # Run basic syntax check
    echo "Running syntax check..."
    if ! make clean > /dev/null 2>&1; then
        echo "ERROR: Clean failed"
        exit 1
    fi
    
    if ! make test-unit > /dev/null 2>&1; then
        echo "ERROR: Unit tests failed"
        echo "Please fix test failures before committing"
        exit 1
    fi
    
    echo "Pre-commit checks passed ✓"
else
    echo "No C/H production code changes detected."
fi

echo "Pre-commit checks completed."
EOF

# Make hooks executable
chmod +x .git/hooks/post-commit
chmod +x .git/hooks/pre-commit

echo "Git hooks installed successfully!"
echo
echo "Hooks created:"
echo "  • .git/hooks/post-commit  - Analyzes code changes after commit"
echo "  • .git/hooks/pre-commit  - Runs quality checks before commit"
echo
echo "Usage:"
echo "  git commit -m 'your message'  # Will run both hooks automatically"
echo "  git commit --no-verify       # Skip hooks if needed"
