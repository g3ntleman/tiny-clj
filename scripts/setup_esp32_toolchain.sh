#!/bin/bash
# ESP32 Cross-Compiler Setup Script for Tiny-CLJ
# This script sets up the ESP32 toolchain for cross-compilation

set -e

echo "🔧 Setting up ESP32 Cross-Compiler for Tiny-CLJ..."

# Detect OS
if [[ "$OSTYPE" == "darwin"* ]]; then
    OS="macos"
elif [[ "$OSTYPE" == "linux-gnu"* ]]; then
    OS="linux"
else
    echo "❌ Unsupported OS: $OSTYPE"
    exit 1
fi

echo "📱 Detected OS: $OS"

# ESP32 Toolchain Installation Path
ESP32_TOOLCHAIN_PATH="$HOME/esp/xtensa-esp32-elf"

# ESP32 Toolchain URLs (using Espressif's official toolchain)
if [[ "$OS" == "macos" ]]; then
    TOOLCHAIN_URL="https://github.com/espressif/llvm-project/releases/download/esp-14.0.0-20220415/xtensa-esp32-elf-llvm14_0_0-esp-14.0.0-20220415-macos.tar.xz"
elif [[ "$OS" == "linux" ]]; then
    TOOLCHAIN_URL="https://github.com/espressif/llvm-project/releases/download/esp-14.0.0-20220415/xtensa-esp32-elf-llvm14_0_0-esp-14.0.0-20220415-linux-amd64.tar.xz"
fi

# Create ESP32 directory
mkdir -p "$HOME/esp"

# Check if toolchain already exists
if [[ -d "$ESP32_TOOLCHAIN_PATH" ]]; then
    echo "✅ ESP32 toolchain already exists at $ESP32_TOOLCHAIN_PATH"
    echo "🔍 Checking toolchain..."
    if [[ -f "$ESP32_TOOLCHAIN_PATH/bin/xtensa-esp32-elf-gcc" ]]; then
        echo "✅ ESP32 toolchain is properly installed"
        "$ESP32_TOOLCHAIN_PATH/bin/xtensa-esp32-elf-gcc" --version
        exit 0
    else
        echo "⚠️  Toolchain directory exists but is incomplete, reinstalling..."
        rm -rf "$ESP32_TOOLCHAIN_PATH"
    fi
fi

echo "📥 Downloading ESP32 toolchain..."
cd "$HOME/esp"

# Download toolchain
if [[ "$OS" == "macos" ]]; then
    curl -L -o xtensa-esp32-elf.tar.xz "$TOOLCHAIN_URL"
elif [[ "$OS" == "linux" ]]; then
    wget -O xtensa-esp32-elf.tar.xz "$TOOLCHAIN_URL"
fi

echo "📦 Extracting ESP32 toolchain..."
tar -xf xtensa-esp32-elf.tar.xz
rm xtensa-esp32-elf.tar.xz

# Verify installation
if [[ -f "$ESP32_TOOLCHAIN_PATH/bin/xtensa-esp32-elf-gcc" ]]; then
    echo "✅ ESP32 toolchain installed successfully!"
    echo "📍 Location: $ESP32_TOOLCHAIN_PATH"
    echo "🔍 Version:"
    "$ESP32_TOOLCHAIN_PATH/bin/xtensa-esp32-elf-gcc" --version
else
    echo "❌ ESP32 toolchain installation failed"
    exit 1
fi

# Set environment variable
echo "🔧 Setting up environment..."
export ESP32_TOOLCHAIN_PATH="$ESP32_TOOLCHAIN_PATH"

# Add to shell profile
SHELL_PROFILE=""
if [[ -f "$HOME/.zshrc" ]]; then
    SHELL_PROFILE="$HOME/.zshrc"
elif [[ -f "$HOME/.bashrc" ]]; then
    SHELL_PROFILE="$HOME/.bashrc"
elif [[ -f "$HOME/.bash_profile" ]]; then
    SHELL_PROFILE="$HOME/.bash_profile"
fi

if [[ -n "$SHELL_PROFILE" ]]; then
    echo "" >> "$SHELL_PROFILE"
    echo "# ESP32 Toolchain for Tiny-CLJ" >> "$SHELL_PROFILE"
    echo "export ESP32_TOOLCHAIN_PATH=\"$ESP32_TOOLCHAIN_PATH\"" >> "$SHELL_PROFILE"
    echo "export PATH=\"\$ESP32_TOOLCHAIN_PATH/bin:\$PATH\"" >> "$SHELL_PROFILE"
    echo "✅ Added ESP32 toolchain to $SHELL_PROFILE"
fi

echo ""
echo "🎉 ESP32 Cross-Compiler Setup Complete!"
echo ""
echo "📋 Next steps:"
echo "1. Restart your terminal or run: source $SHELL_PROFILE"
echo "2. Build Tiny-CLJ for ESP32:"
echo "   cd /Users/theisen/Projects/tiny-clj"
echo "   cmake -DCMAKE_TOOLCHAIN_FILE=toolchains/esp32.cmake -DCMAKE_BUILD_TYPE=Release ."
echo "   make tiny-clj-esp32"
echo ""
echo "🔧 ESP32 Toolchain Path: $ESP32_TOOLCHAIN_PATH"
echo "📱 Target: ESP32 (Xtensa LX6, 32-bit)"
echo "💾 Flash: 4MB, RAM: 520KB"
echo "🎯 Binary Size Target: <150KB"
