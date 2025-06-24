#!/bin/bash

# ATS V3 Raspberry Pi Build Script
# Optimized for ARM architecture compilation

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}ATS V3 Raspberry Pi Build Script${NC}"
echo "=================================="

# Detect architecture
ARCH=$(uname -m)
echo "Detected architecture: $ARCH"

# Build configuration
BUILD_TYPE=${1:-Release}
BUILD_DIR="build"
INSTALL_DIR="/opt/ats-v3"

echo "Build type: $BUILD_TYPE"
echo "Build directory: $BUILD_DIR"

# Check if running on Raspberry Pi
if [[ "$ARCH" == "armv7l" || "$ARCH" == "aarch64" ]]; then
    echo -e "${GREEN}Building for Raspberry Pi ($ARCH)${NC}"
    PI_BUILD=true
else
    echo -e "${YELLOW}Building for non-Pi architecture ($ARCH)${NC}"
    PI_BUILD=false
fi

# Create build directory
echo "Creating build directory..."
rm -rf $BUILD_DIR
mkdir -p $BUILD_DIR
cd $BUILD_DIR

# Configure CMake with Pi optimizations
echo "Configuring CMake..."
if [ "$PI_BUILD" = true ]; then
    # Raspberry Pi specific optimizations
    CMAKE_FLAGS=(
        -DCMAKE_BUILD_TYPE=$BUILD_TYPE
        -DCMAKE_CXX_FLAGS_RELEASE="-O3 -DNDEBUG -march=armv8-a+crc+simd -mtune=cortex-a72 -flto"
        -DCMAKE_C_FLAGS_RELEASE="-O3 -DNDEBUG -march=armv8-a+crc+simd -mtune=cortex-a72 -flto"
        -DCMAKE_INSTALL_PREFIX=$INSTALL_DIR
        -DBUILD_TESTING=OFF
    )
else
    # Generic build flags
    CMAKE_FLAGS=(
        -DCMAKE_BUILD_TYPE=$BUILD_TYPE
        -DCMAKE_INSTALL_PREFIX=$INSTALL_DIR
        -DBUILD_TESTING=OFF
    )
fi

cmake "${CMAKE_FLAGS[@]}" ..

# Build
echo "Building ATS V3..."
CORES=$(nproc)
echo "Using $CORES CPU cores for build"

make -j$CORES

echo -e "${GREEN}Build completed successfully!${NC}"

# Optional: Install
if [ "$2" = "install" ]; then
    echo "Installing ATS V3..."
    sudo make install
    
    # Set up systemd service
    if [ -f "../systemd/ats-v3.service" ]; then
        echo "Installing systemd service..."
        sudo cp ../systemd/ats-v3.service /etc/systemd/system/
        sudo systemctl daemon-reload
        echo -e "${GREEN}Systemd service installed${NC}"
        echo "Use 'sudo systemctl enable ats-v3' to enable auto-start"
        echo "Use 'sudo systemctl start ats-v3' to start the service"
    fi
    
    echo -e "${GREEN}Installation completed!${NC}"
fi

# Performance info for Pi
if [ "$PI_BUILD" = true ]; then
    echo ""
    echo -e "${YELLOW}Raspberry Pi Performance Tips:${NC}"
    echo "- Use an SSD instead of SD card for better I/O performance"
    echo "- Ensure adequate cooling (CPU temperature < 70°C)"
    echo "- Consider increasing GPU memory split for better performance"
    echo "- Monitor CPU throttling with 'vcgencmd get_throttled'"
fi

echo ""
echo -e "${GREEN}Build script completed!${NC}"