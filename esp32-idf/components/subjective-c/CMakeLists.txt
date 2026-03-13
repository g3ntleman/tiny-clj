idf_component_register(
  SRCS
    "../../../subjective-c/src/platform_allocated_size.c"
    "../../../subjective-c/src/memory_profiler.c"
    "../../../subjective-c/src/debug_trace.c"
    "../../../subjective-c/src/value.c"
    "../../../subjective-c/src/types.c"
    "../../../subjective-c/src/memory.c"
    "../../../subjective-c/src/strings.c"
    "../../../subjective-c/src/mini_format.c"
    "../../../subjective-c/src/vector.c"
    "../../../subjective-c/src/exception.c"
    "../../../subjective-c/src/map.c"
    "../../../subjective-c/src/record.c"
    "../../../subjective-c/src/byte_array.c"
    "../../../subjective-c/src/hashmap.c"
    "../../../subjective-c/src/hashset.c"
    "../../../subjective-c/src/callbacks.c"
    "../../../subjective-c/src/datetime_utc.c"
    "../../../subjective-c/src/instant.c"
    "../../../subjective-c/src/uuid.c"
    "../../../subjective-c/src/regex.c"
    "../../../external/tiny_regex.c"
  INCLUDE_DIRS
    "../../../subjective-c/src"
    "../../../subjective-c/src/subjective-c"
    "../../../src"
    "../../../"
)

# Keep stdout flushing behavior consistent with host builds (helps debugging).
target_compile_definitions(${COMPONENT_LIB} PUBLIC UNITY_USE_FLUSH_STDOUT)
# tinyclj is built with DEBUG=1 and calls debug_trace_allocation from list.c/ast.c.
target_compile_definitions(${COMPONENT_LIB} PUBLIC DEBUG=1)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-clobbered)
