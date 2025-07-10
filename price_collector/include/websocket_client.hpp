#pragma once

#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/steady_timer.hpp>
#include <memory>
#include <string>
#include <functional>
#include <chrono>
#include <atomic>
#include <queue>
#include <mutex>
#include <thread>

namespace ats {
namespace price_collector {

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = boost::asio::ip::tcp;

// WebSocket message types
enum class MessageType {
    TEXT,
    BINARY
};

// WebSocket message structure
struct WebSocketMessage {
    std::string data;
    MessageType type;
    std::chrono::system_clock::time_point timestamp;
    
    WebSocketMessage(const std::string& d, MessageType t = MessageType::TEXT)
        : data(d), type(t), timestamp(std::chrono::system_clock::now()) {}
};

// WebSocket connection status
enum class WebSocketStatus {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    RECONNECTING,
    CLOSING,
    ERROR
};

// Callback types
using MessageCallback = std::function<void(const WebSocketMessage&)>;
using ConnectionCallback = std::function<void(WebSocketStatus, const std::string& reason)>;
using ErrorCallback = std::function<void(const std::string& error)>;

// WebSocket client configuration
struct WebSocketConfig {
    std::string host;
    std::string port;
    std::string target;
    bool use_ssl;
    std::chrono::seconds ping_interval;
    std::chrono::seconds pong_timeout;
    std::chrono::seconds reconnect_delay;
    int max_reconnect_attempts;
    size_t max_message_size;
    std::unordered_map<std::string, std::string> headers;
    
    WebSocketConfig() 
        : port("443"), target("/"), use_ssl(true)
        , ping_interval(30), pong_timeout(10), reconnect_delay(5)
        , max_reconnect_attempts(10), max_message_size(1024 * 1024) {}
};

// Asynchronous WebSocket client using Boost.Beast
class WebSocketClient : public std::enable_shared_from_this<WebSocketClient> {
public:
    WebSocketClient(net::io_context& ioc, ssl::context& ssl_ctx);
    ~WebSocketClient();
    
    // Configuration
    void configure(const WebSocketConfig& config);
    void set_message_callback(MessageCallback callback);
    void set_connection_callback(ConnectionCallback callback);
    void set_error_callback(ErrorCallback callback);
    
    // Connection management
    void connect();
    void disconnect();
    bool is_connected() const;
    WebSocketStatus get_status() const;
    
    // Message sending
    bool send_text(const std::string& message);
    bool send_binary(const std::vector<uint8_t>& data);
    bool send_ping();
    
    // Queue management
    size_t get_send_queue_size() const;
    size_t get_receive_queue_size() const;
    void clear_queues();
    
    // Statistics
    size_t get_messages_sent() const;
    size_t get_messages_received() const;
    size_t get_bytes_sent() const;
    size_t get_bytes_received() const;
    std::chrono::milliseconds get_last_message_time() const;
    std::chrono::milliseconds get_connection_uptime() const;
    int get_reconnect_count() const;
    
    // Auto-reconnection
    void enable_auto_reconnect(bool enable = true);
    bool is_auto_reconnect_enabled() const;
    
private:
    struct Implementation;
    std::unique_ptr<Implementation> impl_;
    
    // Connection handlers
    void on_resolve(beast::error_code ec, tcp::resolver::results_type results);
    void on_connect(beast::error_code ec, tcp::resolver::results_type::endpoint_type);
    void on_ssl_handshake(beast::error_code ec);
    void on_handshake(beast::error_code ec);
    void on_write(beast::error_code ec, std::size_t bytes_transferred);
    void on_read(beast::error_code ec, std::size_t bytes_transferred);
    void on_ping(beast::error_code ec);
    void on_pong(beast::error_code ec);
    void on_close(beast::error_code ec);
    
    // Internal methods
    void start_ping_timer();
    void start_pong_timer();
    void start_reconnect_timer();
    void process_send_queue();
    void handle_error(const std::string& operation, beast::error_code ec);
    void change_status(WebSocketStatus new_status, const std::string& reason = "");
    void schedule_reconnect();
    void reset_connection();
};

// WebSocket client manager for handling multiple connections
class WebSocketManager {
public:
    WebSocketManager(net::io_context& ioc, ssl::context& ssl_ctx);
    ~WebSocketManager();
    
    // Client management
    std::shared_ptr<WebSocketClient> create_client(const std::string& client_id);
    std::shared_ptr<WebSocketClient> get_client(const std::string& client_id);
    void remove_client(const std::string& client_id);
    void remove_all_clients();
    
    // Bulk operations
    void connect_all();
    void disconnect_all();
    void send_to_all(const std::string& message);
    
    // Statistics
    size_t get_client_count() const;
    size_t get_connected_count() const;
    std::vector<std::string> get_client_ids() const;
    
    // Event handling
    void set_global_message_callback(MessageCallback callback);
    void set_global_connection_callback(ConnectionCallback callback);
    void set_global_error_callback(ErrorCallback callback);
    
private:
    net::io_context& ioc_;
    ssl::context& ssl_ctx_;
    
    std::unordered_map<std::string, std::shared_ptr<WebSocketClient>> clients_;
    std::mutex clients_mutex_;
    
    MessageCallback global_message_callback_;
    ConnectionCallback global_connection_callback_;
    ErrorCallback global_error_callback_;
    
    void on_client_message(const std::string& client_id, const WebSocketMessage& message);
    void on_client_connection(const std::string& client_id, WebSocketStatus status, const std::string& reason);
    void on_client_error(const std::string& client_id, const std::string& error);
};

// Utility functions for WebSocket operations
namespace websocket_utils {
    
    // Message formatting
    std::string create_subscribe_message(const std::string& channel, const std::vector<std::string>& symbols);
    std::string create_unsubscribe_message(const std::string& channel, const std::vector<std::string>& symbols);
    std::string create_ping_message();
    std::string create_pong_message();
    
    // JSON message parsing
    bool is_valid_json(const std::string& message);
    nlohmann::json parse_json_message(const std::string& message);
    std::string get_message_type(const nlohmann::json& json);
    std::string get_channel(const nlohmann::json& json);
    std::string get_symbol(const nlohmann::json& json);
    
    // Error handling
    std::string get_websocket_error_message(beast::error_code ec);
    bool is_connection_error(beast::error_code ec);
    bool is_temporary_error(beast::error_code ec);
    
    // URL utilities
    std::string build_websocket_url(const std::string& host, const std::string& port, 
                                   const std::string& path, bool use_ssl = true);
    std::string extract_host_from_url(const std::string& url);
    std::string extract_path_from_url(const std::string& url);
    
    // SSL context setup
    void setup_ssl_context(ssl::context& ctx);
    bool verify_certificate(bool preverified, ssl::verify_context& ctx);
}

} // namespace price_collector
} // namespace ats