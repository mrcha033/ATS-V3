#include "websocket_client.hpp"
#include "../utils/logger.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <random>
#include <algorithm>
#include <cmath>

#ifdef HAVE_CURL
#include <curl/curl.h>
#endif

#ifdef HAVE_OPENSSL
#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#endif

// Undefine problematic Windows macros after all includes
#ifdef SendMessage
    #undef SendMessage
#endif
#ifdef ERROR
    #undef ERROR
#endif

namespace ats {

// WebSocketClient Implementation
WebSocketClient::WebSocketClient()
    : state_(WebSocketState::DISCONNECTED), should_reconnect_(false), running_(false),
      base_reconnect_delay_(std::chrono::seconds(1)), max_reconnect_delay_(std::chrono::seconds(60)),
      max_reconnect_attempts_(10), reconnect_attempts_(0), backoff_multiplier_(2.0),
      max_queue_size_(1000), messages_sent_(0), messages_received_(0), bytes_sent_(0), bytes_received_(0),
      connection_attempts_(0), max_message_size_(1024 * 1024), ping_interval_(std::chrono::seconds(30)),
      pong_timeout_(std::chrono::seconds(10)), connected_(false), user_agent_("ATS-V3-WebSocket/1.0"),
      default_timeout_ms_(5000), verify_ssl_(true), auto_reconnect_(true), reconnect_interval_ms_(5000) {
    
    last_message_time_ = std::chrono::steady_clock::now();
    last_reconnect_attempt_ = std::chrono::steady_clock::now();
}

WebSocketClient::~WebSocketClient() {
    Disconnect();
}

bool WebSocketClient::Connect(const std::string& url) {
    if (state_.load() == WebSocketState::CONNECTED) {
        LOG_WARNING("WebSocket already connected");
        return true;
    }
    
    url_ = url;
    running_ = true;
    state_ = WebSocketState::CONNECTING;
    
    // Start connection thread
    connection_thread_ = std::thread(&WebSocketClient::ConnectionLoop, this);
    
    // Start worker threads
    worker_thread_ = std::thread(&WebSocketClient::WorkerLoop, this);
    send_thread_ = std::thread(&WebSocketClient::SendLoop, this);
    
    LOG_INFO("WebSocket connecting to: {}", url);
    return true;
}

void WebSocketClient::Disconnect() {
    if (!running_.load()) {
        return;
    }
    
    running_ = false;
    connected_ = false;
    state_ = WebSocketState::DISCONNECTED;
    
    // Notify all threads to stop
    queue_cv_.notify_all();
    
    // Join threads
    if (connection_thread_.joinable()) {
        connection_thread_.join();
    }
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    if (send_thread_.joinable()) {
        send_thread_.join();
    }
    
    LOG_INFO("WebSocket disconnected from: {}", url_);
}

void WebSocketClient::ForceReconnect() {
    if (running_.load()) {
        connected_ = false;
        state_ = WebSocketState::RECONNECTING;
        LOG_INFO("Force reconnecting WebSocket to: {}", url_);
    }
}

bool WebSocketClient::SendMessage(const std::string& message) {
    if (!IsConnected()) {
        LOG_WARNING("Cannot send message: WebSocket not connected");
        return false;
    }
    
    if (message.length() > max_message_size_) {
        LOG_ERROR("Message too large: {} bytes (max: {})", message.length(), max_message_size_);
        return false;
    }
    
    // Add to outgoing queue with overflow protection
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (outgoing_queue_.size() >= max_queue_size_) {
            LOG_WARNING("Message queue overflow, dropping oldest message");
            outgoing_queue_.pop(); // Remove oldest message
        }
        outgoing_queue_.push(message);
    }
    queue_cv_.notify_one();
    
    return true;
}

bool WebSocketClient::SendPing() {
    if (!IsConnected()) {
        return false;
    }
    
    // Simple ping implementation
    return SendMessage("ping");
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

void WebSocketClient::SetAutoReconnect(bool auto_reconnect, int interval_ms) {
    auto_reconnect_ = auto_reconnect;
    reconnect_interval_ms_ = interval_ms;
    reconnect_delay_ = std::chrono::milliseconds(interval_ms);
}

std::chrono::milliseconds WebSocketClient::GetTimeSinceLastMessage() const {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now - last_message_time_);
}

double WebSocketClient::GetMessageRate() const {
    auto duration = GetTimeSinceLastMessage();
    if (duration.count() == 0) return 0.0;
    
    double seconds = duration.count() / 1000.0;
    return messages_received_.load() / seconds;
}

