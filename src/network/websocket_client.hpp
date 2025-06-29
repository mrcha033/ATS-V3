#pragma once

// Prevent Windows header pollution
#if defined(_WIN32)
    
    // Undefine conflicting Windows macros before our declarations
    #ifdef SendMessage
        #undef SendMessage
    #endif
    #ifdef GetMessage  
        #undef GetMessage
    #endif
#endif

#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <chrono>
#include <unordered_map>
#include <memory>
#include "../core/types.hpp"

namespace ats {

// Note: WebSocketState moved to types.hpp to avoid duplication

struct WebSocketMessage {
    std::string data;
    std::chrono::steady_clock::time_point timestamp;
    size_t size;
    
    WebSocketMessage(const std::string& d) : data(d), 
                                           timestamp(std::chrono::steady_clock::now()),
                                           size(d.length()) {}
};

// WebSocket client implementation
class WebSocketClient {
public:
    using MessageCallback = std::function<void(const std::string&)>;
    using StateCallback = std::function<void(WebSocketState)>;
    using ErrorCallback = std::function<void(const std::string&)>;

private:
    // Connection info
    std::string url_;
    std::string protocol_;
    
    // State management
    std::atomic<WebSocketState> state_;
    std::thread connection_thread_;
    std::atomic<bool> should_reconnect_;
    std::atomic<bool> running_;
    
    // Callbacks
    MessageCallback message_callback_;
    StateCallback state_callback_;
    ErrorCallback error_callback_;
    
    // Reconnection settings with exponential backoff
    std::chrono::seconds base_reconnect_delay_;
    std::chrono::seconds max_reconnect_delay_;
    int max_reconnect_attempts_;
    std::atomic<int> reconnect_attempts_;
    double backoff_multiplier_;
    std::chrono::steady_clock::time_point last_reconnect_attempt_;
    
    // Message queue for outgoing messages with overflow protection
    std::queue<std::string> outgoing_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    size_t max_queue_size_;
    
    // Statistics
    std::atomic<long long> messages_sent_;
    std::atomic<long long> messages_received_;
    std::atomic<long long> bytes_sent_;
    std::atomic<long long> bytes_received_;
    std::atomic<long long> connection_attempts_;
    std::chrono::steady_clock::time_point last_message_time_;
    
    // Configuration
    size_t max_message_size_;
    std::chrono::seconds ping_interval_;
    std::chrono::seconds pong_timeout_;
    
    // Connection state
    std::atomic<bool> connected_;
    
    // Configuration
    std::string user_agent_;
    int default_timeout_ms_;
    bool verify_ssl_;
    bool auto_reconnect_;
    int reconnect_interval_ms_;
    std::chrono::milliseconds reconnect_delay_;
    
    // Threading
    std::thread worker_thread_;
    std::thread send_thread_;
    
public:
    WebSocketClient();
    ~WebSocketClient();
    
    // Connection management
    bool Connect(const std::string& url);
    void Disconnect();
    void ForceReconnect();
    
    // Configuration
    void SetMessageCallback(MessageCallback callback) { message_callback_ = callback; }
    void SetStateCallback(StateCallback callback) { state_callback_ = callback; }
    void SetErrorCallback(ErrorCallback callback) { error_callback_ = callback; }
    
    void SetReconnectDelay(std::chrono::seconds delay) { base_reconnect_delay_ = delay; }
    void SetMaxReconnectAttempts(int attempts) { max_reconnect_attempts_ = attempts; }
    void SetMaxMessageSize(size_t size) { max_message_size_ = size; }
    void SetPingInterval(std::chrono::seconds interval) { ping_interval_ = interval; }
    void SetPongTimeout(std::chrono::seconds timeout) { pong_timeout_ = timeout; }
    
    void SetUserAgent(const std::string& user_agent);
    void SetDefaultTimeout(int timeout_ms);
    void SetSslVerification(bool verify);
    void SetAutoReconnect(bool auto_reconnect, int interval_ms = 5000);
    
    // Message sending
    #if defined(_WIN32) && defined(SendMessage)
        #undef SendMessage
    #endif
    bool SendMessage(const std::string& message);
    bool SendPing();
    
    // State queries
    WebSocketState GetState() const { return state_.load(); }
    bool IsConnected() const { return state_.load() == WebSocketState::CONNECTED; }
    bool IsReconnecting() const { return state_.load() == WebSocketState::RECONNECTING; }
    
    // Statistics
    long long GetMessagesSent() const { return messages_sent_.load(); }
    long long GetMessagesReceived() const { return messages_received_.load(); }
    long long GetBytesSent() const { return bytes_sent_.load(); }
    long long GetBytesReceived() const { return bytes_received_.load(); }
    long long GetConnectionAttempts() const { return connection_attempts_.load(); }
    
    std::chrono::milliseconds GetTimeSinceLastMessage() const;
    double GetMessageRate() const; // messages per second
    bool IsHealthy() const;
    
    // Utility
    void LogStatistics() const;
    void ResetStatistics();
    
    // Status
    std::string GetConnectionStatus() const;
    
private:
    void ConnectionLoop();
    bool AttemptConnection();
    void HandleReconnection();
    void ProcessOutgoingMessages();
    void UpdateState(WebSocketState new_state);
    void OnError(const std::string& error);
    void OnMessage(const std::string& message);
    
    // WebSocket protocol implementation (simplified)
    bool PerformHandshake();
    bool SendFrame(const std::string& payload, bool is_text = true);
    bool ReceiveFrame(std::string& payload);
    
    // Utility functions
    std::string GenerateKey();
    std::string ComputeAcceptKey(const std::string& key);
    bool ValidateHttpResponse(const std::string& response);
    
    // Private methods
    void WorkerLoop();
    void SendLoop();
    void HandleMessage(const std::string& message);
    void HandleError(const std::string& error);
    void Reconnect();
};

// Multi-symbol WebSocket manager for exchanges
class WebSocketManager {
private:
    std::unordered_map<std::string, std::unique_ptr<WebSocketClient>> clients_;
    std::unordered_map<std::string, std::string> client_urls_; // Store URL for each client
    mutable std::mutex clients_mutex_;
    
    // Shared message handler
    using SymbolCallback = std::function<void(const std::string& symbol, const std::string& message)>;
    SymbolCallback symbol_callback_;
    
public:
    WebSocketManager() = default;
    ~WebSocketManager();
    
    // Client management
    bool AddClient(const std::string& name, const std::string& url);
    void RemoveClient(const std::string& name);
    WebSocketClient* GetClient(const std::string& name);
    
    // Callbacks
    void SetSymbolCallback(SymbolCallback callback) { symbol_callback_ = callback; }
    
    // Bulk operations
    void ConnectAll();
    void DisconnectAll();
    void SendToAll(const std::string& message);
    
    // State monitoring
    std::vector<std::string> GetConnectedClients() const;
    std::vector<std::string> GetDisconnectedClients() const;
    bool AreAllConnected() const;
    
    // Statistics
    void LogAllStatistics() const;
    size_t GetTotalMessagesReceived() const;
    size_t GetTotalMessagesSent() const;
    
private:
    void OnClientMessage(const std::string& client_name, const std::string& message);
    void OnClientStateChange(const std::string& client_name, WebSocketState state);
    void OnClientError(const std::string& client_name, const std::string& error);
};

} // namespace ats 