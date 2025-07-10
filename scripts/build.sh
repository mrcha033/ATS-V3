#!/bin/bash

# Simple build script for ATS-V3
# Usage: ./scripts/build.sh [Release|Debug]

BUILD_TYPE=${1:-Release}
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

echo "Building ATS-V3 in $BUILD_TYPE mode..."
echo "Project root: $PROJECT_ROOT"

cd "$PROJECT_ROOT"

# Create build directory
if [ ! -d "build" ]; then
    mkdir build
fi

cd build

# Configure
echo "Configuring with CMake..."
cmake .. -DCMAKE_BUILD_TYPE=$BUILD_TYPE

# Build
echo "Building..."
cmake --build . --config $BUILD_TYPE

echo "Build complete!"
echo "Executable: build/src/ats-v3"
echo "Tests: build/tests/run_tests"