bool WebSocketClient::IsHealthy() const {
    if (!IsConnected()) return false;
    
    // Check if we've received messages recently
    auto time_since_last = GetTimeSinceLastMessage();
    return time_since_last < std::chrono::minutes(5);
}

void WebSocketClient::LogStatistics() const {
    LOG_INFO("=== WebSocket Statistics ===");
    LOG_INFO("URL: {}", url_);
    LOG_INFO("State: {}", GetConnectionStatus());
    LOG_INFO("Messages sent: {}", messages_sent_.load());
    LOG_INFO("Messages received: {}", messages_received_.load());
    LOG_INFO("Bytes sent: {}", bytes_sent_.load());
    LOG_INFO("Bytes received: {}", bytes_received_.load());
    LOG_INFO("Connection attempts: {}", connection_attempts_.load());
    LOG_INFO("Message rate: {:.2f} msg/s", GetMessageRate());
    LOG_INFO("Time since last message: {} ms", GetTimeSinceLastMessage().count());
}

void WebSocketClient::ResetStatistics() {
    messages_sent_ = 0;
    messages_received_ = 0;
    bytes_sent_ = 0;
    bytes_received_ = 0;
    connection_attempts_ = 0;
}

std::string WebSocketClient::GetConnectionStatus() const {
    switch (state_.load()) {
        case WebSocketState::DISCONNECTED: return "DISCONNECTED";
        case WebSocketState::CONNECTING: return "CONNECTING";
        case WebSocketState::CONNECTED: return "CONNECTED";
        case WebSocketState::RECONNECTING: return "RECONNECTING";
        case WebSocketState::ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

void WebSocketClient::ConnectionLoop() {
    LOG_DEBUG("WebSocket connection loop started");
    
    while (running_.load()) {
        try {
            if (!connected_.load() && state_.load() != WebSocketState::CONNECTED) {
                if (AttemptConnection()) {
                    connected_ = true;
                    state_ = WebSocketState::CONNECTED;
                    reconnect_attempts_ = 0;
                    
                    if (state_callback_) {
                        state_callback_(WebSocketState::CONNECTED);
                    }
                } else {
                    HandleReconnection();
                }
            }
            
            std::this_thread::sleep_for(std::chrono::seconds(1));
            
        } catch (const std::exception& e) {
            LOG_ERROR("Error in WebSocket connection loop: {}", e.what());
            OnError(e.what());
        }
    }
    
    LOG_DEBUG("WebSocket connection loop stopped");
}

bool WebSocketClient::AttemptConnection() {
    connection_attempts_.fetch_add(1);
    
    // Simplified connection attempt - in real implementation would use actual WebSocket protocol
    LOG_INFO("Attempting WebSocket connection to: {}", url_);
    
    if (PerformHandshake()) {
        LOG_INFO("WebSocket connection established");
        return true;
    } else {
        LOG_WARNING("WebSocket connection failed");
        return false;
    }
}

void WebSocketClient::HandleReconnection() {
    if (!auto_reconnect_ || !running_.load()) {
        return;
    }
    
    reconnect_attempts_.fetch_add(1);
    
    if (reconnect_attempts_.load() >= max_reconnect_attempts_) {
        LOG_ERROR("Max reconnection attempts reached, giving up");
        state_ = WebSocketState::ERROR;
        running_ = false;
        return;
    }
    
    state_ = WebSocketState::RECONNECTING;
    if (state_callback_) {
        state_callback_(WebSocketState::RECONNECTING);
    }
    
    // Calculate exponential backoff delay
    auto delay = base_reconnect_delay_ * static_cast<int>(std::pow(backoff_multiplier_, reconnect_attempts_.load() - 1));
    delay = std::min(delay, max_reconnect_delay_);
    
    LOG_INFO("Reconnecting in {} seconds (attempt {}/{}) using exponential backoff", 
             delay.count(), reconnect_attempts_.load(), max_reconnect_attempts_);
    
    last_reconnect_attempt_ = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(delay);
}

void WebSocketClient::ProcessOutgoingMessages() {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    
    while (!outgoing_queue_.empty() && connected_.load()) {
        std::string message = outgoing_queue_.front();
        outgoing_queue_.pop();
        
        if (SendFrame(message)) {
            messages_sent_.fetch_add(1);
            bytes_sent_.fetch_add(message.length());
        }
    }
}

void WebSocketClient::UpdateState(WebSocketState new_state) {
    WebSocketState old_state = state_.exchange(new_state);
    
    if (old_state != new_state && state_callback_) {
        state_callback_(new_state);
    }
}

void WebSocketClient::OnError(const std::string& error) {
    LOG_ERROR("WebSocket error: {}", error);
    
    if (error_callback_) {
        error_callback_(error);
    }
    
    connected_ = false;
    if (running_.load()) {
        UpdateState(WebSocketState::ERROR);
    }
}

void WebSocketClient::OnMessage(const std::string& message) {
    messages_received_.fetch_add(1);
    bytes_received_.fetch_add(message.length());
    last_message_time_ = std::chrono::steady_clock::now();
    
    if (message_callback_) {
        message_callback_(message);
    }
}

bool WebSocketClient::PerformHandshake() {
#ifdef HAVE_CURL
    CURL* curl = curl_easy_init();
    if (!curl) {
        LOG_ERROR("Failed to initialize CURL for WebSocket handshake");
        return false;
    }

    std::string key = GenerateKey();
    std::string accept_key = ComputeAcceptKey(key);

    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Connection: Upgrade");
    headers = curl_slist_append(headers, "Upgrade: websocket");
    headers = curl_slist_append(headers, "Sec-WebSocket-Version: 13");
    headers = curl_slist_append(headers, ("Sec-WebSocket-Key: " + key).c_str());
    headers = curl_slist_append(headers, ("User-Agent: " + user_agent_).c_str());

    std::string response_header;
    curl_easy_setopt(curl, CURLOPT_URL, url_.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response_header);
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, default_timeout_ms_);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, verify_ssl_ ? 1L : 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, verify_ssl_ ? 2L : 0L);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);

    if (res != CURLE_OK) {
        LOG_ERROR("WebSocket handshake failed: {}", curl_easy_strerror(res));
        curl_easy_cleanup(curl);
        return false;
    }

    long response_code;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    curl_easy_cleanup(curl);

    if (response_code != 101) {
        LOG_ERROR("WebSocket handshake failed with HTTP status code: {}", response_code);
        return false;
    }

    // TODO: Validate the Sec-WebSocket-Accept header in the response
    
    return true;
