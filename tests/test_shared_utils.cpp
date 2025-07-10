#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "utils/logger.hpp"
#include "utils/crypto_utils.hpp"
#include "config/config_manager.hpp"
#include "types/common_types.hpp"
#include <filesystem>
#include <fstream>

using namespace ats;
using namespace ats::utils;
using namespace ats::config;
using namespace ats::types;

// Test fixtures
class LoggerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create test logs directory
        std::filesystem::create_directories("test_logs");
    }
    
    void TearDown() override {
        Logger::shutdown();
        // Clean up test logs
        std::filesystem::remove_all("test_logs");
    }
};

class CryptoUtilsTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_key = CryptoUtils::generate_aes_key();
        test_data = "Hello, ATS Crypto Test!";
    }
    
    std::vector<uint8_t> test_key;
    std::string test_data;
};

class ConfigManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_config_file = "test_config.json";
        config_manager = std::make_unique<ConfigManager>();
    }
    
    void TearDown() override {
        std::filesystem::remove(test_config_file);
    }
    
    std::string test_config_file;
    std::unique_ptr<ConfigManager> config_manager;
};

// Logger Tests
TEST_F(LoggerTest, InitializationAndBasicLogging) {
    EXPECT_NO_THROW(Logger::initialize("test_logs/test.log", LogLevel::DEBUG));
    
    // Test basic logging
    EXPECT_NO_THROW(Logger::info("Test info message"));
    EXPECT_NO_THROW(Logger::debug("Test debug message"));
    EXPECT_NO_THROW(Logger::warn("Test warning message"));
    EXPECT_NO_THROW(Logger::error("Test error message"));
    
    // Test log level
    EXPECT_EQ(Logger::get_level(), LogLevel::DEBUG);
    EXPECT_TRUE(Logger::is_enabled(LogLevel::INFO));
    EXPECT_TRUE(Logger::is_enabled(LogLevel::DEBUG));
}

TEST_F(LoggerTest, LogLevelFiltering) {
    Logger::initialize("test_logs/level_test.log", LogLevel::WARN);
    
    EXPECT_EQ(Logger::get_level(), LogLevel::WARN);
    EXPECT_FALSE(Logger::is_enabled(LogLevel::DEBUG));
    EXPECT_FALSE(Logger::is_enabled(LogLevel::INFO));
    EXPECT_TRUE(Logger::is_enabled(LogLevel::WARN));
    EXPECT_TRUE(Logger::is_enabled(LogLevel::ERROR));
}

TEST_F(LoggerTest, TradingLoggerFunctions) {
    Logger::initialize("test_logs/trading_test.log", LogLevel::INFO);
    
    EXPECT_NO_THROW(TradingLogger::log_order_created("binance", "BTC/USDT", "order123", "BUY", 0.1, 50000.0));
    EXPECT_NO_THROW(TradingLogger::log_order_filled("binance", "BTC/USDT", "order123", 0.1, 50000.0));
    EXPECT_NO_THROW(TradingLogger::log_arbitrage_opportunity("BTC/USDT", "binance", "upbit", 50000.0, 50500.0, 1.0, 50.0));
    EXPECT_NO_THROW(TradingLogger::log_risk_alert("MAX_DRAWDOWN", "Portfolio drawdown exceeded", 0.06, 0.05));
}

TEST_F(LoggerTest, ScopedTimer) {
    Logger::initialize("test_logs/timer_test.log", LogLevel::DEBUG);
    
    {
        ScopedTimer timer("test_operation");
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } // Timer should log the duration here
    
    // No exception should be thrown
    SUCCEED();
}

// Crypto Utils Tests
TEST_F(CryptoUtilsTest, RandomGeneration) {
    auto random_bytes = CryptoUtils::generate_random_bytes(32);
    EXPECT_EQ(random_bytes.size(), 32);
    
    auto aes_key = CryptoUtils::generate_aes_key();
    EXPECT_EQ(aes_key.size(), 32);
    
    auto iv = CryptoUtils::generate_iv();
    EXPECT_EQ(iv.size(), 12);
}

TEST_F(CryptoUtilsTest, AES_GCM_EncryptionDecryption) {
    std::vector<uint8_t> plaintext(test_data.begin(), test_data.end());
    
    // Encrypt
    auto encrypted = CryptoUtils::encrypt_aes_gcm(plaintext, test_key);
    EXPECT_TRUE(encrypted.success);
    EXPECT_FALSE(encrypted.encrypted_data.empty());
    EXPECT_EQ(encrypted.iv.size(), 12);
    EXPECT_EQ(encrypted.tag.size(), 16);
    
    // Decrypt
    auto decrypted = CryptoUtils::decrypt_aes_gcm(encrypted.encrypted_data, test_key, encrypted.iv, encrypted.tag);
    EXPECT_TRUE(decrypted.success);
    EXPECT_EQ(decrypted.decrypted_data, plaintext);
}

