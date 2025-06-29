# ATS V3 - Raspberry Pi Arbitrage Trading System

A high-performance, automated arbitrage trading system optimized for Raspberry Pi deployment with cross-platform development support.

## 🚀 **Features**

- **Multi-Exchange Support**: Binance, Upbit, and extensible to other exchanges
- **Real-time Price Monitoring**: WebSocket and REST API integration
- **Automated Arbitrage Detection**: Advanced opportunity detection algorithms
- **Risk Management**: Built-in position sizing and risk controls
- **Raspberry Pi Optimized**: ARM-specific optimizations and resource monitoring
- **Cross-Platform Development**: Full Windows MSVC and Linux GCC support
- **Production Ready**: systemd service, health checks, and comprehensive logging
- **Monitoring & Alerts**: System resource monitoring with alert capabilities via Telegram and Discord.

## 📋 **System Requirements**

### **Production (Raspberry Pi)**
- **Raspberry Pi 4** (4GB+ RAM recommended)
- **Ubuntu Server 22.04 LTS** (64-bit) or Raspberry Pi OS
- **SSD Storage** (for better I/O performance vs SD card)
- **Stable Internet Connection**

### **Development (Windows/Linux)**
- **C++20 Compatible Compiler** (GCC 10+, MSVC 2019+, Clang 12+)
- **CMake 3.16+**
- **Visual Studio 2022** (recommended for Windows development)

## 🔧 **Quick Start**

### **1. Clone and Build**

#### **Windows Development**
```powershell
# Clone repository
git clone https://github.com/yourusername/ats-v3.git
cd ats-v3

# Create build directory
mkdir build
cd build

# Configure with CMake
cmake .. -G "Visual Studio 17 2022" -A x64

# Build
cmake --build . --config Release

# Run
.\Release\ATS_V3.exe
```

#### **Linux/Raspberry Pi**
```bash
# Install dependencies
sudo apt update
sudo apt install -y build-essential cmake git libcurl4-openssl-dev libsqlite3-dev pkg-config

# Clone and build
git clone https://github.com/yourusername/ats-v3.git
cd ats-v3

# Build
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j4

# Run
./ATS_V3
```

### **2. Configuration**

First, copy the example configuration file:
```bash
cp config/settings.json.example config/settings.json
```
Next, configure your API keys.

### **3. API Key Security**

For security, it is **strongly recommended** to use environment variables to configure your API keys. This avoids storing them in plain text.

The system will automatically use environment variables if they are present. The expected format is `UPPERCASE_EXCHANGE_NAME_API_KEY` and `UPPERCASE_EXCHANGE_NAME_SECRET_KEY`.

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

### **4. Running the System**

#### **Development Mode**
```bash
# Run with debug logging
./ATS_V3 --debug --log-level=DEBUG

# Run with specific config file
./ATS_V3 --config=config/my_settings.json
```

#### **Production Mode (Linux/Raspberry Pi)**
```bash
# Install as system service
sudo cp systemd/ats-v3.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable ats-v3
sudo systemctl start ats-v3

# Check status
sudo systemctl status ats-v3

# View logs
sudo journalctl -u ats-v3 -f
```

## 🧪 **Testing**

To run the tests, you first need to build the project in `Debug` mode to ensure that the test executable is generated.

### **Linux/Raspberry Pi**
```bash
# Navigate to the build directory
cd build

# Run the tests
ctest
```

### **Windows**
```powershell
# Navigate to the build directory
cd build

# Run the tests
ctest --output-on-failure
```

## 📁 **Project Structure**

```
ats-v3/
├── src/                        # Source code
│   ├── main.cpp               # Application entry point
│   ├── core/                  # Core arbitrage engine
│   ├── exchange/              # Exchange implementations
│   ├── network/               # Network and API clients
│   ├── data/                  # Data structures and storage
│   ├── utils/                 # Utilities (logging, config, JSON)
│   └── monitoring/            # System monitoring
├── config/                    # Configuration files
│   └── settings.json.example  # Example configuration
├── scripts/                   # Build and deployment scripts
├── systemd/                   # System service configuration
└── CMakeLists.txt            # Build configuration
```

## 🤝 **Contributing**

1. Fork the repository
2. Create your feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'Add some amazing feature'`)
4. Push to the branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

## 📄 **License**

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## ⚠️ **Disclaimer**

**This software is for educational and research purposes only. Cryptocurrency trading carries significant financial risk. Users are solely responsible for their trading decisions and any financial losses. The authors are not liable for any damages or losses resulting from the use of this software.**
