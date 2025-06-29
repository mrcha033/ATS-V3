# ATS-V3 Source Code

This directory contains the source code for the Arbitrage Trading System V3.

## Directory Structure

- `core/`: Contains the core application logic, including the arbitrage engine, price monitor, opportunity detector, trade executor, risk manager, and portfolio manager.
- `data/`: Contains the data models and data access objects for market data, price cache, and other data sources.
- `exchange/`: Contains the exchange-specific implementations for interacting with different cryptocurrency exchanges.
- `monitoring/`: Contains the system monitoring and health check components.
- `network/`: Contains the network clients for interacting with REST and WebSocket APIs.
- `utils/`: Contains utility classes for logging, configuration management, and other common tasks.

## Building the Code

To build the code, you will need a C++ compiler that supports C++17 and the following libraries:

- OpenSSL
- cURL
- nlohmann/json
- jwt-cpp
- googletest (for running the tests)

Once you have installed the dependencies, you can build the code using CMake.