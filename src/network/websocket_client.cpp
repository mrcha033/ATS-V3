#include "websocket_client.hpp"
#include "../utils/logger.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <random>
#include <algorithm>

#ifdef HAVE_CURL
#include <curl/curl.h>
#endif

namespace ats {

// WebSocketClient Implementation
WebSocketClient::WebSocketClient() 
    : state_(WebSocketState::DISCONNECTED)
    , should_reconnect_(true)
    , running_(false)
    , reconnect_delay_(std::chrono::seconds(5))
    , max_reconnect_attempts_(10)
    , reconnect_attempts_(0)
    , messages_sent_(0)
    , messages_received_(0)
    , bytes_sent_(0)
    , bytes_received_(0)
    , connection_attempts_(0)
    , last_message_time_(std::chrono::steady_clock::now())
    , max_message_size_(1024 * 1024) // 1MB
    , ping_interval_(std::chrono::seconds(30))
    , pong_timeout_(std::chrono::seconds(10)) {
}

WebSocketClient::~WebSocketClient() {
    Disconnect();
}

bool WebSocketClient::Connect(const std::string& url) {
    if (state_.load() == WebSocketState::CONNECTED) {
        LOG_WARNING("WebSocket already connected to {}", url);
        return true;
    }
    
    url_ = url;
    running_ = true;
    state_ = WebSocketState::CONNECTING;
    
    connection_thread_ = std::thread(&WebSocketClient::ConnectionLoop, this);
    
    LOG_INFO("WebSocket connecting to {}", url);
    return true;
}

void WebSocketClient::Disconnect() {
    if (!running_.load()) {
        return;
    }
    
    running_ = false;
    should_reconnect_ = false;
    
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        queue_cv_.notify_all();
    }
    
    if (connection_thread_.joinable()) {
        connection_thread_.join();
    }
    
    UpdateState(WebSocketState::DISCONNECTED);
    LOG_INFO("WebSocket disconnected");
}

void WebSocketClient::ForceReconnect() {
    if (running_.load()) {
        reconnect_attempts_ = 0;
        // Trigger reconnection by updating state
        if (state_.load() == WebSocketState::CONNECTED) {
            UpdateState(WebSocketState::RECONNECTING);
        }
        LOG_INFO("WebSocket forced reconnection triggered");
    }
}

void WebSocketClient::SetUserAgent(const std::string& user_agent) {
    user_agent_ = user_agent;
}

void WebSocketClient::SetDefaultTimeout(int timeout_ms) {
    default_timeout_ms_ = timeout_ms;
}

void WebSocketClient::SetSslVerification(bool verify) {
    verify_ssl_ = verify;
}

bool WebSocketClient::SendMessage(const std::string& message) {
    if (state_.load() != WebSocketState::CONNECTED) {
        LOG_WARNING("Cannot send message - WebSocket not connected");
        return false;
    }
    
    if (message.size() > max_message_size_) {
        LOG_ERROR("Message too large: {} bytes (max: {})", message.size(), max_message_size_);
        return false;
    }
    
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        outgoing_queue_.push(message);
        queue_cv_.notify_one();
    }
    
    return true;
}

bool WebSocketClient::SendPing() {
    return SendMessage("ping"); // Simplified ping implementation
}

std::chrono::milliseconds WebSocketClient::GetTimeSinceLastMessage() const {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now - last_message_time_);
}

double WebSocketClient::GetMessageRate() const {
    auto total_messages = messages_sent_.load() + messages_received_.load();
    if (total_messages == 0) return 0.0;
    
    // Simple rate calculation - could be improved with time window
    return static_cast<double>(total_messages) / 60.0; // messages per minute approximation
}

bool WebSocketClient::IsHealthy() const {
    if (state_.load() != WebSocketState::CONNECTED) {
        return false;
    }
    
    // Check if we've received messages recently
    auto time_since_last = GetTimeSinceLastMessage();
    return time_since_last < std::chrono::minutes(5);
}

