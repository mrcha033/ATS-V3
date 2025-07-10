#include "redis_subscriber.hpp"
#include "utils/logger.hpp"
#include "utils/json_parser.hpp"
#include <nlohmann/json.hpp>
#include <hiredis/hiredis.h>
#include <thread>
#include <chrono>
#include <sstream>
#include <queue>
#include <mutex>
#include <condition_variable>

namespace ats {
namespace trading_engine {

struct RedisSubscriber::Implementation {
    RedisSubscriberConfig config;
    redisContext* redis_context = nullptr;
    std::atomic<bool> running{false};
    std::atomic<bool> connected{false};
    std::atomic<bool> initialized{false};
    
    // Threading
    std::thread connection_thread;
    std::thread message_processing_thread;
    std::thread health_check_thread;
    std::vector<std::thread> worker_threads;
    
    // Message queue
    std::queue<RedisMessage> message_queue;
    std::mutex message_queue_mutex;
    std::condition_variable message_queue_cv;
    
    // Subscriptions
    std::vector<std::string> subscribed_channels;
    std::vector<std::string> subscribed_patterns;
    mutable std::shared_mutex subscriptions_mutex;
    
    // Callbacks
    MessageCallback message_callback;
    PriceUpdateCallback price_update_callback;
    ConnectionCallback connection_callback;
    ErrorCallback error_callback;
    
    // Statistics
    mutable SubscriberStatistics statistics;
    std::mutex stats_mutex;
    
    // Connection state
    std::atomic<std::chrono::system_clock::time_point> last_ping_time;
    std::atomic<std::chrono::system_clock::time_point> last_message_time;
    std::atomic<int> reconnect_attempts{0};
    