TEST_F(CryptoUtilsTest, HMAC_SHA256) {
    std::string key = "test_key";
    std::string message = "test_message";
    
    auto hmac_bytes = CryptoUtils::hmac_sha256(
        std::vector<uint8_t>(message.begin(), message.end()),
        std::vector<uint8_t>(key.begin(), key.end())
    );
    
    EXPECT_EQ(hmac_bytes.size(), 32); // SHA256 produces 32 bytes
    
    auto hmac_hex = CryptoUtils::hmac_sha256_hex(message, key);
    EXPECT_EQ(hmac_hex.length(), 64); // 32 bytes = 64 hex chars
    
    auto hmac_base64 = CryptoUtils::hmac_sha256_base64(message, key);
    EXPECT_FALSE(hmac_base64.empty());
}

TEST_F(CryptoUtilsTest, Base64_Encoding) {
    std::vector<uint8_t> data = {0x48, 0x65, 0x6C, 0x6C, 0x6F}; // "Hello"
    
    auto encoded = CryptoUtils::base64_encode(data);
    EXPECT_EQ(encoded, "SGVsbG8=");
    
    auto decoded = CryptoUtils::base64_decode(encoded);
    EXPECT_EQ(decoded, data);
}

TEST_F(CryptoUtilsTest, Hex_Encoding) {
    std::vector<uint8_t> data = {0x48, 0x65, 0x6C, 0x6C, 0x6F}; // "Hello"
    
    auto encoded = CryptoUtils::hex_encode(data);
    EXPECT_EQ(encoded, "48656c6c6f");
    
    auto decoded = CryptoUtils::hex_decode(encoded);
    EXPECT_EQ(decoded, data);
}

TEST_F(CryptoUtilsTest, ExchangeSignatures) {
    std::string query = "symbol=BTCUSDT&side=BUY&type=MARKET&quantity=0.1";
    std::string secret = "test_secret";
    
    auto binance_sig = CryptoUtils::generate_binance_signature(query, secret);
    EXPECT_FALSE(binance_sig.empty());
    EXPECT_EQ(binance_sig.length(), 64); // SHA256 hex = 64 chars
    
    auto upbit_sig = CryptoUtils::generate_upbit_signature("access_key", secret, query);
    EXPECT_FALSE(upbit_sig.empty());
}

TEST_F(CryptoUtilsTest, SecureString) {
    {
        SecureString secure_str("sensitive_data");
        EXPECT_EQ(secure_str.size(), 14);
        EXPECT_FALSE(secure_str.empty());
        EXPECT_STREQ(secure_str.c_str(), "sensitive_data");
    }
    
    SecureString empty_str(10);
    EXPECT_EQ(empty_str.size(), 10);
    EXPECT_FALSE(empty_str.empty());
    
    empty_str.clear();
    EXPECT_EQ(empty_str.size(), 0);
}

// Config Manager Tests
TEST_F(ConfigManagerTest, DefaultConfiguration) {
    auto trading_config = config_manager->get_trading_config();
    EXPECT_FALSE(trading_config.enabled);
    EXPECT_EQ(trading_config.min_spread_threshold, 0.005);
    EXPECT_EQ(trading_config.max_position_size, 1000.0);
    
    auto risk_config = config_manager->get_risk_config();
    EXPECT_EQ(risk_config.max_portfolio_risk, 0.05);
    EXPECT_EQ(risk_config.max_single_trade_risk, 0.01);
    
    auto db_config = config_manager->get_database_config();
    EXPECT_EQ(db_config.redis_host, "localhost");
    EXPECT_EQ(db_config.redis_port, 6379);
}

TEST_F(ConfigManagerTest, ConfigurationSaveLoad) {
    // Create test configuration
    nlohmann::json test_config = {
        {"trading", {
            {"enabled", true},
            {"min_spread_threshold", 0.01},
            {"max_position_size", 2000.0}
        }},
        {"database", {
            {"redis_host", "test_host"},
            {"redis_port", 6380}
        }},
        {"exchanges", nlohmann::json::array({
            {
                {"id", "binance"},
                {"name", "Binance"},
                {"api_key", "test_key"},
                {"secret_key", "test_secret"},
                {"sandbox_mode", true}
            }
        })}
    };
    
    // Save test config
    std::ofstream file(test_config_file);
    file << test_config.dump(4);
    file.close();
    
    // Load config
    EXPECT_TRUE(config_manager->load_config(test_config_file));
    
    // Verify loaded configuration
    auto trading_config = config_manager->get_trading_config();
    EXPECT_TRUE(trading_config.enabled);
    EXPECT_EQ(trading_config.min_spread_threshold, 0.01);
    EXPECT_EQ(trading_config.max_position_size, 2000.0);
    
    auto db_config = config_manager->get_database_config();
    EXPECT_EQ(db_config.redis_host, "test_host");
    EXPECT_EQ(db_config.redis_port, 6380);
    
    auto exchanges = config_manager->get_exchange_configs();
    EXPECT_EQ(exchanges.size(), 1);
    EXPECT_EQ(exchanges[0].id, "binance");
    EXPECT_EQ(exchanges[0].name, "Binance");
    EXPECT_TRUE(exchanges[0].sandbox_mode);
}

