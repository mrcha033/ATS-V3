#!/bin/bash

# This script builds the ATS-V3 application on a Raspberry Pi.

# Exit on error
set -e

# Update package lists
sudo apt-get update

# Install dependencies
sudo apt-get install -y build-essential cmake libssl-dev libcurl4-openssl-dev libgtest-dev uuid-dev

# Build the application
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# Run the tests
# ctest

# Install the application
# sudo make install
