# Common Include Paths Configuration for CMake
# This file defines common include paths and compiler definitions
# to ensure consistent compilation across different targets.

# Common include directories
set(COMMON_INCLUDE_DIRS
    external
    external/unity/src
    src
)

# Common compiler definitions
set(COMMON_COMPILE_DEFINITIONS
    ENABLE_MEMORY_PROFILING
    DEBUG
    ENABLE_META
)

# Function to apply common settings to a target
function(apply_common_settings target_name)
    target_include_directories(${target_name} PRIVATE ${COMMON_INCLUDE_DIRS})
    target_compile_definitions(${target_name} PRIVATE ${COMMON_COMPILE_DEFINITIONS})
endfunction()
