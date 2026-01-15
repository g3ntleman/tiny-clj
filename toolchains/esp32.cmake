# ESP32 Cross-Compilation Toolchain
# This file configures CMake for ESP32 cross-compilation

# Set the system name
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR xtensa)

#
# Toolchain discovery order:
# 1) Prefer an ESP-IDF environment (optionally sourced) which puts Xtensa tools in PATH.
# 2) Fallback to explicit ESP32_TOOLCHAIN_PATH (legacy).
#
# Note: Espressif toolchains may be installed with either prefix:
# - xtensa-esp32-elf-* (older naming)
# - xtensa-esp-elf-*   (current ESP-IDF tools naming)
find_program(ESP32_GCC xtensa-esp32-elf-gcc xtensa-esp-elf-gcc)
find_program(ESP32_GXX xtensa-esp32-elf-g++ xtensa-esp-elf-g++)
find_program(ESP32_AR  xtensa-esp32-elf-ar  xtensa-esp-elf-ar)
find_program(ESP32_RANLIB xtensa-esp32-elf-ranlib xtensa-esp-elf-ranlib)
find_program(ESP32_LD  xtensa-esp32-elf-ld  xtensa-esp-elf-ld)
find_program(ESP32_STRIP xtensa-esp32-elf-strip xtensa-esp-elf-strip)
find_program(ESP32_OBJCOPY xtensa-esp32-elf-objcopy xtensa-esp-elf-objcopy)
find_program(ESP32_OBJDUMP xtensa-esp32-elf-objdump xtensa-esp-elf-objdump)

if(ESP32_GCC)
    # Set the cross compiler from PATH.
    set(CMAKE_C_COMPILER "${ESP32_GCC}" CACHE FILEPATH "C compiler")
    if(ESP32_GXX)
        set(CMAKE_CXX_COMPILER "${ESP32_GXX}" CACHE FILEPATH "C++ compiler")
    endif()
    set(CMAKE_ASM_COMPILER "${ESP32_GCC}" CACHE FILEPATH "ASM compiler")

    if(ESP32_LD)
        set(CMAKE_LINKER "${ESP32_LD}" CACHE FILEPATH "Linker")
    endif()
    if(ESP32_AR)
        set(CMAKE_AR "${ESP32_AR}" CACHE FILEPATH "Archiver")
    endif()
    if(ESP32_RANLIB)
        set(CMAKE_RANLIB "${ESP32_RANLIB}" CACHE FILEPATH "Ranlib")
    endif()
    if(ESP32_STRIP)
        set(CMAKE_STRIP "${ESP32_STRIP}" CACHE FILEPATH "Strip")
    endif()
    if(ESP32_OBJCOPY)
        set(CMAKE_OBJCOPY "${ESP32_OBJCOPY}" CACHE FILEPATH "Objcopy")
    endif()
    if(ESP32_OBJDUMP)
        set(CMAKE_OBJDUMP "${ESP32_OBJDUMP}" CACHE FILEPATH "Objdump")
    endif()
