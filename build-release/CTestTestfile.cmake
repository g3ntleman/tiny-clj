# CMake generated Testfile for 
# Source directory: /Users/theisen/Projects/tiny-clj
# Build directory: /Users/theisen/Projects/tiny-clj/build-release
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(unit "/Users/theisen/Projects/tiny-clj/build-release/test-unit" "--all")
set_tests_properties(unit PROPERTIES  LABELS "unit" WORKING_DIRECTORY "/Users/theisen/Projects/tiny-clj/build-release" _BACKTRACE_TRIPLES "/Users/theisen/Projects/tiny-clj/CMakeLists.txt;186;add_test;/Users/theisen/Projects/tiny-clj/CMakeLists.txt;0;")
add_test(integration "/Users/theisen/Projects/tiny-clj/build-release/test-integration" "--all")
set_tests_properties(integration PROPERTIES  LABELS "integration" WORKING_DIRECTORY "/Users/theisen/Projects/tiny-clj/build-release" _BACKTRACE_TRIPLES "/Users/theisen/Projects/tiny-clj/CMakeLists.txt;189;add_test;/Users/theisen/Projects/tiny-clj/CMakeLists.txt;0;")
add_test(parser "/Users/theisen/Projects/tiny-clj/build-release/test-parser")
set_tests_properties(parser PROPERTIES  LABELS "parser" WORKING_DIRECTORY "/Users/theisen/Projects/tiny-clj/build-release" _BACKTRACE_TRIPLES "/Users/theisen/Projects/tiny-clj/CMakeLists.txt;192;add_test;/Users/theisen/Projects/tiny-clj/CMakeLists.txt;0;")
add_test(repl-eval-add "/Users/theisen/Projects/tiny-clj/build-release/tiny-clj-repl" "-e" "(+ 1 2)")
set_tests_properties(repl-eval-add PROPERTIES  LABELS "repl" PASS_REGULAR_EXPRESSION "3
" _BACKTRACE_TRIPLES "/Users/theisen/Projects/tiny-clj/CMakeLists.txt;196;add_test;/Users/theisen/Projects/tiny-clj/CMakeLists.txt;0;")
add_test(repl-println-vector "/Users/theisen/Projects/tiny-clj/build-release/tiny-clj-repl" "-e" "(println [1 2 3])")
set_tests_properties(repl-println-vector PROPERTIES  LABELS "repl" PASS_REGULAR_EXPRESSION "println: \\[[0-9 ]+\\][\\r\\n]+nil" _BACKTRACE_TRIPLES "/Users/theisen/Projects/tiny-clj/CMakeLists.txt;199;add_test;/Users/theisen/Projects/tiny-clj/CMakeLists.txt;0;")
add_test(repl-ns-eval "/Users/theisen/Projects/tiny-clj/build-release/tiny-clj-repl" "-n" "user" "-e" "(+ 2 2)")
set_tests_properties(repl-ns-eval PROPERTIES  LABELS "repl" PASS_REGULAR_EXPRESSION "4
" _BACKTRACE_TRIPLES "/Users/theisen/Projects/tiny-clj/CMakeLists.txt;202;add_test;/Users/theisen/Projects/tiny-clj/CMakeLists.txt;0;")
add_test(repl-error-divzero "/Users/theisen/Projects/tiny-clj/build-release/tiny-clj-repl" "-e" "(/ 1 0)")
set_tests_properties(repl-error-divzero PROPERTIES  LABELS "repl" WILL_FAIL "TRUE" _BACKTRACE_TRIPLES "/Users/theisen/Projects/tiny-clj/CMakeLists.txt;205;add_test;/Users/theisen/Projects/tiny-clj/CMakeLists.txt;0;")