TEST_F(ConfigManagerTest, ConfigurationValidation) {
    // Test with invalid configuration
    nlohmann::json invalid_config = {
        {"trading", {
            {"enabled", true},
            {"min_spread_threshold", -0.01}, // Invalid: negative
            {"max_position_size", 0}         // Invalid: zero
        }},
        {"risk", {
            {"max_portfolio_risk", 1.5}      // Invalid: > 1.0
        }},
        {"exchanges", nlohmann::json::array({
            {
                {"id", ""},                  // Invalid: empty
                {"api_key", ""},            // Invalid: empty
                {"secret_key", "test_secret"}
            }
        })}
    };
    
    std::ofstream file(test_config_file);
    file << invalid_config.dump(4);
    file.close();
    
    EXPECT_FALSE(config_manager->load_config(test_config_file));
    
    auto errors = config_manager->get_validation_errors();
    EXPECT_GT(errors.size(), 0);
}

TEST_F(ConfigManagerTest, GenericValueAccess) {
    config_manager->set_value("test.string_value", std::string("hello"));
    config_manager->set_value("test.int_value", 42);
    config_manager->set_value("test.double_value", 3.14);
    config_manager->set_value("test.bool_value", true);
    
    EXPECT_EQ(config_manager->get_value<std::string>("test.string_value"), "hello");
    EXPECT_EQ(config_manager->get_value<int>("test.int_value"), 42);
    EXPECT_EQ(config_manager->get_value<double>("test.double_value"), 3.14);
    EXPECT_EQ(config_manager->get_value<bool>("test.bool_value"), true);
    
    // Test default values
    EXPECT_EQ(config_manager->get_value<std::string>("nonexistent.key", "default"), "default");
    EXPECT_EQ(config_manager->get_value<int>("nonexistent.key", 999), 999);
}

// Common Types Tests
TEST(CommonTypesTest, TickerCreation) {
    auto timestamp = std::chrono::system_clock::now();
    Ticker ticker("BTC/USDT", "binance", 49900.0, 50000.0, 49950.0, 1000.0, timestamp);
    
    EXPECT_EQ(ticker.symbol, "BTC/USDT");
    EXPECT_EQ(ticker.exchange, "binance");
    EXPECT_EQ(ticker.bid, 49900.0);
    EXPECT_EQ(ticker.ask, 50000.0);
    EXPECT_EQ(ticker.last, 49950.0);
    EXPECT_EQ(ticker.volume_24h, 1000.0);
    EXPECT_EQ(ticker.timestamp, timestamp);
}

TEST(CommonTypesTest, OrderCreation) {
    Order order("order123", "binance", "BTC/USDT", OrderType::LIMIT, OrderSide::BUY, 0.1, 50000.0);
    
    EXPECT_EQ(order.id, "order123");
    EXPECT_EQ(order.exchange, "binance");
    EXPECT_EQ(order.symbol, "BTC/USDT");
    EXPECT_EQ(order.type, OrderType::LIMIT);
    EXPECT_EQ(order.side, OrderSide::BUY);
    EXPECT_EQ(order.quantity, 0.1);
    EXPECT_EQ(order.price, 50000.0);
    EXPECT_EQ(order.status, OrderStatus::PENDING);
    EXPECT_EQ(order.filled_quantity, 0.0);
}

TEST(CommonTypesTest, ArbitrageOpportunity) {
    ArbitrageOpportunity opportunity("BTC/USDT", "binance", "upbit", 50000.0, 50500.0, 1.0, 1.0, 500.0);
    
    EXPECT_EQ(opportunity.symbol, "BTC/USDT");
    EXPECT_EQ(opportunity.buy_exchange, "binance");
    EXPECT_EQ(opportunity.sell_exchange, "upbit");
    EXPECT_EQ(opportunity.buy_price, 50000.0);
    EXPECT_EQ(opportunity.sell_price, 50500.0);
    EXPECT_EQ(opportunity.spread_percentage, 1.0);
    EXPECT_EQ(opportunity.potential_profit, 500.0);
    EXPECT_EQ(opportunity.validity_duration, std::chrono::milliseconds(5000));
}

TEST(CommonTypesTest, Portfolio) {
    Portfolio portfolio;
    
    // Add some balances
    Balance btc_balance("BTC", "binance", 1.0, 0.9, 0.1);
    Balance usdt_balance("USDT", "binance", 50000.0, 49000.0, 1000.0);
    
    portfolio.balances["BTC"] = btc_balance;
    portfolio.balances["USDT"] = usdt_balance;
    
    EXPECT_EQ(portfolio.balances.size(), 2);
    EXPECT_EQ(portfolio.balances["BTC"].total, 1.0);
    EXPECT_EQ(portfolio.balances["USDT"].available, 49000.0);
}

// Main test runner
int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}