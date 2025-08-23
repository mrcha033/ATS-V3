# Security Module

## Purpose

The `security` module is a cornerstone of the ATS-V3, providing a comprehensive suite of features to protect sensitive data, ensure secure communication, and control access to system resources. It implements various cryptographic operations, authentication mechanisms, authorization policies, and two-factor authentication.

## Key Components

-   **`AuthManager` (`include/auth_manager.hpp`, `src/auth_manager.cpp`)**:
    Manages various authentication and session-related functionalities:
    -   **Exchange API Authentication**: Handles the complex process of signing outgoing requests to cryptocurrency exchanges (e.g., Binance, Upbit, Coinbase, Kraken) using exchange-specific HMAC-SHA256/SHA512 algorithms, timestamps, and nonces. It also verifies incoming signatures from exchanges.
    -   **Internal API Authentication**: Manages API tokens for secure communication between internal ATS-V3 services, including their generation, verification, and revocation.
    -   **JWT Token Management**: Supports the creation and validation of JSON Web Tokens (JWTs) for secure, stateless authentication and authorization.
    -   **Session Management**: Provides robust functionality for creating, validating, updating, and terminating user and service sessions, ensuring secure access control.
    -   **Rate Limiting**: Implements a basic rate-limiting mechanism to protect API endpoints from abuse and denial-of-service attacks.
    -   **Nonce Management**: Utilizes nonces (numbers used once) to prevent replay attacks on authenticated requests.

-   **`CryptoManager` (`include/crypto_manager.hpp`, `src/crypto_manager.cpp`)**:
    The foundational cryptographic component, built upon the OpenSSL library. It provides essential cryptographic primitives:
    -   **AES-256-GCM Encryption/Decryption**: Offers strong symmetric encryption for sensitive data at rest or in transit.
    -   **API Key Management**: Securely stores and retrieves encrypted API keys and secrets for various exchanges. It uses a master key (loaded from a secure file or generated at runtime) to encrypt these credentials, ensuring they are never stored in plaintext.
    -   **HMAC Operations**: Generates and verifies HMAC-SHA256/SHA512 hashes, crucial for message integrity and authentication.
    -   **Key Generation**: Provides utilities for generating cryptographically secure random keys and strings for various security purposes.
    -   **Base64/Hex Encoding/Decoding**: Utility functions for converting binary data to and from common string representations.
    -   **Secure Memory Handling**: Includes `SecurityUtils::secure_zero_memory` to overwrite sensitive data in memory after use, mitigating the risk of data leakage.

-   **`RbacManager` (`include/rbac_manager.hpp`, `src/rbac_manager.cpp`)**:
    Implements a comprehensive Role-Based Access Control (RBAC) system to manage user permissions and access to system resources:
    -   **Permissions**: Defines granular permissions (e.g., `perm_trading_place_order`, `perm_admin_user_management`) with specific resource types, actions, and scopes.
    -   **Roles**: Groups related permissions into roles (e.g., `role_trader`, `role_risk_manager`, `role_admin`).
    -   **Users**: Manages user accounts, assigning roles and direct permissions. Supports user activation/deactivation and password hashing.
    -   **Access Control Queries**: Determines if a user has a specific permission or role, enabling fine-grained access control.
    -   **Context-based Access Control**: Can evaluate access requests based on additional contextual information (e.g., client IP, specific resource ID).
    -   **Session Management**: Manages user sessions within the RBAC context, linking authenticated users to their active permissions.
    -   **Audit Logging**: Logs all access attempts (successful or denied) for security auditing and compliance purposes.
    -   **Security Policies**: Supports defining and enforcing various security policies, such as password complexity rules and session timeouts.
    -   **Persistence**: Stores RBAC data (permissions, roles, users, sessions) persistently to files, encrypted using the `CryptoManager`.

-   **`TlsManager` (`include/tls_manager.hpp`, `src/tls_manager.cpp`)**:
    Manages TLS/SSL certificates and configurations for secure network communication:
    -   **Certificate Generation**: Generates self-signed X.509 certificates and Certificate Signing Requests (CSRs).
    -   **Certificate Loading/Validation**: Loads certificates and private keys from files and validates their authenticity and expiration.
    -   **gRPC TLS Configuration**: Creates `grpc::ServerCredentials` and `grpc::ChannelCredentials` for secure gRPC communication between services.
    -   **SSL Context Creation**: Provides `SSL_CTX` objects for configuring TLS for other network protocols like REST APIs and WebSockets.
    -   **Security Policies**: Enforces secure TLS practices, including setting minimum TLS versions (e.g., TLS 1.2/1.3), configuring strong cipher suites, and enabling Perfect Forward Secrecy (PFS).
    -   **Certificate Renewal**: Checks for certificates nearing expiry and supports renewal processes.

-   **`TotpManager` (`include/totp_manager.hpp`, `src/totp_manager.cpp`)**:
    Implements Time-based One-Time Password (TOTP) for robust two-factor authentication (2FA), compatible with popular authenticator apps like Google Authenticator:
    -   **Secret Key Generation**: Generates and manages TOTP secret keys (Base32 encoded).
    -   **Code Generation/Verification**: Generates and verifies TOTP codes with a configurable time window tolerance.
    -   **Backup Codes**: Generates and manages one-time backup codes for user recovery in case of lost authenticator devices.
    -   **QR Code Generation**: Creates URLs for generating QR codes, simplifying the setup process for users.
    -   **User 2FA Status**: Tracks the 2FA status for each user (enabled, verified, locked).
    -   **Security Features**: Implements lockout mechanisms to prevent brute-force attacks on 2FA codes after a configurable number of failed attempts.
    -   **Persistence**: Stores TOTP secrets and user 2FA status to files, encrypted using the `CryptoManager`.

-   **`SecurityUtils` (nested within `crypto_manager.hpp`)**:
    A collection of general security utility functions, including constant-time comparison (to prevent timing attacks), secure memory zeroing, and input validation helpers.

## Integration with Other Modules

-   **`shared`**: Provides fundamental cryptographic primitives and secure data types that are built upon by the `security` module.
-   **`trading_engine`**: Relies on `AuthManager` for authenticating with exchanges and `RbacManager` for authorizing trading actions. It also uses `TlsManager` for secure gRPC communication.
-   **`risk_manager`**: May use `AuthManager` for internal API authentication and `RbacManager` for controlling access to risk management functionalities.
-   **`ui_dashboard`**: Utilizes `AuthManager` for user login and session management, `RbacManager` for controlling UI element visibility and user actions, and `TotpManager` for 2FA during login.
-   **`notification_service`**: Could potentially use `AuthManager` for authenticating notification requests.

## Design Philosophy

The `security` module is built with a strong emphasis on:
-   **Defense-in-Depth**: Implementing multiple layers of security controls to provide robust protection.
-   **Least Privilege**: Enforcing access control policies to ensure users and services only have the minimum necessary permissions.
-   **Confidentiality, Integrity, Availability**: Protecting data confidentiality through encryption, ensuring data integrity through hashing and digital signatures, and maintaining system availability through robust design.
-   **Compliance**: Adhering to industry best practices and standards for cryptographic operations and access control.
-   **Usability**: Balancing strong security with a user-friendly experience (e.g., easy 2FA setup).

