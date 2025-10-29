# ESP32 Cross-Compilation Toolchain
# This file configures CMake for ESP32 cross-compilation

# Set the system name
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR xtensa)

# ESP32 Toolchain Path (adjust as needed)
# Must be set via environment variable ESP32_TOOLCHAIN_PATH
set(ESP32_TOOLCHAIN_PATH "$ENV{ESP32_TOOLCHAIN_PATH}")
if(NOT ESP32_TOOLCHAIN_PATH)
    message(FATAL_ERROR "ESP32_TOOLCHAIN_PATH environment variable is not set. "
            "Please set it to the path of your ESP32 toolchain installation. "
            "Example: export ESP32_TOOLCHAIN_PATH=\$HOME/esp/xtensa-esp32-elf-clang")
endif()

# Set the cross compiler
set(CMAKE_C_COMPILER "${ESP32_TOOLCHAIN_PATH}/bin/xtensa-esp32-elf-gcc" CACHE FILEPATH "C compiler")
set(CMAKE_CXX_COMPILER "${ESP32_TOOLCHAIN_PATH}/bin/xtensa-esp32-elf-g++" CACHE FILEPATH "C++ compiler")
set(CMAKE_ASM_COMPILER "${ESP32_TOOLCHAIN_PATH}/bin/xtensa-esp32-elf-gcc" CACHE FILEPATH "ASM compiler")

# Set the linker
set(CMAKE_LINKER "${ESP32_TOOLCHAIN_PATH}/bin/xtensa-esp32-elf-ld" CACHE FILEPATH "Linker")
set(CMAKE_AR "${ESP32_TOOLCHAIN_PATH}/bin/xtensa-esp32-elf-ar" CACHE FILEPATH "Archiver")
set(CMAKE_RANLIB "${ESP32_TOOLCHAIN_PATH}/bin/xtensa-esp32-elf-ranlib" CACHE FILEPATH "Ranlib")
set(CMAKE_STRIP "${ESP32_TOOLCHAIN_PATH}/bin/xtensa-esp32-elf-strip" CACHE FILEPATH "Strip")
set(CMAKE_OBJCOPY "${ESP32_TOOLCHAIN_PATH}/bin/xtensa-esp32-elf-objcopy" CACHE FILEPATH "Objcopy")
set(CMAKE_OBJDUMP "${ESP32_TOOLCHAIN_PATH}/bin/xtensa-esp32-elf-objdump" CACHE FILEPATH "Objdump")

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
add_definitions(-DIDF_VER="4.4.0")

# ESP32 memory layout
set(ESP32_FLASH_SIZE_KB 4096 CACHE STRING "ESP32 Flash size in KB")
set(ESP32_RAM_SIZE_KB 520 CACHE STRING "ESP32 RAM size in KB")

# ESP32 partition table (minimal for Tiny-CLJ)
set(ESP32_PARTITION_TABLE_SIZE_KB 4 CACHE STRING "ESP32 partition table size in KB")

# Calculate available space for application
math(EXPR ESP32_APP_SIZE_KB "${ESP32_FLASH_SIZE_KB} - ${ESP32_PARTITION_TABLE_SIZE_KB}")
set(ESP32_APP_SIZE "${ESP32_APP_SIZE_KB}KB")

message(STATUS "ESP32 Toolchain: ${ESP32_TOOLCHAIN_PATH}")
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
