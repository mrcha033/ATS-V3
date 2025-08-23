# Development Guide

This guide provides instructions for setting up your development environment, building the ATS-V3 project, and contributing to the codebase.

## Prerequisites

Before you begin, ensure you have the following installed:

-   **C++ Compiler**: A C++17 compatible compiler (e.g., GCC 9+, Clang 9+, MSVC 2019+).
-   **CMake**: Version 3.16 or higher.
-   **Conan**: A C/C++ package manager. Install it via pip: `pip install conan`.
-   **Git**: For version control.
-   **Docker & Docker Compose**: Recommended for running the full system and its dependencies.
-   **Qt6**: Required if you plan to build and develop the `ui_dashboard` module. Download from the official Qt website.

## Setting up the Development Environment

1.  **Clone the Repository**:
    ```bash
    git clone https://github.com/your-repo/ATS-V3.git
    cd ATS-V3
    ```

2.  **Install Conan Dependencies**:
    Conan is used to manage third-party C++ libraries. The `conanfile.txt` specifies the project's dependencies.
    ```bash
    conan profile detect --force # Detects your system's default profile
    conan install . --output-folder=build --build=missing
    ```
    This command will download and build (if necessary) all required dependencies into the `build` directory.

3.  **Configure and Build with CMake**:
    Create a build directory and configure CMake. It's recommended to use an out-of-source build.
    ```bash
    mkdir build && cd build
    cmake .. -DCMAKE_BUILD_TYPE=Debug # Use Debug for development
    ```
    If you want to build the UI Dashboard, ensure Qt6 is installed and discoverable by CMake. You might need to set `CMAKE_PREFIX_PATH` to your Qt installation directory.
    ```bash
    # Example for Linux/macOS
    # cmake .. -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x.x/gcc_64
    ```

4.  **Compile the Project**:
    ```bash
    cmake --build . --config Debug -j$(nproc) # Use -j<num_cores> for parallel compilation
    ```
    On Windows with Visual Studio, you can open the generated `.sln` file in Visual Studio and build from there.

## Running Tests

ATS-V3 uses Google Test for unit and integration testing. Tests are located in the `tests/` directory within each module.

To run all tests:

```bash
cd build
ctest --output-on-failure
```

## Code Style and Linting

We adhere to a consistent code style. Please ensure your code follows the project's conventions. Tools like `clang-format` can be used to automatically format your C++ code.

## Contributing

1.  Fork the repository.
2.  Create a new branch for your feature or bug fix.
3.  Make your changes, ensuring they follow the project's code style.
4.  Write or update tests to cover your changes.
5.  Ensure all existing tests pass.
6.  Commit your changes with a clear and concise message.
7.  Push your branch and open a Pull Request.

## Debugging

-   **CMake Debug Build**: Ensure you configure CMake with `-DCMAKE_BUILD_TYPE=Debug` to include debugging symbols.
-   **IDE Integration**: Most modern IDEs (VS Code, CLion, Visual Studio) have excellent CMake integration, allowing you to build, run, and debug directly within the IDE.
-   **Logging**: Utilize the `ats::utils::Logger` (from the `shared` module) for detailed logging. You can configure log levels in `config/settings.json` or via command-line arguments for the `ui_dashboard`.
-   **Docker**: If running services in Docker, you can attach debuggers to running containers (requires specific setup for C++ debugging in Docker).