void WebSocketClient::LogStatistics() const {
    LOG_INFO("WebSocket Statistics for {}", url_);
    LOG_INFO("  State: {}", static_cast<int>(state_.load()));
    LOG_INFO("  Messages sent: {}", messages_sent_.load());
    LOG_INFO("  Messages received: {}", messages_received_.load());
    LOG_INFO("  Bytes sent: {}", bytes_sent_.load());
    LOG_INFO("  Bytes received: {}", bytes_received_.load());
    LOG_INFO("  Connection attempts: {}", connection_attempts_.load());
    LOG_INFO("  Reconnect attempts: {}", reconnect_attempts_.load());
}

void WebSocketClient::ResetStatistics() {
    messages_sent_ = 0;
    messages_received_ = 0;
    bytes_sent_ = 0;
    bytes_received_ = 0;
    connection_attempts_ = 0;
    reconnect_attempts_ = 0;
}

void WebSocketClient::ConnectionLoop() {
    LOG_INFO("WebSocket connection loop started for {}", url_);
    
    while (running_.load()) {
        try {
            if (state_.load() == WebSocketState::CONNECTING || 
                state_.load() == WebSocketState::RECONNECTING) {
                
                if (AttemptConnection()) {
                    UpdateState(WebSocketState::CONNECTED);
                    LOG_INFO("WebSocket connected successfully to {}", url_);
                    
                    // Start processing messages
                    ProcessOutgoingMessages();
                } else {
                    HandleReconnection();
                }
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
        } catch (const std::exception& e) {
            LOG_ERROR("Error in WebSocket connection loop: {}", e.what());
            OnError("Connection loop error: " + std::string(e.what()));
            HandleReconnection();
        }
    }
    
    LOG_INFO("WebSocket connection loop ended");
}

bool WebSocketClient::AttemptConnection() {
    connection_attempts_++;
    
    try {
        // Simplified connection attempt - in real implementation would use actual WebSocket library
        LOG_DEBUG("Attempting WebSocket connection to {}", url_);
        
        if (!PerformHandshake()) {
            LOG_ERROR("WebSocket handshake failed for {}", url_);
            return false;
        }
        
        last_message_time_ = std::chrono::steady_clock::now();
        return true;
        
    } catch (const std::exception& e) {
        OnError("Connection attempt failed: " + std::string(e.what()));
        return false;
    }
}

void WebSocketClient::HandleReconnection() {
    if (!should_reconnect_.load() || !running_.load()) {
        UpdateState(WebSocketState::DISCONNECTED);
        return;
    }
    
    if (reconnect_attempts_.load() >= max_reconnect_attempts_) {
        LOG_ERROR("Maximum reconnection attempts reached for {}", url_);
        UpdateState(WebSocketState::ERROR);
        OnError("Maximum reconnection attempts exceeded");
        return;
    }
    
    reconnect_attempts_++;
    UpdateState(WebSocketState::RECONNECTING);
    
    LOG_INFO("WebSocket reconnecting to {} (attempt {}/{})", 
             url_, reconnect_attempts_.load(), max_reconnect_attempts_);
    
    // Wait before attempting reconnection
    std::this_thread::sleep_for(reconnect_delay_);
}

void WebSocketClient::ProcessOutgoingMessages() {
    while (running_.load() && state_.load() == WebSocketState::CONNECTED) {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        
        if (outgoing_queue_.empty()) {
            queue_cv_.wait_for(lock, std::chrono::seconds(1));
            continue;
        }
        
        std::string message = outgoing_queue_.front();
        outgoing_queue_.pop();
        lock.unlock();
        
        try {
            if (SendFrame(message)) {
                messages_sent_++;
                bytes_sent_ += message.size();
                LOG_TRACE("WebSocket message sent: {} bytes", message.size());
            } else {
                LOG_ERROR("Failed to send WebSocket message");
                OnError("Failed to send message");
            }
        } catch (const std::exception& e) {
            LOG_ERROR("Error sending WebSocket message: {}", e.what());
            OnError("Send error: " + std::string(e.what()));
        }
    }
}

void WebSocketClient::UpdateState(WebSocketState new_state) {
    WebSocketState old_state = state_.exchange(new_state);
    
    if (old_state != new_state) {
        LOG_DEBUG("WebSocket state changed: {} -> {}", 
                 static_cast<int>(old_state), static_cast<int>(new_state));
        
        if (state_callback_) {
            try {
                state_callback_(new_state);
            } catch (const std::exception& e) {
                LOG_ERROR("Error in state callback: {}", e.what());
            }
        }
    }
}

void WebSocketClient::OnError(const std::string& error) {
    LOG_ERROR("WebSocket error: {}", error);
    
    if (error_callback_) {
        try {
            error_callback_(error);
        } catch (const std::exception& e) {
            LOG_ERROR("Error in error callback: {}", e.what());
        }
    }
}

void WebSocketClient::OnMessage(const std::string& message) {
    messages_received_++;
    bytes_received_ += message.size();
    last_message_time_ = std::chrono::steady_clock::now();
    
    LOG_TRACE("WebSocket message received: {} bytes", message.size());
    
    if (message_callback_) {
        try {
            message_callback_(message);
        } catch (const std::exception& e) {
            LOG_ERROR("Error in message callback: {}", e.what());
        }
    }
}

bool WebSocketClient::PerformHandshake() {
    // Simplified WebSocket handshake implementation
    // In real implementation, would perform proper HTTP upgrade request
    
    LOG_DEBUG("Performing WebSocket handshake for {}", url_);
    
    try {
        // Generate WebSocket key
        std::string key = GenerateKey();
        
        // Build handshake request (simplified)
        std::ostringstream request;
        request << "GET " << url_ << " HTTP/1.1\r\n";
        request << "Host: " << url_ << "\r\n";
        request << "Upgrade: websocket\r\n";
        request << "Connection: Upgrade\r\n";
        request << "Sec-WebSocket-Key: " << key << "\r\n";
        request << "Sec-WebSocket-Version: 13\r\n";
        request << "\r\n";
        
        // For stub implementation, just return true
        // Real implementation would send request and validate response
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("WebSocket handshake failed: {}", e.what());
        return false;
    }
}

bool WebSocketClient::SendFrame(const std::string& payload, bool is_text) {
    // Simplified WebSocket frame sending
    // Real implementation would construct proper WebSocket frame format
    
    try {
        LOG_TRACE("Sending WebSocket frame: {} bytes", payload.size());
        
        // For stub implementation, just simulate successful send
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to send WebSocket frame: {}", e.what());
        return false;
    }
}

bool WebSocketClient::ReceiveFrame(std::string& payload) {
    // Simplified WebSocket frame receiving
    // Real implementation would parse WebSocket frame format
    
    try {
        // For stub implementation, just simulate message reception
        static int message_counter = 0;
        
        if (message_counter++ % 100 == 0) {
            payload = "{\"type\":\"ping\",\"data\":\"keepalive\"}";
            return true;
        }
        
        return false;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to receive WebSocket frame: {}", e.what());
        return false;
    }
}

std::string WebSocketClient::GenerateKey() {
    // Generate random WebSocket key
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);
    
    std::string key;
    for (int i = 0; i < 16; ++i) {
        key += static_cast<char>(dis(gen));
    }
    
    // Base64 encode (simplified)
    return "dGhlIHNhbXBsZSBub25jZQ=="; // Placeholder base64 string
}

