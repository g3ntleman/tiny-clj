#!/bin/bash
# Script to count only C/H source code changes in production code
# Ignores CSV files, CMake logs, binary files, and test code

# Function to count lines in C/H files only
count_c_h_changes() {
    local commit_hash="$1"
    
    if [ -z "$commit_hash" ]; then
        echo "Usage: $0 <commit-hash>"
        echo "Example: $0 HEAD"
        exit 1
    fi
    
    echo "=== Code Change Analysis for commit $commit_hash ==="
    echo
    
    # Get changed files in the commit
    local changed_files=$(git show --name-only --pretty=format: "$commit_hash" | grep -E '\.(c|h)$' | grep '^src/' | grep -v '^src/tests/')
    
    if [ -z "$changed_files" ]; then
        echo "No C/H production code changes found."
        return 0
    fi
    
    echo "Changed C/H production files:"
    echo "$changed_files"
    echo
    
    # Count additions and deletions for C/H files only
    local total_added=0
    local total_deleted=0
    
    while IFS= read -r file; do
        if [ -n "$file" ] && [ -f "$file" ]; then
            echo "Analyzing: $file"
            
            # Get diff stats for this file
            local stats=$(git show --numstat "$commit_hash" -- "$file" 2>/dev/null | grep -E '\.(c|h)$' | head -1)
            if [ -n "$stats" ]; then
                local added=$(echo "$stats" | awk '{print $1}')
                local deleted=$(echo "$stats" | awk '{print $2}')
                
                # Handle binary files (marked with -)
                if [ "$added" = "-" ]; then
                    added=0
                fi
                if [ "$deleted" = "-" ]; then
                    deleted=0
                fi
                
                # Convert to numbers, handle empty values
                if [ -z "$added" ] || [ "$added" = "" ]; then
                    added=0
                fi
                if [ -z "$deleted" ] || [ "$deleted" = "" ]; then
                    deleted=0
                fi
                
                echo "  Added: $added lines, Deleted: $deleted lines"
                total_added=$((total_added + added))
                total_deleted=$((total_deleted + deleted))
            else
                echo "  No changes detected"
            fi
        fi
    done <<< "$changed_files"
    
    echo
    echo "=== Summary ==="
    echo "Total C/H production code changes:"
    echo "  Added: $total_added lines"
    echo "  Deleted: $total_deleted lines"
    
    local net_change=$((total_added - total_deleted))
    if [ $net_change -gt 0 ]; then
        echo "  Net change: +$net_change lines of code"
    elif [ $net_change -lt 0 ]; then
        echo "  Net change: $net_change lines of code"
    else
        echo "  Net change: 0 lines of code"
    fi
    
    if [ $total_added -gt 0 ] || [ $total_deleted -gt 0 ]; then
        echo
        echo "Efficiency ratio: $((total_added * 100 / (total_added + total_deleted)))% additions"
    fi
}

# Function to show what files are ignored
show_ignored_files() {
    echo "=== Ignored Files (not counted) ==="
    echo "• CSV files (Reports/*.csv)"
    echo "• CMake logs (CMakeFiles/*, CMakeConfigureLog.yaml)"
    echo "• Binary files (test-*, *.o, executables)"
    echo "• Test code (src/tests/*)"
    echo "• Automatically generated files"
    echo
}

# Main execution
if [ "$1" = "--help" ] || [ "$1" = "-h" ]; then
    echo "Code Change Counter for C/H Production Code"
    echo
    echo "Usage: $0 <commit-hash>"
    echo "       $0 --help"
    echo
    echo "Examples:"
    echo "  $0 HEAD          # Analyze latest commit"
    echo "  $0 HEAD~1        # Analyze previous commit"
    echo "  $0 abc1234        # Analyze specific commit"
    echo
    show_ignored_files
    exit 0
fi

if [ "$1" = "--ignored" ]; then
    show_ignored_files
    exit 0
fi

count_c_h_changes "$1"
