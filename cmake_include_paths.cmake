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
    MEMORY_PROFILING_ENABLED=${MEMORY_PROFILING_ENABLED_VALUE}
    DEBUG
    META_ENABLED=${META_ENABLED_VALUE}
)

# Function to apply common settings to a target
function(apply_common_settings target_name)
    target_include_directories(${target_name} PRIVATE ${COMMON_INCLUDE_DIRS})
    target_compile_definitions(${target_name} PRIVATE ${COMMON_COMPILE_DEFINITIONS})
endfunction()
