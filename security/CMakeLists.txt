# Security Module CMakeLists.txt

# Create security library
add_library(security
    src/crypto_manager.cpp
    src/auth_manager.cpp
    src/tls_manager.cpp
    src/totp_manager.cpp
    src/rbac_manager.cpp
)

# Include directories
target_include_directories(security PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/shared/include
)

# Link dependencies
target_link_libraries(security
    PUBLIC
        OpenSSL::SSL
        OpenSSL::Crypto
        ${CONAN_LIBS}
    PRIVATE
        Threads::Threads
)

# Set properties
set_target_properties(security PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

# Add compiler definitions
target_compile_definitions(security PRIVATE
    OPENSSL_API_COMPAT=0x10100000L
)

# Add compile options for security
target_compile_options(security PRIVATE
    -Wall
    -Wpedantic
    -Werror
    -O2
    -fstack-protector-strong
    -D_FORTIFY_SOURCE=2
)