std::string WebSocketClient::ComputeAcceptKey(const std::string& key) {
    // Simplified WebSocket accept key computation
    // Real implementation would use SHA-1 hash
    return key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
}

bool WebSocketClient::ValidateHttpResponse(const std::string& response) {
    // Simplified HTTP response validation
    return response.find("HTTP/1.1 101") != std::string::npos &&
           response.find("Upgrade: websocket") != std::string::npos;
}

// WebSocketManager Implementation
WebSocketManager::~WebSocketManager() {
    DisconnectAll();
}

bool WebSocketManager::AddClient(const std::string& name, const std::string& url) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    
    if (clients_.find(name) != clients_.end()) {
        LOG_WARNING("WebSocket client '{}' already exists", name);
        return false;
    }
    
    auto client = std::make_unique<WebSocketClient>();
    
    // Set up callbacks
    client->SetMessageCallback([this, name](const std::string& message) {
        OnClientMessage(name, message);
    });
    
    client->SetStateCallback([this, name](WebSocketState state) {
        OnClientStateChange(name, state);
    });
    
    client->SetErrorCallback([this, name](const std::string& error) {
        OnClientError(name, error);
    });
    
    clients_[name] = std::move(client);
    client_urls_[name] = url; // Store the URL for later use
    
    LOG_INFO("WebSocket client '{}' added for URL: {}", name, url);
    return true;
}