    Implementation() {
        last_ping_time = std::chrono::system_clock::now();
        last_message_time = std::chrono::system_clock::now();
    }
};

RedisSubscriber::RedisSubscriber() : impl_(std::make_unique<Implementation>()) {}

RedisSubscriber::~RedisSubscriber() {
    if (impl_->running) {
        stop();
    }
    
    if (impl_->redis_context) {
        redisFree(impl_->redis_context);
    }
}

bool RedisSubscriber::initialize(const RedisSubscriberConfig& config) {
    if (impl_->initialized) {
        utils::Logger::warn("RedisSubscriber already initialized");
        return true;
    }
    
    impl_->config = config;
    impl_->statistics.start_time = std::chrono::system_clock::now();
    
    // Initialize subscriptions from config
    {
        std::unique_lock<std::shared_mutex> lock(impl_->subscriptions_mutex);
        impl_->subscribed_channels = config.channels;
        
        if (!config.channel_pattern.empty()) {
            impl_->subscribed_patterns.push_back(config.channel_pattern);
        }
    }
    
    impl_->initialized = true;
    utils::Logger::info("RedisSubscriber initialized successfully");
    return true;
}

bool RedisSubscriber::start() {
    if (!impl_->initialized) {
        utils::Logger::error("RedisSubscriber not initialized");
        return false;
    }
    
    if (impl_->running) {
        utils::Logger::warn("RedisSubscriber already running");
        return true;
    }
    
    impl_->running = true;
    impl_->reconnect_attempts = 0;
    
    // Start connection thread
    impl_->connection_thread = std::thread([this]() {
        while (impl_->running) {
            if (!impl_->connected) {
                if (connect_to_redis()) {
                    impl_->connected = true;
                    impl_->statistics.is_connected = true;
                    impl_->reconnect_attempts = 0;
                    
                    if (impl_->connection_callback) {
                        impl_->connection_callback(true, "Connected to Redis");
                    }
                    
                    // Subscribe to channels
                    {
                        std::shared_lock<std::shared_mutex> lock(impl_->subscriptions_mutex);
                        for (const auto& channel : impl_->subscribed_channels) {
                            redisReply* reply = (redisReply*)redisCommand(impl_->redis_context, "SUBSCRIBE %s", channel.c_str());
                            if (reply) {
                                freeReplyObject(reply);
                            }
                        }
                        
                        for (const auto& pattern : impl_->subscribed_patterns) {
                            redisReply* reply = (redisReply*)redisCommand(impl_->redis_context, "PSUBSCRIBE %s", pattern.c_str());
                            if (reply) {
                                freeReplyObject(reply);
                            }
                        }
                    }
                    
                    utils::Logger::info("Redis subscriber connected and subscribed to channels");
                } else {
                    impl_->reconnect_attempts++;
                    impl_->statistics.total_connection_errors++;
                    
                    if (impl_->error_callback) {
                        impl_->error_callback("Failed to connect to Redis, attempt " + std::to_string(impl_->reconnect_attempts));
                    }
                    
                    std::this_thread::sleep_for(impl_->config.reconnect_delay);
                }
            } else {
                // Listen for messages
                redisReply* reply = nullptr;
                int result = redisGetReply(impl_->redis_context, (void**)&reply);
                
                if (result == REDIS_OK && reply) {
                    if (reply->type == REDIS_REPLY_ARRAY && reply->elements >= 3) {
                        std::string message_type = reply->element[0]->str;
                        std::string channel = reply->element[1]->str;
                        std::string message = reply->element[2]->str;
                        
                        if (message_type == "message" || message_type == "pmessage") {
                            RedisMessage redis_msg(channel, message);
                            
                            // Add to processing queue
                            {
                                std::lock_guard<std::mutex> lock(impl_->message_queue_mutex);
                                impl_->message_queue.push(redis_msg);
                            }
                            impl_->message_queue_cv.notify_one();
                            
                            impl_->statistics.total_messages_received++;
                            impl_->last_message_time = std::chrono::system_clock::now();
                        }
                    }
                    freeReplyObject(reply);
                } else {
                    // Connection lost
                    handle_connection_lost();
                }
            }
        }
    });
    
    // Start message processing thread
    impl_->message_processing_thread = std::thread([this]() {
        message_processing_loop();
    });
    
    // Start health check thread
    if (impl_->config.enable_health_check) {
        impl_->health_check_thread = std::thread([this]() {
            health_check_loop();
        });
    }
    
    utils::Logger::info("RedisSubscriber started successfully");
    return true;
}

void RedisSubscriber::stop() {
    if (!impl_->running) {
        return;
    }
    
    utils::Logger::info("Stopping RedisSubscriber...");
    
    impl_->running = false;
    impl_->message_queue_cv.notify_all();
    
    // Join threads
    if (impl_->connection_thread.joinable()) {
        impl_->connection_thread.join();
    }
    
    if (impl_->message_processing_thread.joinable()) {
        impl_->message_processing_thread.join();
    }
    
    if (impl_->health_check_thread.joinable()) {
        impl_->health_check_thread.join();
    }
    
    // Disconnect from Redis
    disconnect_from_redis();
    
    utils::Logger::info("RedisSubscriber stopped");
}

bool RedisSubscriber::is_running() const {
    return impl_->running;
}

bool RedisSubscriber::subscribe_to_channel(const std::string& channel) {
    std::unique_lock<std::shared_mutex> lock(impl_->subscriptions_mutex);
    
    if (std::find(impl_->subscribed_channels.begin(), impl_->subscribed_channels.end(), channel) 
        != impl_->subscribed_channels.end()) {
        return true; // Already subscribed
    }
    
    impl_->subscribed_channels.push_back(channel);
    
    if (impl_->connected && impl_->redis_context) {
        redisReply* reply = (redisReply*)redisCommand(impl_->redis_context, "SUBSCRIBE %s", channel.c_str());
        if (reply) {
            freeReplyObject(reply);
            utils::Logger::info("Subscribed to channel: {}", channel);
            return true;
        }
    }
    
    return false;
}

bool RedisSubscriber::unsubscribe_from_channel(const std::string& channel) {
    std::unique_lock<std::shared_mutex> lock(impl_->subscriptions_mutex);
    
    auto it = std::find(impl_->subscribed_channels.begin(), impl_->subscribed_channels.end(), channel);
    if (it != impl_->subscribed_channels.end()) {
        impl_->subscribed_channels.erase(it);
        
        if (impl_->connected && impl_->redis_context) {
            redisReply* reply = (redisReply*)redisCommand(impl_->redis_context, "UNSUBSCRIBE %s", channel.c_str());
            if (reply) {
                freeReplyObject(reply);
                utils::Logger::info("Unsubscribed from channel: {}", channel);
                return true;
            }
        }
    }
    
    return false;
}

std::vector<std::string> RedisSubscriber::get_subscribed_channels() const {
    std::shared_lock<std::shared_mutex> lock(impl_->subscriptions_mutex);
    return impl_->subscribed_channels;
}

void RedisSubscriber::set_message_callback(MessageCallback callback) {
    impl_->message_callback = callback;
}

void RedisSubscriber::set_price_update_callback(PriceUpdateCallback callback) {
    impl_->price_update_callback = callback;
}

void RedisSubscriber::set_connection_callback(ConnectionCallback callback) {
    impl_->connection_callback = callback;
}

void RedisSubscriber::set_error_callback(ErrorCallback callback) {
    impl_->error_callback = callback;
}

bool RedisSubscriber::is_connected() const {
    return impl_->connected;
}

SubscriberStatistics RedisSubscriber::get_statistics() const {
    std::lock_guard<std::mutex> lock(impl_->stats_mutex);
    
    // Update uptime
    auto now = std::chrono::system_clock::now();
    auto uptime = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - impl_->statistics.start_time);
    const_cast<SubscriberStatistics&>(impl_->statistics).uptime = uptime;
    
