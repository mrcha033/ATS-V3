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
- **Monitoring & Alerts**: System resource monitoring with alert capabilities

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
- **Optional**: CURL, WebSocket libraries (includes complete implementations if unavailable)

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

### **2. Configuration Setup**

#### **Create Configuration File**
```bash
# Copy example configuration
cp config/settings.json.example config/settings.json

# Edit configuration
nano config/settings.json  # Linux
notepad config/settings.json  # Windows
```

#### **Exchange API Setup**

**Binance API Setup:**
1. Go to [Binance API Management](https://www.binance.com/en/my/settings/api-management)
2. Create new API key
3. Enable "Enable Spot & Margin Trading"
4. Add your IP address to whitelist (optional but recommended)
5. Copy API Key and Secret Key

**Upbit API Setup:**
1. Go to [Upbit API Management](https://upbit.com/mypage/open_api_management)
2. Create new API key
3. Enable "View" and "Trading" permissions
4. Copy Access Key and Secret Key

#### **Update Configuration**
```json
{
  "exchanges": {
    "binance": {
      "name": "binance",
      "enabled": true,
      "api_key": "your_binance_api_key_here",
      "secret_key": "your_binance_secret_key_here",
      "base_url": "https://api.binance.com",
      "testnet": false
    },
    "upbit": {
      "name": "upbit",
      "enabled": true,
      "api_key": "your_upbit_access_key_here",
      "secret_key": "your_upbit_secret_key_here",
      "base_url": "https://api.upbit.com",
      "testnet": false
    }
  },
  "trading": {
    "pairs": [
      "BTC/USDT",
      "ETH/USDT",
      "BNB/USDT"
    ],
    "base_currency": "USDT"
  },
  "arbitrage": {
    "min_profit_threshold": 0.5,
    "max_position_size": 1000.0,
    "max_risk_per_trade": 0.02
  },
  "risk_management": {
    "max_daily_loss": 500.0,
    "max_open_positions": 5,
    "position_size_percent": 0.1
  }
}
```

### **3. Asset Allocation Strategy**

**Recommended Asset Distribution:**

```
Total Investment: $10,000

Binance (50% = $5,000):
├── USDT: $3,500 (70%) - For buying opportunities
├── BTC: $750 (15%) - For selling opportunities
└── ETH: $750 (15%) - For selling opportunities

Upbit (50% = $5,000):
├── KRW: ₩4,200,000 (70%) - For buying opportunities
├── BTC: ₩900,000 (15%) - For selling opportunities
└── ETH: ₩900,000 (15%) - For selling opportunities
```

**Why This Distribution:**
- **70% stable currency** (USDT/KRW) for immediate buying power
- **30% major coins** (BTC/ETH) for immediate selling opportunities
- **Balanced across exchanges** for bidirectional arbitrage

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

## 🔒 **Security Best Practices**

### **API Key Security**
```bash
# Set restrictive permissions on config file
chmod 600 config/settings.json

# Use environment variables (optional)
export BINANCE_API_KEY="your_key_here"
export BINANCE_SECRET_KEY="your_secret_here"
export UPBIT_ACCESS_KEY="your_key_here"
export UPBIT_SECRET_KEY="your_secret_here"
```

### **Network Security**
- **Use VPN** for remote access to Raspberry Pi
- **Enable firewall** and close unnecessary ports
- **Regular security updates** for the operating system
- **Monitor API key usage** for suspicious activity

### **Risk Management**
- **Start with small amounts** ($100-$1000) for testing
- **Set conservative profit thresholds** (0.5% minimum)
- **Monitor daily loss limits** closely
- **Keep manual override** capability

## 📊 **Monitoring and Alerts**

### **Real-time Monitoring**
```bash
# View system resources
htop

# Monitor log files
tail -f logs/ats_v3.log

# Check network connections
netstat -tlnp | grep ATS_V3
```

### **Alert Configuration**
```json
{
  "alerts": {
    "enabled": true,
    "telegram": {
      "enabled": true,
      "bot_token": "your_telegram_bot_token",
      "chat_id": "your_telegram_chat_id"
    },
    "discord": {
      "enabled": false,
      "webhook_url": "your_discord_webhook_url"
    }
  }
}
```

## 🐛 **Troubleshooting**

### **Common Issues**

**Connection Problems:**
```bash
# Test exchange connectivity
curl -I https://api.binance.com/api/v3/ping
curl -I https://api.upbit.com/v1/market/all
```

**Permission Errors:**
```bash
# Fix file permissions
chmod +x ATS_V3
chmod 600 config/settings.json
```

**Memory Issues (Raspberry Pi):**
```bash
# Check memory usage
free -h

# Increase swap space
sudo dphys-swapfile swapoff
sudo nano /etc/dphys-swapfile  # Set CONF_SWAPSIZE=2048
sudo dphys-swapfile setup
sudo dphys-swapfile swapon
```

### **Debug Mode**
```bash
# Run with maximum logging
./ATS_V3 --debug --log-level=TRACE --console-output=true

# Enable core dumps
ulimit -c unlimited
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
├── logs/                      # Log files (created at runtime)
├── data/                      # Database files (created at runtime)
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

**Always:**
- Start with small amounts for testing
- Understand the risks involved
- Comply with local regulations
- Monitor your trades actively
- Use at your own risk