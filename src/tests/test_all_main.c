/*
 * Test All Main
 * 
 * Wrapper executable that runs all test categories (unit, integration, benchmark)
 * for comprehensive testing and CI/CD integration.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

// ============================================================================
// COMMAND LINE INTERFACE
// ============================================================================

void print_usage(const char *program_name) {
    printf("Usage: %s [options]\n", program_name);
    printf("Options:\n");
    printf("  --help, -h          Show this help message\n");
    printf("  --unit, -u          Run unit tests only\n");
    printf("  --integration, -i   Run integration tests only\n");
    printf("  --benchmark, -b     Run benchmark tests only\n");
    printf("  --all, -a           Run all tests (default)\n");
    printf("  --quick, -q         Run quick tests (unit + integration)\n");
    printf("  --verbose, -v       Verbose output\n");
    printf("  --parallel, -p      Run tests in parallel\n");
    printf("  --report, -r        Generate comprehensive report\n");
}

int run_test_executable(const char *executable, const char *args[], int verbose) {
    if (verbose) {
        printf("Running: %s", executable);
        for (int i = 0; args[i] != NULL; i++) {
            printf(" %s", args[i]);
        }
        printf("\n");
    }
    
    pid_t pid = fork();
    if (pid == 0) {
        // Child process
        execvp(executable, (char *const *)args);
        perror("execvp failed");
        exit(1);
    } else if (pid > 0) {
        // Parent process
        int status;
        waitpid(pid, &status, 0);
        return WEXITSTATUS(status);
    } else {
        perror("fork failed");
        return 1;
    }
}

int run_unit_tests(int verbose) {
    printf("=== Running Unit Tests ===\n\n");
    const char *args[] = {"./test-unit", NULL};
    return run_test_executable("./test-unit", args, verbose);
}

int run_integration_tests(int verbose) {
    printf("=== Running Integration Tests ===\n\n");
    const char *args[] = {"./test-integration", NULL};
    return run_test_executable("./test-integration", args, verbose);
}

int run_benchmark_tests(int verbose) {
    printf("=== Running Benchmark Tests ===\n\n");
    const char *args[] = {"./test-benchmark", NULL};
    return run_test_executable("./test-benchmark", args, verbose);
}

int run_quick_tests(int verbose) {
    printf("=== Running Quick Tests (Unit + Integration) ===\n\n");
    
    int unit_result = run_unit_tests(verbose);
    if (unit_result != 0) {
        printf("Unit tests failed with exit code: %d\n", unit_result);
        return unit_result;
    }
    
    int integration_result = run_integration_tests(verbose);
    if (integration_result != 0) {
        printf("Integration tests failed with exit code: %d\n", integration_result);
        return integration_result;
    }
    
    printf("Quick tests completed successfully!\n");
    return 0;
}

int run_all_tests(int verbose) {
    printf("=== Running All Tests ===\n\n");
    
    int unit_result = run_unit_tests(verbose);
    int integration_result = run_integration_tests(verbose);
    int benchmark_result = run_benchmark_tests(verbose);
    
    printf("\n=== Test Results Summary ===\n");
    printf("Unit Tests:        %s\n", unit_result == 0 ? "PASS" : "FAIL");
    printf("Integration Tests: %s\n", integration_result == 0 ? "PASS" : "FAIL");
    printf("Benchmark Tests:   %s\n", benchmark_result == 0 ? "PASS" : "FAIL");
    
    if (unit_result == 0 && integration_result == 0 && benchmark_result == 0) {
        printf("\nAll tests passed! ✅\n");
        return 0;
    } else {
        printf("\nSome tests failed! ❌\n");
        return 1;
    }
}

int run_parallel_tests(int verbose) {
    printf("=== Running Tests in Parallel ===\n\n");
    
    pid_t unit_pid = fork();
    if (unit_pid == 0) {
        exit(run_unit_tests(verbose));
    }
    
    pid_t integration_pid = fork();
    if (integration_pid == 0) {
        exit(run_integration_tests(verbose));
    }
    
    pid_t benchmark_pid = fork();
    if (benchmark_pid == 0) {
        exit(run_benchmark_tests(verbose));
    }
    
    // Wait for all child processes
    int unit_status, integration_status, benchmark_status;
    waitpid(unit_pid, &unit_status, 0);
    waitpid(integration_pid, &integration_status, 0);
    waitpid(benchmark_pid, &benchmark_status, 0);
    
    int unit_result = WEXITSTATUS(unit_status);
    int integration_result = WEXITSTATUS(integration_status);
    int benchmark_result = WEXITSTATUS(benchmark_status);
    
    printf("\n=== Parallel Test Results Summary ===\n");
    printf("Unit Tests:        %s\n", unit_result == 0 ? "PASS" : "FAIL");
    printf("Integration Tests: %s\n", integration_result == 0 ? "PASS" : "FAIL");
    printf("Benchmark Tests:   %s\n", benchmark_result == 0 ? "PASS" : "FAIL");
    
    if (unit_result == 0 && integration_result == 0 && benchmark_result == 0) {
        printf("\nAll parallel tests passed! ✅\n");
        return 0;
    } else {
        printf("\nSome parallel tests failed! ❌\n");
        return 1;
    }
}

int generate_report(int verbose) {
    printf("=== Generating Comprehensive Test Report ===\n\n");
    
    // Run all tests and collect results
    int result = run_all_tests(verbose);
    
    // Generate detailed report
    printf("\n=== Generating Report Files ===\n");
    
    // Unit test report
    const char *unit_args[] = {"./test-unit", "--report", NULL};
    run_test_executable("./test-unit", unit_args, verbose);
    
    // Integration test report
    const char *integration_args[] = {"./test-integration", "--report", NULL};
    run_test_executable("./test-integration", integration_args, verbose);
    
    // Benchmark report
    const char *benchmark_args[] = {"./test-benchmark", "--report", NULL};
    run_test_executable("./test-benchmark", benchmark_args, verbose);
    
    printf("Comprehensive test report generated!\n");
    return result;
}

// ============================================================================
// MAIN FUNCTION
// ============================================================================

int main(int argc, char *argv[]) {
    printf("=== Tiny-Clj Test All Runner ===\n\n");
    
    int verbose = 0;
    int parallel = 0;
    
    // Parse command line arguments
    if (argc == 1) {
        // No arguments - run all tests
        return run_all_tests(verbose);
    }
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--unit") == 0 || strcmp(argv[i], "-u") == 0) {
            return run_unit_tests(verbose);
        } else if (strcmp(argv[i], "--integration") == 0 || strcmp(argv[i], "-i") == 0) {
            return run_integration_tests(verbose);
        } else if (strcmp(argv[i], "--benchmark") == 0 || strcmp(argv[i], "-b") == 0) {
            return run_benchmark_tests(verbose);
        } else if (strcmp(argv[i], "--all") == 0 || strcmp(argv[i], "-a") == 0) {
            return run_all_tests(verbose);
        } else if (strcmp(argv[i], "--quick") == 0 || strcmp(argv[i], "-q") == 0) {
            return run_quick_tests(verbose);
        } else if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) {
            verbose = 1;
        } else if (strcmp(argv[i], "--parallel") == 0 || strcmp(argv[i], "-p") == 0) {
            parallel = 1;
        } else if (strcmp(argv[i], "--report") == 0 || strcmp(argv[i], "-r") == 0) {
            return generate_report(verbose);
        } else {
            printf("Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }
    
    // Handle parallel flag
    if (parallel) {
        return run_parallel_tests(verbose);
    }
    
    return 0;
}