#else
    LOG_ERROR("CURL not available, cannot perform WebSocket handshake.");
    // TODO: Implement the actual WebSocket handshake protocol
    // Simplified handshake - real implementation would use HTTP upgrade
    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Simulate connection time
    return true; // Assume success for now
#endif
}

bool WebSocketClient::SendFrame(const std::string& payload, bool is_text) {
#ifdef HAVE_CURL
    CURL* curl = curl_easy_init();
    if (!curl) {
        LOG_ERROR("Failed to initialize CURL for WebSocket send");
        return false;
    }

    std::vector<unsigned char> frame;
    frame.push_back(0x81); // FIN + Text Frame

    if (payload.size() <= 125) {
        frame.push_back(payload.size() | 0x80); // Mask bit + payload length
    } else if (payload.size() <= 65535) {
        frame.push_back(126 | 0x80);
        frame.push_back((payload.size() >> 8) & 0xFF);
        frame.push_back(payload.size() & 0xFF);
    } else {
        frame.push_back(127 | 0x80);
        for (int i = 7; i >= 0; --i) {
            frame.push_back((payload.size() >> (i * 8)) & 0xFF);
        }
    }

    // Masking key
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);
    std::vector<unsigned char> mask(4);
    for (int i = 0; i < 4; ++i) {
        mask[i] = dis(gen);
        frame.push_back(mask[i]);
    }

    // Masked payload
    for (size_t i = 0; i < payload.size(); ++i) {
        frame.push_back(payload[i] ^ mask[i % 4]);
    }

    size_t sent = 0;
    CURLcode res = curl_easy_send(curl, frame.data(), frame.size(), &sent);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        LOG_ERROR("Failed to send WebSocket frame: {}", curl_easy_strerror(res));
        return false;
    }

    return true;
#else
    LOG_ERROR("CURL not available, cannot send WebSocket frame.");
    return false;
#endif
}

