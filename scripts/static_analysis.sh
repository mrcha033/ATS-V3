#!/bin/bash

# Static Analysis Script for ATS-V3
# This script runs various static analysis tools on the codebase

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

cd "$PROJECT_ROOT"

echo "Starting static analysis for ATS-V3..."

# Check if build directory exists
if [ ! -d "build" ]; then
    echo "Build directory not found. Creating it..."
    mkdir -p build
    cd build
    cmake .. -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
    cd ..
fi

# Generate compile commands if not exists
if [ ! -f "build/compile_commands.json" ]; then
    echo "Generating compile commands..."
    cd build
    cmake .. -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
    cd ..
fi

# Create symlink for compile commands in project root
if [ ! -L "compile_commands.json" ]; then
    ln -s build/compile_commands.json compile_commands.json
fi

echo "Running clang-tidy..."
if command -v clang-tidy >/dev/null 2>&1; then
    find src -name "*.cpp" -o -name "*.hpp" | grep -v sqlite3 | xargs clang-tidy -p build/ --quiet
    echo "clang-tidy analysis completed."
else
    echo "clang-tidy not found. Install with: apt install clang-tidy"
fi

echo "Running cppcheck..."
if command -v cppcheck >/dev/null 2>&1; then
    cppcheck --project=compile_commands.json \
             --enable=all \
             --suppress-file=.cppcheck \
             --std=c++20 \
             --platform=unix64 \
             --inline-suppr \
             --quiet \
             --error-exitcode=1 \
             src/
    echo "cppcheck analysis completed."
else
    echo "cppcheck not found. Install with: apt install cppcheck"
fi

echo "Running include-what-you-use..."
if command -v include-what-you-use >/dev/null 2>&1; then
    find src -name "*.cpp" | head -5 | xargs include-what-you-use -p build/
    echo "include-what-you-use analysis completed (sample files only)."
else
    echo "include-what-you-use not found. Install with: apt install iwyu"
fi

echo "Static analysis completed!"
echo "Review the output above for any issues that need to be addressed."