void WebSocketManager::RemoveClient(const std::string& name) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    
    auto it = clients_.find(name);
    if (it != clients_.end()) {
        it->second->Disconnect();
        clients_.erase(it);
        client_urls_.erase(name); // Also remove stored URL
        LOG_INFO("WebSocket client '{}' removed", name);
    }
}

WebSocketClient* WebSocketManager::GetClient(const std::string& name) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    
    auto it = clients_.find(name);
    return (it != clients_.end()) ? it->second.get() : nullptr;
}

void WebSocketManager::ConnectAll() {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    
    for (const auto& [name, client] : clients_) {
        if (!client->IsConnected()) {
            LOG_INFO("Connecting WebSocket client '{}'", name);
            // Use stored URL instead of hardcoded one
            auto url_it = client_urls_.find(name);
            if (url_it != client_urls_.end()) {
                client->Connect(url_it->second);
            } else {
                LOG_WARNING("No URL found for WebSocket client '{}'", name);
            }
        }
    }
}

void WebSocketManager::DisconnectAll() {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    
    for (const auto& [name, client] : clients_) {
        LOG_INFO("Disconnecting WebSocket client '{}'", name);
        client->Disconnect();
    }
}

void WebSocketManager::SendToAll(const std::string& message) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    
    for (const auto& [name, client] : clients_) {
        if (client->IsConnected()) {
            client->SendMessage(message);
        }
    }
}

std::vector<std::string> WebSocketManager::GetConnectedClients() const {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    
    std::vector<std::string> connected;
    for (const auto& [name, client] : clients_) {
        if (client->IsConnected()) {
            connected.push_back(name);
        }
    }
    
    return connected;
}

std::vector<std::string> WebSocketManager::GetDisconnectedClients() const {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    
    std::vector<std::string> disconnected;
    for (const auto& [name, client] : clients_) {
        if (!client->IsConnected()) {
            disconnected.push_back(name);
        }
    }
    
    return disconnected;
}

bool WebSocketManager::AreAllConnected() const {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    
    for (const auto& [name, client] : clients_) {
        if (!client->IsConnected()) {
            return false;
        }
    }
    
    return !clients_.empty();
}

void WebSocketManager::LogAllStatistics() const {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    
    LOG_INFO("WebSocket Manager Statistics:");
    LOG_INFO("  Total clients: {}", clients_.size());
    
    for (const auto& [name, client] : clients_) {
        LOG_INFO("  Client '{}' statistics:", name);
        client->LogStatistics();
    }
}

size_t WebSocketManager::GetTotalMessagesReceived() const {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    
    size_t total = 0;
    for (const auto& [name, client] : clients_) {
        total += client->GetMessagesReceived();
    }
    
    return total;
}

size_t WebSocketManager::GetTotalMessagesSent() const {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    
    size_t total = 0;
    for (const auto& [name, client] : clients_) {
        total += client->GetMessagesSent();
    }
    
    return total;
}

void WebSocketManager::OnClientMessage(const std::string& client_name, const std::string& message) {
    LOG_TRACE("WebSocket message from '{}': {} bytes", client_name, message.size());
    
    if (symbol_callback_) {
        try {
            symbol_callback_(client_name, message);
        } catch (const std::exception& e) {
            LOG_ERROR("Error in symbol callback for client '{}': {}", client_name, e.what());
        }
    }
}

void WebSocketManager::OnClientStateChange(const std::string& client_name, WebSocketState state) {
    LOG_DEBUG("WebSocket client '{}' state changed to {}", client_name, static_cast<int>(state));
}

void WebSocketManager::OnClientError(const std::string& client_name, const std::string& error) {
    LOG_ERROR("WebSocket client '{}' error: {}", client_name, error);
}

} // namespace ats