    return impl_->statistics;
}

bool RedisSubscriber::is_healthy() const {
    if (!impl_->running || !impl_->connected) {
        return false;
    }
    
    // Check if we've received messages recently
    auto now = std::chrono::system_clock::now();
    auto time_since_last_message = std::chrono::duration_cast<std::chrono::seconds>(
        now - impl_->last_message_time);
    
    if (time_since_last_message > std::chrono::seconds(60)) {
        return false;
    }
    
    return true;
}

bool RedisSubscriber::ping_server() {
    if (!impl_->connected || !impl_->redis_context) {
        return false;
    }
    
    redisReply* reply = (redisReply*)redisCommand(impl_->redis_context, "PING");
    if (reply) {
        bool success = (reply->type == REDIS_REPLY_STATUS && 
                       std::string(reply->str) == "PONG");
        freeReplyObject(reply);
        
        if (success) {
            impl_->last_ping_time = std::chrono::system_clock::now();
        }
        
        return success;
    }
    
    return false;
}

bool RedisSubscriber::connect_to_redis() {
    if (impl_->redis_context) {
        redisFree(impl_->redis_context);
        impl_->redis_context = nullptr;
    }
    
    struct timeval timeout = {
        static_cast<time_t>(impl_->config.connection_timeout.count()),
        0
    };
    
    impl_->redis_context = redisConnectWithTimeout(impl_->config.host.c_str(), 
                                                   impl_->config.port, 
                                                   timeout);
    
    if (!impl_->redis_context || impl_->redis_context->err) {
        if (impl_->redis_context) {
            utils::Logger::error("Redis connection error: {}", impl_->redis_context->errstr);
            redisFree(impl_->redis_context);
            impl_->redis_context = nullptr;
        }
        return false;
    }
    
    // Authenticate if password is provided
    if (!impl_->config.password.empty()) {
        redisReply* reply = (redisReply*)redisCommand(impl_->redis_context, "AUTH %s", impl_->config.password.c_str());
        if (!reply || reply->type == REDIS_REPLY_ERROR) {
            utils::Logger::error("Redis authentication failed");
            if (reply) freeReplyObject(reply);
            return false;
        }
        freeReplyObject(reply);
    }
    
    // Set command timeout
    timeout.tv_sec = impl_->config.command_timeout.count();
    redisSetTimeout(impl_->redis_context, timeout);
    
    utils::Logger::info("Connected to Redis at {}:{}", impl_->config.host, impl_->config.port);
    return true;
}

void RedisSubscriber::disconnect_from_redis() {
    if (impl_->redis_context) {
        redisFree(impl_->redis_context);
        impl_->redis_context = nullptr;
    }
    
    impl_->connected = false;
    impl_->statistics.is_connected = false;
    
    if (impl_->connection_callback) {
        impl_->connection_callback(false, "Disconnected from Redis");
    }
}

void RedisSubscriber::handle_connection_lost() {
    utils::Logger::warn("Redis connection lost, attempting reconnection...");
    
    impl_->connected = false;
    impl_->statistics.is_connected = false;
    impl_->statistics.total_reconnections++;
    
    if (impl_->connection_callback) {
        impl_->connection_callback(false, "Connection lost");
    }
    
    disconnect_from_redis();
}

void RedisSubscriber::message_processing_loop() {
    utils::Logger::debug("Redis message processing loop started");
    
    while (impl_->running) {
        std::unique_lock<std::mutex> lock(impl_->message_queue_mutex);
        
        impl_->message_queue_cv.wait(lock, [this]() {
            return !impl_->message_queue.empty() || !impl_->running;
        });
        
        if (!impl_->running) break;
        
        while (!impl_->message_queue.empty()) {
            RedisMessage message = impl_->message_queue.front();
            impl_->message_queue.pop();
            lock.unlock();
            
            process_raw_message(message);
            
            lock.lock();
        }
    }
    
    utils::Logger::debug("Redis message processing loop stopped");
}

void RedisSubscriber::process_raw_message(const RedisMessage& message) {
    auto start_time = std::chrono::high_resolution_clock::now();
    
    try {
        // Call general message callback
        if (impl_->message_callback) {
            impl_->message_callback(message);
        }
        
        // Process price update messages
        if (is_price_update_message(message)) {
            process_price_update_message(message);
        }
        
        impl_->statistics.total_messages_processed++;
        
        // Record processing time
        auto end_time = std::chrono::high_resolution_clock::now();
        auto processing_time = std::chrono::duration_cast<std::chrono::microseconds>(
            end_time - start_time);
        record_message_processed(processing_time);
        
    } catch (const std::exception& e) {
        handle_parsing_error(message, e.what());
    }
}

void RedisSubscriber::process_price_update_message(const RedisMessage& message) {
    try {
        PriceUpdateEvent event = parse_price_message(message);
        
        if (impl_->price_update_callback) {
            impl_->price_update_callback(event);
        }
        
        impl_->statistics.total_price_updates++;
        
    } catch (const std::exception& e) {
        handle_parsing_error(message, "Price update parsing error: " + std::string(e.what()));
    }
}

PriceUpdateEvent RedisSubscriber::parse_price_message(const RedisMessage& message) {
    PriceUpdateEvent event;
    event.source_channel = message.channel;
    event.received_at = message.timestamp;
    event.event_type = "price_update";
    
    try {
        // Parse JSON message
        nlohmann::json j = nlohmann::json::parse(message.message);
        
        // Extract ticker information
        event.ticker.symbol = j.value("symbol", "");
        event.ticker.exchange = j.value("exchange", "");
        event.ticker.bid = j.value("bid", 0.0);
        event.ticker.ask = j.value("ask", 0.0);
        event.ticker.last = j.value("last", 0.0);
        event.ticker.volume = j.value("volume", 0.0);
        event.ticker.volume_quote = j.value("volume_quote", 0.0);
        event.ticker.high = j.value("high", 0.0);
        event.ticker.low = j.value("low", 0.0);
        event.ticker.open = j.value("open", 0.0);
        event.ticker.close = j.value("close", 0.0);
        event.ticker.change = j.value("change", 0.0);
        event.ticker.change_percent = j.value("change_percent", 0.0);
        
        // Set timestamp
        if (j.contains("timestamp")) {
            int64_t timestamp_ms = j["timestamp"];
            event.ticker.timestamp = std::chrono::system_clock::time_point(
                std::chrono::milliseconds(timestamp_ms));
        } else {
            event.ticker.timestamp = std::chrono::system_clock::now();
        }
        
    } catch (const std::exception& e) {
        utils::Logger::error("Failed to parse price message: {}", e.what());
        throw;
    }
    
    return event;
}

bool RedisSubscriber::is_price_update_message(const RedisMessage& message) {
    // Check if the channel name indicates a price update
    return message.channel.find("price") != std::string::npos ||
           message.channel.find("ticker") != std::string::npos ||
           message.channel.find("market") != std::string::npos;
}

void RedisSubscriber::handle_parsing_error(const RedisMessage& message, const std::string& error) {
    utils::Logger::error("Message parsing error in channel {}: {}", message.channel, error);
    
    impl_->statistics.total_parsing_errors++;
    
    if (impl_->error_callback) {
        impl_->error_callback("Parsing error: " + error);
    }
}

void RedisSubscriber::record_message_processed(std::chrono::microseconds processing_time) {
    std::lock_guard<std::mutex> lock(impl_->stats_mutex);
    
    auto current_latency = impl_->statistics.average_processing_latency.load();
    auto new_latency = std::chrono::duration_cast<std::chrono::milliseconds>(
        (current_latency + processing_time) / 2);
    impl_->statistics.average_processing_latency = new_latency;
}

void RedisSubscriber::health_check_loop() {
    utils::Logger::debug("Redis health check loop started");
    
    while (impl_->running) {
        std::this_thread::sleep_for(impl_->config.health_check_interval);
        
        if (impl_->running) {
            check_connection_health();
            check_message_flow_health();
        }
    }
    
    utils::Logger::debug("Redis health check loop stopped");
}

void RedisSubscriber::check_connection_health() {
    if (impl_->connected && !ping_server()) {
        utils::Logger::warn("Redis ping failed, connection may be unhealthy");
        handle_connection_lost();
    }
}

void RedisSubscriber::check_message_flow_health() {
    auto now = std::chrono::system_clock::now();
    auto time_since_last_message = std::chrono::duration_cast<std::chrono::seconds>(
        now - impl_->last_message_time);
    
    if (time_since_last_message > std::chrono::seconds(120)) {
        utils::Logger::warn("No messages received in {} seconds", time_since_last_message.count());
        
        if (impl_->error_callback) {
            impl_->error_callback("No messages received for extended period");
        }
    }
}

// TradeLogger implementation
struct TradeLogger::Implementation {
    std::string influxdb_url;
    std::string database;
    std::string log_directory;
    
