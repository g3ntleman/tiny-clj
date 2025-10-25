# ESP32 Cross-Compilation Guide

## Overview
This guide explains how to cross-compile Tiny-CLJ for ESP32 microcontrollers using the Xtensa toolchain.

## ESP32 Specifications
- **Architecture**: Xtensa LX6 (32-bit)
- **Flash Memory**: 4MB (default)
- **RAM**: 520KB
- **Target Binary Size**: <150KB
- **Toolchain**: Xtensa GCC (xtensa-esp32-elf)

## Quick Start

### 1. Setup ESP32 Toolchain
```bash
# Run the automated setup script
./scripts/setup_esp32_toolchain.sh
```

### 2. Build for ESP32
```bash
# Configure for ESP32 cross-compilation
cmake -DCMAKE_TOOLCHAIN_FILE=toolchains/esp32.cmake -DCMAKE_BUILD_TYPE=Release .

# Build the ESP32 target
make tiny-clj-esp32
```

### 3. Check Binary Size
```bash
# Check if we meet the <150KB target
ls -lh tiny-clj-esp32
```

## Manual Setup

### Install ESP32 Toolchain
```bash
# Create ESP32 directory
mkdir -p ~/esp
cd ~/esp

# Download ESP32 toolchain (macOS)
curl -L -o xtensa-esp32-elf.tar.xz \
  "https://github.com/espressif/crosstool-NG/releases/download/esp-2022r1/xtensa-esp32-elf-11.2_20220823-x86_64-apple-darwin.tar.xz"

# Extract toolchain
tar -xf xtensa-esp32-elf.tar.xz
rm xtensa-esp32-elf.tar.xz

# Set environment variable
export ESP32_TOOLCHAIN_PATH="$HOME/esp/xtensa-esp32-elf"
export PATH="$ESP32_TOOLCHAIN_PATH/bin:$PATH"
```

### Linux Setup
```bash
# Download ESP32 toolchain (Linux)
wget -O xtensa-esp32-elf.tar.xz \
  "https://github.com/espressif/crosstool-NG/releases/download/esp-2022r1/xtensa-esp32-elf-11.2_20220823-x86_64-linux-gnu.tar.xz"

# Extract and setup (same as macOS)
tar -xf xtensa-esp32-elf.tar.xz
rm xtensa-esp32-elf.tar.xz
export ESP32_TOOLCHAIN_PATH="$HOME/esp/xtensa-esp32-elf"
export PATH="$ESP32_TOOLCHAIN_PATH/bin:$PATH"
```

## Build Configurations

### Release Build (Optimized for Size)
```bash
cmake -DCMAKE_TOOLCHAIN_FILE=toolchains/esp32.cmake \
      -DCMAKE_BUILD_TYPE=Release .
make tiny-clj-esp32
```

### Debug Build (With Debug Symbols)
```bash
cmake -DCMAKE_TOOLCHAIN_FILE=toolchains/esp32.cmake \
      -DCMAKE_BUILD_TYPE=Debug .
make tiny-clj-esp32
```

### Embedded Build (Ultra-Compact)
```bash
cmake -DCMAKE_TOOLCHAIN_FILE=toolchains/esp32.cmake \
      -DCMAKE_BUILD_TYPE=Embedded .
make tiny-clj-esp32
```

## ESP32-Specific Optimizations

### Compiler Flags
- `-mlongcalls`: Required for ESP32 function calls
- `-Os`: Optimize for size
- `-ffunction-sections -fdata-sections`: Enable dead code elimination
- `-fno-unwind-tables`: Remove unwind tables
- `-fno-stack-protector`: Disable stack protection

### Linker Flags
- `-Wl,--gc-sections`: Remove unused sections
- `-Wl,--strip-all`: Remove all symbols
- `-Wl,--strip-debug`: Remove debug symbols
- `-mlongcalls`: ESP32-specific linker flag

## Memory Layout

