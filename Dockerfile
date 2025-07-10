FROM ubuntu:22.04

# Install dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    wget \
    python3 \
    python3-pip \
    libssl-dev \
    libcurl4-openssl-dev \
    libboost-all-dev \
    redis-server \
    && rm -rf /var/lib/apt/lists/*

# Install Conan
RUN pip3 install conan

# Create app directory
WORKDIR /app

# Copy conanfile first for better caching
COPY conanfile.txt .

# Install dependencies
RUN conan profile detect --force
RUN conan install . --build=missing

# Copy source code
COPY . .

# Build the application
RUN mkdir build && cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Release && \
    make -j$(nproc)

# Create runtime user
RUN useradd -m -s /bin/bash ats

# Change ownership
RUN chown -R ats:ats /app

# Switch to runtime user
USER ats

# Expose port for dashboard
EXPOSE 8080

# Run the application
CMD ["./build/ats-v3"]