    std::atomic<bool> influxdb_enabled{false};
    std::atomic<bool> file_logging_enabled{false};
    std::atomic<bool> healthy{false};
    
    std::queue<std::string> pending_logs;
    std::mutex pending_logs_mutex;
    std::thread flush_thread;
    std::atomic<bool> running{false};
    
    std::chrono::seconds flush_interval{30};
    size_t batch_size{100};
    std::atomic<size_t> total_logs_written{0};
    
    std::ofstream current_log_file;
    std::mutex file_mutex;
};

TradeLogger::TradeLogger() : impl_(std::make_unique<Implementation>()) {}

TradeLogger::~TradeLogger() {
    if (impl_->running) {
        flush_pending_logs();
    }
    
    if (impl_->current_log_file.is_open()) {
        impl_->current_log_file.close();
    }
}

bool TradeLogger::initialize(const std::string& influxdb_url, const std::string& database) {
    impl_->influxdb_url = influxdb_url;
    impl_->database = database;
    impl_->influxdb_enabled = true;
    impl_->running = true;
    impl_->healthy = true;
    
    // Start flush thread
    impl_->flush_thread = std::thread([this]() {
        while (impl_->running) {
            std::this_thread::sleep_for(impl_->flush_interval);
            if (impl_->running) {
                process_pending_logs();
            }
        }
    });
    
    utils::Logger::info("TradeLogger initialized with InfluxDB at {}", influxdb_url);
    return true;
}

bool TradeLogger::initialize_file_logging(const std::string& log_directory) {
    impl_->log_directory = log_directory;
    impl_->file_logging_enabled = true;
    
    // Create directory if it doesn't exist
    std::filesystem::create_directories(log_directory);
    
    utils::Logger::info("TradeLogger file logging enabled at {}", log_directory);
    return true;
}

bool TradeLogger::log_trade_execution(const TradeExecution& execution) {
    std::string log_entry;
    
    if (impl_->influxdb_enabled) {
        log_entry = trade_execution_to_line_protocol(execution);
        
        std::lock_guard<std::mutex> lock(impl_->pending_logs_mutex);
        impl_->pending_logs.push(log_entry);
    }
    
    if (impl_->file_logging_enabled) {
        std::string csv_entry = trade_execution_to_csv(execution);
        write_to_file(csv_entry);
    }
    
    return true;
}

bool TradeLogger::log_arbitrage_opportunity(const ArbitrageOpportunity& opportunity) {
    if (impl_->influxdb_enabled) {
        std::string log_entry = arbitrage_opportunity_to_line_protocol(opportunity);
        
        std::lock_guard<std::mutex> lock(impl_->pending_logs_mutex);
        impl_->pending_logs.push(log_entry);
    }
    
    return true;
}

bool TradeLogger::flush_pending_logs() {
    process_pending_logs();
    flush_file_buffers();
    return true;
}

size_t TradeLogger::get_pending_log_count() const {
    std::lock_guard<std::mutex> lock(impl_->pending_logs_mutex);
    return impl_->pending_logs.size();
}

bool TradeLogger::is_healthy() const {
    return impl_->healthy;
}

std::string TradeLogger::trade_execution_to_line_protocol(const TradeExecution& execution) {
    std::ostringstream oss;
    oss << "trade_execution,symbol=" << execution.symbol 
        << ",buy_exchange=" << execution.buy_exchange
        << ",sell_exchange=" << execution.sell_exchange
        << ",result=" << static_cast<int>(execution.result)
        << " quantity=" << execution.quantity
        << ",executed_quantity=" << execution.executed_quantity
        << ",buy_price=" << execution.buy_price
        << ",sell_price=" << execution.sell_price
        << ",expected_profit=" << execution.expected_profit
        << ",actual_profit=" << execution.actual_profit
        << ",total_fees=" << execution.total_fees
        << ",execution_latency=" << execution.execution_latency.count()
        << " " << std::chrono::duration_cast<std::chrono::nanoseconds>(
               execution.timestamp.time_since_epoch()).count();
    
    return oss.str();
}

std::string TradeLogger::trade_execution_to_csv(const TradeExecution& execution) {
    std::ostringstream oss;
    oss << execution.trade_id << ","
        << execution.symbol << ","
        << execution.buy_exchange << ","
        << execution.sell_exchange << ","
        << execution.quantity << ","
        << execution.executed_quantity << ","
        << execution.buy_price << ","
        << execution.sell_price << ","
        << execution.expected_profit << ","
        << execution.actual_profit << ","
        << execution.total_fees << ","
        << execution.execution_latency.count() << ","
        << static_cast<int>(execution.result) << ","
        << format_timestamp(execution.timestamp) << std::endl;
    
    return oss.str();
}

std::string TradeLogger::arbitrage_opportunity_to_line_protocol(const ArbitrageOpportunity& opportunity) {
    std::ostringstream oss;
    oss << "arbitrage_opportunity,symbol=" << opportunity.symbol 
        << ",buy_exchange=" << opportunity.buy_exchange
        << ",sell_exchange=" << opportunity.sell_exchange
        << " buy_price=" << opportunity.buy_price
        << ",sell_price=" << opportunity.sell_price
        << ",available_quantity=" << opportunity.available_quantity
        << ",spread_percentage=" << opportunity.spread_percentage
        << ",expected_profit=" << opportunity.expected_profit
        << ",confidence_score=" << opportunity.confidence_score
        << " " << std::chrono::duration_cast<std::chrono::nanoseconds>(
               opportunity.detected_at.time_since_epoch()).count();
    
    return oss.str();
}

bool TradeLogger::write_to_file(const std::string& log_entry) {
    std::lock_guard<std::mutex> lock(impl_->file_mutex);
    
    if (!impl_->current_log_file.is_open()) {
        std::string filename = create_log_filename("trades");
        impl_->current_log_file.open(filename, std::ios::app);
        
        if (!impl_->current_log_file.is_open()) {
            utils::Logger::error("Failed to open log file: {}", filename);
            return false;
        }
    }
    
    impl_->current_log_file << log_entry;
    impl_->total_logs_written++;
    
    return true;
}

std::string TradeLogger::create_log_filename(const std::string& prefix) {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    
    std::ostringstream oss;
    oss << impl_->log_directory << "/" << prefix << "_" 
        << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S") << ".csv";
    
    return oss.str();
}

void TradeLogger::process_pending_logs() {
    std::vector<std::string> batch;
    
    {
        std::lock_guard<std::mutex> lock(impl_->pending_logs_mutex);
        
        while (!impl_->pending_logs.empty() && batch.size() < impl_->batch_size) {
            batch.push_back(impl_->pending_logs.front());
            impl_->pending_logs.pop();
        }
    }
    
    // Process batch (in real implementation, would send to InfluxDB)
    for (const auto& log_entry : batch) {
        // Simulate InfluxDB write
        utils::Logger::debug("Writing to InfluxDB: {}", log_entry);
        impl_->total_logs_written++;
    }
}

void TradeLogger::flush_file_buffers() {
    std::lock_guard<std::mutex> lock(impl_->file_mutex);
    if (impl_->current_log_file.is_open()) {
        impl_->current_log_file.flush();
    }
}

std::string TradeLogger::format_timestamp(std::chrono::system_clock::time_point timestamp) {
    auto time_t = std::chrono::system_clock::to_time_t(timestamp);
    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&time_t), "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

// Utility functions implementation
namespace redis_utils {

std::string build_price_channel(const std::string& exchange, const std::string& symbol) {
    return "price:" + exchange + ":" + symbol;
}

std::string build_ticker_channel(const std::string& exchange) {
    return "ticker:" + exchange;
}

std::string build_arbitrage_channel() {
    return "arbitrage:opportunities";
}

std::string format_ticker_message(const types::Ticker& ticker) {
    nlohmann::json j;
    j["symbol"] = ticker.symbol;
    j["exchange"] = ticker.exchange;
    j["bid"] = ticker.bid;
    j["ask"] = ticker.ask;
    j["last"] = ticker.last;
    j["volume"] = ticker.volume;
    j["volume_quote"] = ticker.volume_quote;
    j["high"] = ticker.high;
    j["low"] = ticker.low;
    j["open"] = ticker.open;
    j["close"] = ticker.close;
    j["change"] = ticker.change;
    j["change_percent"] = ticker.change_percent;
    j["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
        ticker.timestamp.time_since_epoch()).count();
    
    return j.dump();
}

types::Ticker parse_ticker_message(const std::string& message) {
    types::Ticker ticker;
    
    try {
        nlohmann::json j = nlohmann::json::parse(message);
        
        ticker.symbol = j.value("symbol", "");
        ticker.exchange = j.value("exchange", "");
        ticker.bid = j.value("bid", 0.0);
        ticker.ask = j.value("ask", 0.0);
        ticker.last = j.value("last", 0.0);
        ticker.volume = j.value("volume", 0.0);
        ticker.volume_quote = j.value("volume_quote", 0.0);
        ticker.high = j.value("high", 0.0);
        ticker.low = j.value("low", 0.0);
        ticker.open = j.value("open", 0.0);
        ticker.close = j.value("close", 0.0);
        ticker.change = j.value("change", 0.0);
        ticker.change_percent = j.value("change_percent", 0.0);
        
        if (j.contains("timestamp")) {
            int64_t timestamp_ms = j["timestamp"];
            ticker.timestamp = std::chrono::system_clock::time_point(
                std::chrono::milliseconds(timestamp_ms));
        } else {
            ticker.timestamp = std::chrono::system_clock::now();
        }
        
    } catch (const std::exception& e) {
        utils::Logger::error("Failed to parse ticker message: {}", e.what());
        throw;
    }
    
    return ticker;
}

bool is_valid_redis_message(const RedisMessage& message) {
    return !message.channel.empty() && !message.message.empty();
}

bool is_json_message(const std::string& message) {
    try {
        nlohmann::json::parse(message);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

} // namespace redis_utils

} // namespace trading_engine
} // namespace ats