bool WebSocketClient::ReceiveFrame(std::string& payload) {
#ifdef HAVE_CURL
    CURL* curl = curl_easy_init();
    if (!curl) {
        LOG_ERROR("Failed to initialize CURL for WebSocket receive");
        return false;
    }

    unsigned char header[2];
    size_t received = 0;
    CURLcode res = curl_easy_recv(curl, header, 2, &received);
    if (res != CURLE_OK || received != 2) {
        // Handle error or connection closed
        curl_easy_cleanup(curl);
        return false;
    }

    bool fin = (header[0] & 0x80) != 0;
    unsigned char opcode = header[0] & 0x0F;
    bool masked = (header[1] & 0x80) != 0;
    unsigned long long payload_len = header[1] & 0x7F;

    if (payload_len == 126) {
        unsigned char ext_len[2];
        curl_easy_recv(curl, ext_len, 2, &received);
        payload_len = (ext_len[0] << 8) | ext_len[1];
    } else if (payload_len == 127) {
        unsigned char ext_len[8];
        curl_easy_recv(curl, ext_len, 8, &received);
        payload_len = 0;
        for (int i = 0; i < 8; ++i) {
            payload_len = (payload_len << 8) | ext_len[i];
        }
    }

    std::vector<unsigned char> mask(4);
    if (masked) {
        curl_easy_recv(curl, mask.data(), 4, &received);
    }

    payload.resize(payload_len);
    curl_easy_recv(curl, &payload[0], payload_len, &received);

    if (masked) {
        for (size_t i = 0; i < payload.size(); ++i) {
            payload[i] ^= mask[i % 4];
        }
    }

    curl_easy_cleanup(curl);
    return true;
#else
    LOG_ERROR("CURL not available, cannot receive WebSocket frame.");
    return false;
#endif
}

std::string WebSocketClient::GenerateKey() {
    // Generate random WebSocket key for handshake
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);
    
    std::vector<unsigned char> key_bytes(16);
    for (int i = 0; i < 16; ++i) {
        key_bytes[i] = dis(gen);
    }

#ifdef HAVE_OPENSSL
    BIO *b64 = BIO_new(BIO_f_base64());
    BIO *bio = BIO_new(BIO_s_mem());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    BIO_push(b64, bio);
    BIO_write(b64, key_bytes.data(), 16);
    BIO_flush(b64);

    BUF_MEM *bptr;
    BIO_get_mem_ptr(b64, &bptr);
    std::string encoded_key(bptr->data, bptr->length);
    BIO_free_all(b64);

    return encoded_key;
#else
    // Fallback to a less secure method if OpenSSL is not available
    std::ostringstream oss;
    for (int i = 0; i < 16; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') << dis(gen);
    }
    return oss.str();
#endif
}

std::string WebSocketClient::ComputeAcceptKey(const std::string& key) {
    const std::string magic_string = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::string combined = key + magic_string;

#ifdef HAVE_OPENSSL
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const unsigned char*>(combined.c_str()), combined.length(), hash);

    BIO *b64 = BIO_new(BIO_f_base64());
    BIO *bio = BIO_new(BIO_s_mem());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    BIO_push(b64, bio);
    BIO_write(b64, hash, SHA_DIGEST_LENGTH);
    BIO_flush(b64);

    BUF_MEM *bptr;
    BIO_get_mem_ptr(b64, &bptr);
    std::string accept_key(bptr->data, bptr->length);
    BIO_free_all(b64);

    return accept_key;
#else
    // Simplified accept key computation - real implementation would use SHA-1 + base64
    return key + "_accepted";
#endif
}

bool WebSocketClient::ValidateHttpResponse(const std::string& response) {
    // Simplified response validation
    return response.find("101") != std::string::npos;
}

void WebSocketClient::WorkerLoop() {
    LOG_DEBUG("WebSocket worker loop started");
    
    while (running_.load()) {
        try {
            if (connected_.load()) {
                // Handle incoming messages (simplified)
                std::string payload;
                if (ReceiveFrame(payload)) {
                    OnMessage(payload);
                }
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            
        } catch (const std::exception& e) {
            LOG_ERROR("Error in WebSocket worker loop: {}", e.what());
            OnError(e.what());
        }
    }
    
    LOG_DEBUG("WebSocket worker loop stopped");
}

void WebSocketClient::SendLoop() {
    LOG_DEBUG("WebSocket send loop started");
    
    while (running_.load()) {
        try {
            if (connected_.load()) {
                ProcessOutgoingMessages();
            }
            
            // Wait for messages or timeout
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait_for(lock, std::chrono::milliseconds(100));
            
        } catch (const std::exception& e) {
            LOG_ERROR("Error in WebSocket send loop: {}", e.what());
            OnError(e.what());
        }
    }
    
    LOG_DEBUG("WebSocket send loop stopped");
}

void WebSocketClient::HandleMessage(const std::string& message) {
    OnMessage(message);
}

void WebSocketClient::HandleError(const std::string& error) {
    OnError(error);
}

void WebSocketClient::Reconnect() {
    if (running_.load()) {
        connected_ = false;
        HandleReconnection();
    }
}

// WebSocketManager Implementation
WebSocketManager::~WebSocketManager() {
    DisconnectAll();
}

bool WebSocketManager::AddClient(const std::string& name, const std::string& url) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    
    if (clients_.find(name) != clients_.end()) {
        LOG_WARNING("WebSocket client {} already exists", name);
        return false;
    }
    
    auto client = std::make_unique<WebSocketClient>();
    client->SetStateCallback([this, name](WebSocketState state) {
        OnClientStateChange(name, state);
    });
    client->SetErrorCallback([this, name](const std::string& error) {
        OnClientError(name, error);
    });
    client->SetMessageCallback([this, name](const std::string& message) {
        OnClientMessage(name, message);
    });
    
    client_urls_[name] = url;
    clients_[name] = std::move(client);
    
    LOG_INFO("Added WebSocket client: {} -> {}", name, url);
    return true;
}

