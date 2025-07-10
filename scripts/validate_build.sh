#!/bin/bash

# Build Validation Script for ATS-V3
# This script validates the build configuration without actually building

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

cd "$PROJECT_ROOT"

echo "=== ATS-V3 Build Validation ==="
echo "Project root: $PROJECT_ROOT"
echo

# Check directory structure
echo "1. Checking directory structure..."
for dir in src tests config scripts docs; do
    if [ -d "$dir" ]; then
        echo "  ✓ $dir/ directory exists"
    else
        echo "  ✗ $dir/ directory missing"
        exit 1
    fi
done

# Check CMakeLists.txt files
echo
echo "2. Checking CMake configuration files..."
for cmake_file in CMakeLists.txt src/CMakeLists.txt tests/CMakeLists.txt; do
    if [ -f "$cmake_file" ]; then
        echo "  ✓ $cmake_file exists"
    else
        echo "  ✗ $cmake_file missing"
        exit 1
    fi
done

# Check main source files
echo
echo "3. Checking main source files..."
main_files=(
    "src/main.cpp"
    "src/core/types.hpp"
    "src/core/arbitrage_engine.hpp"
    "src/core/arbitrage_engine.cpp"
    "src/exchange/exchange_interface.hpp"
    "src/utils/config_manager.hpp"
    "src/utils/logger.hpp"
)

for file in "${main_files[@]}"; do
    if [ -f "$file" ]; then
        echo "  ✓ $file exists"
    else
        echo "  ✗ $file missing"
        exit 1
    fi
done

# Check new improvement files
echo
echo "4. Checking improvement files..."
improvement_files=(
    "src/core/exceptions.hpp"
    "src/core/result.hpp"
    "src/core/dependency_container.hpp"
    "src/core/dependency_container.cpp"
    "src/utils/structured_logger.hpp"
    "src/utils/structured_logger.cpp"
    "src/utils/config_validator.hpp"
    "src/utils/config_validator.cpp"
    "src/utils/secure_config.hpp"
    "src/utils/secure_config.cpp"
    "src/utils/thread_pool.hpp"
    "src/utils/thread_pool.cpp"
    "src/utils/atomic_counter.hpp"
    "src/monitoring/performance_monitor.hpp"
    "src/monitoring/performance_monitor.cpp"
)

for file in "${improvement_files[@]}"; do
    if [ -f "$file" ]; then
        echo "  ✓ $file exists"
    else
        echo "  ✗ $file missing"
        exit 1
    fi
done

# Check test files
echo
echo "5. Checking test files..."
test_files=(
    "tests/test_main.cpp"
    "tests/test_arbitrage_engine.cpp"
    "tests/integration/test_end_to_end.cpp"
    "tests/integration/test_exchange_integration.cpp"
)

for file in "${test_files[@]}"; do
    if [ -f "$file" ]; then
        echo "  ✓ $file exists"
    else
        echo "  ✗ $file missing"
        exit 1
    fi
done

# Check configuration files
echo
echo "6. Checking configuration files..."
config_files=(
    "config/settings.json.example"
    ".clang-tidy"
    ".cppcheck"
    ".gitignore"
)

for file in "${config_files[@]}"; do
    if [ -f "$file" ]; then
        echo "  ✓ $file exists"
    else
        echo "  ✗ $file missing"
        exit 1
    fi
done

# Check documentation
echo
echo "7. Checking documentation..."
doc_files=(
    "README.md"
    "docs/API.md"
    "LICENSE"
)

for file in "${doc_files[@]}"; do
    if [ -f "$file" ]; then
        echo "  ✓ $file exists"
    else
        echo "  ✗ $file missing"
        exit 1
    fi
done

# Count source files
echo
echo "8. Source file statistics..."
cpp_count=$(find src -name "*.cpp" | wc -l)
hpp_count=$(find src -name "*.hpp" | wc -l)
test_count=$(find tests -name "*.cpp" | wc -l)

echo "  Source files (.cpp): $cpp_count"
echo "  Header files (.hpp): $hpp_count"
echo "  Test files: $test_count"

# Check for build artifacts (should be clean)
echo
echo "9. Checking for build artifacts (should be clean)..."
build_artifacts=(
    "build"
    "CMakeFiles"
    "x64"
    "Debug"
    "Release"
)

clean=true
for artifact in "${build_artifacts[@]}"; do
    if [ -d "$artifact" ] || [ -f "$artifact" ]; then
        echo "  ✗ Found build artifact: $artifact (should be cleaned)"
        clean=false
    fi
done

if [ "$clean" = true ]; then
    echo "  ✓ No build artifacts found (clean)"
fi

# Validate CMake syntax (basic check)
echo
echo "10. Basic CMake syntax validation..."
if grep -q "cmake_minimum_required.*3\.16" CMakeLists.txt; then
    echo "  ✓ CMake version requirement correct (3.16+)"
else
    echo "  ✗ CMake version requirement incorrect"
    exit 1
fi

if grep -q "set.*CMAKE_CXX_STANDARD.*20" CMakeLists.txt; then
    echo "  ✓ C++ standard set to C++20"
else
    echo "  ✗ C++ standard not set to C++20"
    exit 1
fi

echo
echo "=== Validation Complete ==="
echo "✓ Build configuration is valid"
echo "✓ All required files are present"
echo "✓ Directory structure is correct"
echo
echo "To build the project (when CMake is available):"
echo "  mkdir build && cd build"
echo "  cmake .. -DCMAKE_BUILD_TYPE=Release"
echo "  cmake --build ."
echo
echo "To run tests:"
echo "  cd build && ctest --output-on-failure"