### ESP32 Flash (4MB)
```
┌─────────────────┐
│   Bootloader    │ 32KB
├─────────────────┤
│   Partition     │ 4KB
│   Table         │
├─────────────────┤
│   Tiny-CLJ      │ <150KB (Target)
│   Application   │
├─────────────────┤
│   Free Space    │ ~3.8MB
└─────────────────┘
```

### ESP32 RAM (520KB)
```
┌─────────────────┐
│   System RAM    │ ~200KB
├─────────────────┤
│   Tiny-CLJ      │ ~320KB
│   Runtime       │
└─────────────────┘
```

## Size Optimization Strategies

### 1. Compiler Optimizations
- Use `-Os` for size optimization
- Enable LTO (`-flto`) for cross-module optimization
- Remove debug symbols (`-Wl,--strip-all`)

### 2. Feature Reduction
- Disable memory profiling (`DISABLE_MEMORY_PROFILING`)
- Remove error messages (`DISABLE_ERROR_MESSAGES`)
- Disable debug symbols (`DISABLE_DEBUG_SYMBOLS`)
- Remove string formatting (`DISABLE_STRING_FORMATTING`)

### 3. Code Elimination
- Use function/data sections for dead code elimination
- Remove unused functions with `--gc-sections`
- Strip all symbols with `--strip-all`

## Troubleshooting

### Toolchain Not Found
```bash
# Check if toolchain is installed
ls -la ~/esp/xtensa-esp32-elf/bin/

# Verify environment
echo $ESP32_TOOLCHAIN_PATH
which xtensa-esp32-elf-gcc
```

### Build Errors
```bash
# Clean build directory
rm -rf CMakeFiles/ CMakeCache.txt
make clean

# Reconfigure
cmake -DCMAKE_TOOLCHAIN_FILE=toolchains/esp32.cmake .
```

### Size Target Not Met
```bash
# Check current size
ls -lh tiny-clj-esp32

# Analyze binary sections
xtensa-esp32-elf-size tiny-clj-esp32

# Use embedded build type for maximum optimization
cmake -DCMAKE_TOOLCHAIN_FILE=toolchains/esp32.cmake \
      -DCMAKE_BUILD_TYPE=Embedded .
```

## ESP32 Deployment

### Flash to ESP32
```bash
# Using esptool.py (if ESP-IDF is installed)
esptool.py --chip esp32 --port /dev/ttyUSB0 --baud 460800 \
  write_flash 0x10000 tiny-clj-esp32

# Using ESP-IDF tools
idf.py flash
```

### Monitor Output
```bash
# Monitor ESP32 output
idf.py monitor

# Or using esptool
esptool.py --chip esp32 --port /dev/ttyUSB0 monitor
```

## Performance Expectations

### ESP32 Performance
- **CPU**: 240MHz dual-core Xtensa LX6
- **Memory**: 520KB RAM, 4MB Flash
- **Tiny-CLJ**: <150KB binary size target
- **Startup Time**: <1 second
- **Memory Usage**: <320KB RAM

### Optimization Results
- **Current Size**: ~198KB (macOS simulation)
- **Target Size**: <150KB (ESP32 optimized)
- **Reduction Needed**: ~98KB
- **Strategy**: Feature reduction + compiler optimization

## Next Steps

1. **Setup Toolchain**: Run `./scripts/setup_esp32_toolchain.sh`
2. **Build ESP32 Target**: Use ESP32 cross-compilation
3. **Size Analysis**: Check if <150KB target is met
4. **Feature Reduction**: Implement embedded optimizations
5. **Deploy**: Flash to ESP32 hardware

## References

- [ESP32 Technical Reference Manual](https://www.espressif.com/sites/default/files/documentation/esp32_technical_reference_manual_en.pdf)
- [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/)
- [Xtensa Instruction Set Architecture](https://www.tensilica.com/products/xtensa-customizable/overview.html)
