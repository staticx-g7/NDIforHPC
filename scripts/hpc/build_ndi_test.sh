#!/bin/bash
# Build NDI Test Streamer for Jupiter HPC
# Uses NDI SDK from download directory

set -e

echo "=========================================="
echo "Building NDI Test Streamer"
echo "=========================================="
echo ""

# Set paths
NDI_PATH="${NDI_PATH:-/home/staticxg7/Downloads/NDI SDK for Linux}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUTPUT_NAME="ndi_test_streamer"

echo "NDI SDK Path: $NDI_PATH"
echo "Script Directory: $SCRIPT_DIR"
echo "Output: $SCRIPT_DIR/$OUTPUT_NAME"
echo ""

# Check if NDI SDK exists
if [ ! -d "$NDI_PATH" ]; then
    echo "✗ NDI SDK not found at: $NDI_PATH"
    echo "  Download from: https://ndi.download.newtek.com/NDI-SDK-for-Linux.tar.gz"
    exit 1
fi

# Check if source file exists
if [ ! -f "$SCRIPT_DIR/ndi_test_streamer.cpp" ]; then
    echo "✗ Source file not found: $SCRIPT_DIR/ndi_test_streamer.cpp"
    exit 1
fi

# Check compiler
if ! command -v g++ &> /dev/null; then
    echo "✗ g++ not found"
    exit 1
fi

echo "✓ Compiler found: $(g++ --version | head -1)"
echo ""

# Check architecture
ARCH=$(uname -m)
echo "Host architecture: $ARCH"

# Select library based on architecture
if [ "$ARCH" = "x86_64" ]; then
    LIB_PATH="$NDI_PATH/lib/x86_64-linux-gnu"
    echo "✓ Using x86_64 library for local testing"
elif [ "$ARCH" = "aarch64" ]; then
    LIB_PATH="$NDI_PATH/lib/aarch64-rpi4-linux-gnueabi"
    echo "✓ Using ARM64 library for Jupiter"
else
    echo "⚠️  Unknown architecture $ARCH, trying default library"
    LIB_PATH="$NDI_PATH/lib"
fi

# Check if library exists
if [ -f "$LIB_PATH/libndi.so" ]; then
    echo "✓ Library found: $LIB_PATH/libndi.so"
    echo "  Architecture: $(file "$LIB_PATH/libndi.so")"
else
    echo "✗ Library not found at: $LIB_PATH/libndi.so"
    echo "Available libraries:"
    find "$NDI_PATH/lib" -name "*.so" | head -10
    exit 1
fi

echo ""

# Compile command
echo "Compiling..."
g++ -std=c++17 "$SCRIPT_DIR/ndi_test_streamer.cpp" \
    -I"$NDI_PATH/include" \
    -L"$LIB_PATH" \
    -lndi \
    -o "$SCRIPT_DIR/$OUTPUT_NAME" \
    -Wl,-rpath,"$LIB_PATH" \
    -lpthread

if [ $? -eq 0 ]; then
    echo ""
    echo "✓ Build successful!"
    echo ""
    echo "Output: $SCRIPT_DIR/$OUTPUT_NAME"
    echo ""
    echo "To run on Jupiter:"
    echo "  cd $SCRIPT_DIR"
    echo "  ./$OUTPUT_NAME"
    echo ""
    echo "The stream will appear as 'Jupiter Test Stream' in NDI receivers."
    echo ""
    echo "Note: On Jupiter, you may need to:"
    echo "  1. Load GCC module: module load GCC"
    echo "  2. Set NDI_PATH: export NDI_PATH=\"/e/scratch/cjsc/george2/NDI/NDI SDK for Linux\""
    echo "  3. Run: ./build_ndi_test.sh"
else
    echo "✗ Build failed"
    exit 1
fi

echo "=========================================="
echo "Build Complete"
echo "=========================================="