void WebSocketManager::RemoveClient(const std::string& name) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    
    auto it = clients_.find(name);
    if (it != clients_.end()) {
        it->second->Disconnect();
        clients_.erase(it);
        client_urls_.erase(name);
        LOG_INFO("Removed WebSocket client: {}", name);
    }
}

WebSocketClient* WebSocketManager::GetClient(const std::string& name) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    
    auto it = clients_.find(name);
    return (it != clients_.end()) ? it->second.get() : nullptr;
}

void WebSocketManager::ConnectAll() {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    
    for (const auto& pair : clients_) {
        const std::string& name = pair.first;
        auto& client = pair.second;
        
        auto url_it = client_urls_.find(name);
        if (url_it != client_urls_.end()) {
            client->Connect(url_it->second);
        }
    }
    
    LOG_INFO("Connecting all WebSocket clients ({})", clients_.size());
}

void WebSocketManager::DisconnectAll() {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    
    for (auto& pair : clients_) {
        pair.second->Disconnect();
    }
    
    LOG_INFO("Disconnected all WebSocket clients");
}

void WebSocketManager::SendToAll(const std::string& message) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    
    for (auto& pair : clients_) {
        pair.second->SendMessage(message);
    }
}

std::vector<std::string> WebSocketManager::GetConnectedClients() const {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    std::vector<std::string> connected;
    
    for (const auto& pair : clients_) {
        if (pair.second->IsConnected()) {
            connected.push_back(pair.first);
        }
    }
    
    return connected;
}

std::vector<std::string> WebSocketManager::GetDisconnectedClients() const {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    std::vector<std::string> disconnected;
    
    for (const auto& pair : clients_) {
        if (!pair.second->IsConnected()) {
            disconnected.push_back(pair.first);
        }
    }
    
    return disconnected;
}

bool WebSocketManager::AreAllConnected() const {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    
    for (const auto& pair : clients_) {
        if (!pair.second->IsConnected()) {
            return false;
        }
    }
    
    return !clients_.empty();
}

void WebSocketManager::LogAllStatistics() const {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    
    LOG_INFO("=== WebSocket Manager Statistics ===");
    LOG_INFO("Total clients: {}", clients_.size());
    
    for (const auto& pair : clients_) {
        LOG_INFO("Client: {}", pair.first);
        pair.second->LogStatistics();
    }
}

size_t WebSocketManager::GetTotalMessagesReceived() const {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    size_t total = 0;
    
    for (const auto& pair : clients_) {
        total += pair.second->GetMessagesReceived();
    }
    
    return total;
}

size_t WebSocketManager::GetTotalMessagesSent() const {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    size_t total = 0;
    
    for (const auto& pair : clients_) {
        total += pair.second->GetMessagesSent();
    }
    
    return total;
}

void WebSocketManager::OnClientMessage(const std::string& client_name, const std::string& message) {
    if (symbol_callback_) {
        symbol_callback_(client_name, message);
    }
}

void WebSocketManager::OnClientStateChange(const std::string& client_name, WebSocketState state) {
    LOG_DEBUG("WebSocket client {} state changed to: {}", client_name, static_cast<int>(state));
}

void WebSocketManager::OnClientError(const std::string& client_name, const std::string& error) {
    LOG_ERROR("WebSocket client {} error: {}", client_name, error);
}

} // namespace ats