else()
    # ESP32 Toolchain Path (legacy)
    set(ESP32_TOOLCHAIN_PATH "$ENV{ESP32_TOOLCHAIN_PATH}")
    if(NOT ESP32_TOOLCHAIN_PATH)
        message(FATAL_ERROR "ESP32 toolchain not found in PATH and ESP32_TOOLCHAIN_PATH is not set. "
                "If you use ESP-IDF submodule, run: source ./scripts/esp_env.sh "
                "Otherwise set ESP32_TOOLCHAIN_PATH (e.g. export ESP32_TOOLCHAIN_PATH=$HOME/esp/xtensa-esp32-elf).")
    endif()

    # Prefer current prefix; fall back to older prefix if needed.
    if(EXISTS "${ESP32_TOOLCHAIN_PATH}/bin/xtensa-esp-elf-gcc")
        set(_XTENSA_PREFIX "xtensa-esp-elf")
    else()
        set(_XTENSA_PREFIX "xtensa-esp32-elf")
    endif()

    set(CMAKE_C_COMPILER "${ESP32_TOOLCHAIN_PATH}/bin/${_XTENSA_PREFIX}-gcc" CACHE FILEPATH "C compiler")
    set(CMAKE_CXX_COMPILER "${ESP32_TOOLCHAIN_PATH}/bin/${_XTENSA_PREFIX}-g++" CACHE FILEPATH "C++ compiler")
    set(CMAKE_ASM_COMPILER "${ESP32_TOOLCHAIN_PATH}/bin/${_XTENSA_PREFIX}-gcc" CACHE FILEPATH "ASM compiler")

    set(CMAKE_LINKER "${ESP32_TOOLCHAIN_PATH}/bin/${_XTENSA_PREFIX}-ld" CACHE FILEPATH "Linker")
    set(CMAKE_AR "${ESP32_TOOLCHAIN_PATH}/bin/${_XTENSA_PREFIX}-ar" CACHE FILEPATH "Archiver")
    set(CMAKE_RANLIB "${ESP32_TOOLCHAIN_PATH}/bin/${_XTENSA_PREFIX}-ranlib" CACHE FILEPATH "Ranlib")
    set(CMAKE_STRIP "${ESP32_TOOLCHAIN_PATH}/bin/${_XTENSA_PREFIX}-strip" CACHE FILEPATH "Strip")
    set(CMAKE_OBJCOPY "${ESP32_TOOLCHAIN_PATH}/bin/${_XTENSA_PREFIX}-objcopy" CACHE FILEPATH "Objcopy")
    set(CMAKE_OBJDUMP "${ESP32_TOOLCHAIN_PATH}/bin/${_XTENSA_PREFIX}-objdump" CACHE FILEPATH "Objdump")
endif()

# ESP32-specific compiler flags
set(CMAKE_C_FLAGS_INIT "-mlongcalls -DNO_TERMIOS")
set(CMAKE_CXX_FLAGS_INIT "-mlongcalls -DNO_TERMIOS")

# ESP32 linker flags
set(CMAKE_EXE_LINKER_FLAGS_INIT "-mlongcalls")

# Disable some CMake features that don't work well with cross-compilation
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_C_COMPILER_WORKS 1)
set(CMAKE_CXX_COMPILER_WORKS 1)

# ESP32-specific definitions
add_definitions(-DESP32_BUILD)
add_definitions(-DXTENSA_CPU=esp32)
add_definitions(-DIDF_VER="5.3.4")

# ESP32 memory layout
set(ESP32_FLASH_SIZE_KB 4096 CACHE STRING "ESP32 Flash size in KB")
set(ESP32_RAM_SIZE_KB 520 CACHE STRING "ESP32 RAM size in KB")

# ESP32 partition table (minimal for Tiny-CLJ)
set(ESP32_PARTITION_TABLE_SIZE_KB 4 CACHE STRING "ESP32 partition table size in KB")

# Calculate available space for application
math(EXPR ESP32_APP_SIZE_KB "${ESP32_FLASH_SIZE_KB} - ${ESP32_PARTITION_TABLE_SIZE_KB}")
set(ESP32_APP_SIZE "${ESP32_APP_SIZE_KB}KB")

message(STATUS "ESP32 Toolchain: ${CMAKE_C_COMPILER}")
message(STATUS "ESP32 Flash: ${ESP32_FLASH_SIZE_KB}KB, RAM: ${ESP32_RAM_SIZE_KB}KB")
message(STATUS "Available for app: ${ESP32_APP_SIZE}")

# ESP32 optimization flags for size
set(CMAKE_C_FLAGS_RELEASE "-Os -DNDEBUG -ffunction-sections -fdata-sections -mlongcalls")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-Wl,--gc-sections -Wl,--strip-all")

# ESP32 debug flags
set(CMAKE_C_FLAGS_DEBUG "-g -O0 -DDEBUG")
set(CMAKE_EXE_LINKER_FLAGS_DEBUG "")

# ESP32 embedded flags (ultra-compact)
set(CMAKE_C_FLAGS_EMBEDDED "-Os -DNDEBUG -ffunction-sections -fdata-sections -mlongcalls -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-stack-protector -fomit-frame-pointer")
set(CMAKE_EXE_LINKER_FLAGS_EMBEDDED "-Wl,--gc-sections -Wl,--strip-all -Wl,--strip-debug -Wl,--no-export-dynamic -Wl,--build-id=none -mlongcalls")
