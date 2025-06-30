# ATS-V3: A High-Frequency Arbitrage Trading System

ATS-V3 is a high-performance, automated arbitrage trading system designed for reliability and speed. It is optimized for deployment on low-power devices like the Raspberry Pi but is fully cross-platform for development on Windows and Linux.

## 🚀 Features

- **Multi-Exchange Support**: Built-in support for Binance and Upbit, with an extensible interface to add more exchanges.
- **Real-time Price Monitoring**: Uses both WebSocket and REST APIs for real-time price data.
- **Automated Arbitrage Detection**: Employs advanced algorithms to detect and evaluate arbitrage opportunities.
- **Robust Risk Management**: Features configurable position sizing, loss limits, and rate limits.
- **Cross-Platform**: Compatible with Windows (MSVC), Linux (GCC), and ARM-based systems.
- **Production-Ready**: Includes `systemd` service files, health checks, and comprehensive logging.
- **Monitoring & Alerts**: Integrated system resource monitoring with alerts via Telegram and Discord.

## ⚠️ Disclaimer

**This software is for educational and research purposes only. Cryptocurrency trading carries significant financial risk. Users are solely responsible for their trading decisions and any financial losses. The authors are not liable for any damages or losses resulting from the use of this software.**

## 🏁 Getting Started

### Prerequisites

Before you begin, ensure you have the following software installed:

- **C++20 Compatible Compiler**:
  - **Linux**: GCC 10+ or Clang 12+
  - **Windows**: Visual Studio 2022 (MSVC 2019+)
- **CMake**: Version 3.16 or higher
- **Git**: For cloning the repository

### Building the Project

#### Windows
```powershell
# 1. Clone the repository
git clone https://github.com/yourusername/ats-v3.git
cd ats-v3

# 2. Create a build directory
mkdir build
cd build

# 3. Configure the project with CMake
cmake .. -G "Visual Studio 17 2022" -A x64

# 4. Build the project
cmake --build . --config Release
```

#### Linux / Raspberry Pi
```bash
# 1. Install dependencies
sudo apt update
sudo apt install -y build-essential cmake git libcurl4-openssl-dev libsqlite3-dev pkg-config

# 2. Clone the repository
git clone https://github.com/yourusername/ats-v3.git
cd ats-v3

# 3. Create a build directory
mkdir build
cd build

# 4. Configure and build the project
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j4
```

### Configuration

1.  **Create a settings file**: Copy the example configuration file to create your own:
    ```bash
    cp config/settings.json.example config/settings.json
    ```
2.  **Set up API keys**: For security, it is **strongly recommended** to use environment variables to configure your API keys. This avoids storing them in plain text. The system will automatically use environment variables if they are present. The expected format is `UPPERCASE_EXCHANGE_NAME_API_KEY` and `UPPERCASE_EXCHANGE_NAME_SECRET_KEY`.

    ```bash
    # Example for Binance
    export BINANCE_API_KEY="your_key_here"
    export BINANCE_SECRET_KEY="your_secret_here"

    # Example for Upbit
    export UPBIT_API_KEY="your_key_here"
    export UPBIT_SECRET_KEY="your_secret_here"
    ```
    If you do not use environment variables, the system will fall back to using the `api_key` and `secret_key` fields in the `config/settings.json` file. If you use the JSON file, secure it with restrictive permissions:
    ```bash
    chmod 600 config/settings.json
    ```

## ⚙️ Usage

### Development Mode
You can run the application directly from the build directory.

```bash
# Run the application
./build/Release/ATS_V3

# Run with debug logging
./build/Release/ATS_V3 --debug --log-level=DEBUG

# Run with a specific config file
./build/Release/ATS_V3 --config=config/my_settings.json
```

### Production Mode (Linux / Raspberry Pi)
The project includes a `systemd` service file for running the application as a background service.

```bash
# 1. Install the service file
sudo cp systemd/ats-v3.service /etc/systemd/system/

# 2. Reload the systemd daemon, enable, and start the service
sudo systemctl daemon-reload
sudo systemctl enable ats-v3
sudo systemctl start ats-v3

# 3. Check the status of the service
sudo systemctl status ats-v3

# 4. View the logs
sudo journalctl -u ats-v3 -f
```

## 🧪 Testing

To run the tests, you first need to build the project in `Debug` mode to ensure that the test executable is generated.

```bash
# 1. Configure the project in Debug mode
cmake .. -DCMAKE_BUILD_TYPE=Debug

# 2. Build the test executable
cmake --build .

# 3. Run the tests
ctest --output-on-failure
```

## 📁 Project Structure

```
ats-v3/
├── src/                # All source code
│   ├── main.cpp        # Application entry point
│   ├── core/           # Core logic for arbitrage, risk, and trading
│   ├── exchange/       # Implementations for different exchanges
│   ├── network/        # REST and WebSocket clients
│   ├── data/           # Data models and database management
│   ├── utils/          # Logging, configuration, and JSON parsing
│   └── monitoring/     # System health and resource monitoring
├── config/             # Configuration files
│   └── settings.json.example # Example configuration
├── scripts/            # Build and deployment scripts
├── systemd/            # Systemd service files for production
├── tests/              # Unit and integration tests
└── .gitattributes      # Attributes for language detection
```

## 🤝 Contributing

Contributions are welcome! Please follow these steps:

1.  Fork the repository.
2.  Create a new branch (`git checkout -b feature/your-feature`).
3.  Make your changes and commit them (`git commit -m 'Add some feature'`).
4.  Push to the branch (`git push origin feature/your-feature`).
5.  Open a pull request.

## 📄 License

This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for details.
