# Example STM32 GCC toolchain configuration; adjust paths as needed
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Toolchain prefix and executables (edit for your install)
set(TOOLCHAIN_PREFIX arm-none-eabi)
find_program(CMAKE_C_COMPILER ${TOOLCHAIN_PREFIX}-gcc)
find_program(CMAKE_ASM_COMPILER ${TOOLCHAIN_PREFIX}-gcc)
find_program(CMAKE_AR ${TOOLCHAIN_PREFIX}-ar)
find_program(CMAKE_OBJCOPY ${TOOLCHAIN_PREFIX}-objcopy)
find_program(CMAKE_OBJDUMP ${TOOLCHAIN_PREFIX}-objdump)

# Minimal MCU flags example (adjust per target)
set(MCU_FLAGS "-mcpu=cortex-m3 -mthumb")
set(CMAKE_C_FLAGS_INIT "${MCU_FLAGS} -ffreestanding -fdata-sections -ffunction-sections -Os")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${MCU_FLAGS} -Wl,--gc-sections")
