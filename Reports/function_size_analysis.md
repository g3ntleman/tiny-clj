# Function Size Analysis Report

**Generated:** Tue Oct 21 20:00:52 CEST 2025
**Target:** tiny-clj-stm32
**Build Type:** MinSizeRel
**Target Type:** Mach-O

## Summary

- **Total Executable Size:** 108.8 KB
- **Total Code Size:** 100.9 KB
- **Number of Functions:**      111
- **Top 20 Functions Account for:** 72.00%

## Top 20 Largest Functions

| Rank | Function Name | Size (bytes) | Size (KB) | % of Total |
|------|---------------|--------------|-----------|------------|
| 1 | mh_execute_header | 8480 | 8.2 | 8.00% |
| 2 | ns_define | 7128 | 6.9 | 6.00% |
| 3 | init_special_symbols | 7116 | 6.9 | 6.00% |
| 4 | eval_expr_simple | 5040 | 4.9 | 4.00% |
| 5 | eval_symbol | 3688 | 3.6 | 3.00% |
| 6 | eval_loop_dispatch | 3568 | 3.4 | 3.00% |
| 7 | register_builtins | 3548 | 3.4 | 3.00% |
| 8 | parse_symbol | 3528 | 3.4 | 3.00% |
| 9 | make_object_by_parsing_expr | 3364 | 3.2 | 3.00% |
| 10 | eval_list | 3180 | 3.1 | 3.00% |
| 11 | conj2 | 3164 | 3.0 | 3.00% |
| 12 | eval_fn | 3044 | 2.9 | 2.00% |
| 13 | eval_function_call | 3012 | 2.9 | 2.00% |
| 14 | to_string | 2724 | 2.6 | 2.00% |
| 15 | list_nth | 2688 | 2.6 | 2.00% |
| 16 | native_add_variadic | 2672 | 2.6 | 2.00% |
| 17 | make_named_func | 2596 | 2.5 | 2.00% |
| 18 | native_str | 2284 | 2.2 | 2.00% |
| 19 | reader_skip_block_comment | 2256 | 2.2 | 2.00% |
| 20 | pr_str | 2016 | 1.9 | 1.00% |

## Size Distribution

- **Large Functions (>5KB):** 3 functions, 22.1 KB total
- **Medium Functions (1-5KB):** 20 functions, 55.1 KB total  
- **Small Functions (<1KB):** 88 functions, 23.5 KB total

## Optimization Recommendations

- **mh_execute_header** (8.2 KB): Consider breaking into smaller functions or optimizing algorithm
- **ns_define** (6.9 KB): Consider breaking into smaller functions or optimizing algorithm
- **init_special_symbols** (6.9 KB): Consider breaking into smaller functions or optimizing algorithm
- **eval_expr_simple** (4.9 KB): Consider breaking into smaller functions or optimizing algorithm
- **eval_symbol** (3.6 KB): Consider breaking into smaller functions or optimizing algorithm
- **eval_loop_dispatch** (3.4 KB): Consider breaking into smaller functions or optimizing algorithm
- **register_builtins** (3.4 KB): Consider breaking into smaller functions or optimizing algorithm
- **parse_symbol** (3.4 KB): Consider breaking into smaller functions or optimizing algorithm
- **make_object_by_parsing_expr** (3.2 KB): Consider breaking into smaller functions or optimizing algorithm
- **eval_list** (3.1 KB): Consider breaking into smaller functions or optimizing algorithm

### General Optimization Strategies

1. **Large Functions (>5KB):** Consider breaking into smaller, more focused functions
2. **Frequently Called Functions:** Profile runtime usage to identify hot paths
3. **String Operations:** Look for opportunities to use string interning or const strings
4. **Data Structures:** Consider more compact representations for large data structures
5. **Dead Code Elimination:** Ensure unused functions are properly eliminated by the linker

### Target-Specific Notes

**⚠️ macOS Analysis:** This report analyzes a macOS Mach-O executable, which includes macOS-specific symbols that don't exist on STM32 targets.

**For STM32 Optimization:** Use STM32 toolchain build:
```bash
cmake -DCMAKE_TOOLCHAIN_FILE=toolchains/stm32.cmake -DCMAKE_BUILD_TYPE=MinSizeRel .
make -j4
```

**macOS-specific symbols to ignore:**
- `__mh_execute_header` - Mach-O header (not optimizable)
- `__mh_dylib_header` - Dynamic library header (not optimizable)
- Other `__mh_*` symbols - macOS system symbols


### Next Steps

1. Review the top 10 largest functions for optimization opportunities
2. Consider function inlining for small, frequently called functions
3. Profile runtime to identify which large functions are actually used
4. Look for opportunities to reduce data structure sizes

