#include "http_client.hpp"
#include "utils/logger.hpp"
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <iomanip>
#include <sstream>
#include <cctype>
#include <algorithm>

namespace ats {
namespace price_collector {

// RateLimiter Implementation
RateLimiter::RateLimiter(int requests_per_minute) 
    : max_requests_per_minute_(requests_per_minute) {
}

bool RateLimiter::can_make_request() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto now = std::chrono::steady_clock::now();
    auto minute_ago = now - std::chrono::minutes(1);
    
    // Remove old requests
    auto& times = const_cast<std::queue<std::chrono::steady_clock::time_point>&>(request_times_);
    while (!times.empty() && times.front() < minute_ago) {
        times.pop();
    }
    
    return static_cast<int>(request_times_.size()) < max_requests_per_minute_;
}

void RateLimiter::record_request() {
    std::lock_guard<std::mutex> lock(mutex_);
    request_times_.push(std::chrono::steady_clock::now());
}

std::chrono::milliseconds RateLimiter::get_delay_until_next_request() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (can_make_request()) {
        return std::chrono::milliseconds(0);
    }
    
    auto now = std::chrono::steady_clock::now();
    auto oldest_request = request_times_.front();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - oldest_request);
    auto remaining = std::chrono::minutes(1) - elapsed;
    
    return std::max(std::chrono::milliseconds(0), 
                   std::chrono::duration_cast<std::chrono::milliseconds>(remaining));
}

void RateLimiter::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    request_times_ = {};
}

// HttpClient Implementation Details
struct HttpClient::Implementation {
    net::io_context& ioc;
    ssl::context& ssl_ctx;
    std::string host;
    std::string port;
    bool use_ssl;
    
    tcp::resolver resolver;
    std::unique_ptr<beast::ssl_stream<beast::tcp_stream>> ssl_stream;
    std::unique_ptr<beast::tcp_stream> tcp_stream;
    
    std::string user_agent;
    std::unordered_map<std::string, std::string> default_headers;
    std::unique_ptr<RateLimiter> rate_limiter;
    
    // Statistics
    std::atomic<size_t> total_requests{0};
    std::atomic<size_t> successful_requests{0};
    std::atomic<size_t> failed_requests{0};
    std::vector<std::chrono::milliseconds> latencies;
    std::mutex latencies_mutex;
    
    beast::flat_buffer buffer;
    std::atomic<bool> connected{false};
    
    Implementation(net::io_context& ioc_, ssl::context& ssl_ctx_, 
                  const std::string& host_, const std::string& port_, bool use_ssl_)
        : ioc(ioc_), ssl_ctx(ssl_ctx_), host(host_), port(port_), use_ssl(use_ssl_)
        , resolver(net::make_strand(ioc))
        , user_agent("ATS-V3/1.0 HttpClient") {
        
        if (use_ssl) {
            ssl_stream = std::make_unique<beast::ssl_stream<beast::tcp_stream>>(
                net::make_strand(ioc), ssl_ctx);
        } else {
            tcp_stream = std::make_unique<beast::tcp_stream>(net::make_strand(ioc));
        }
    }
};

HttpClient::HttpClient(net::io_context& ioc, ssl::context& ssl_ctx, 
                      const std::string& host, const std::string& port, bool use_ssl)
    : impl_(std::make_unique<Implementation>(ioc, ssl_ctx, host, port, use_ssl)) {
}

HttpClient::~HttpClient() {
    disconnect();
}

bool HttpClient::connect() {
    try {
        auto const results = impl_->resolver.resolve(impl_->host, impl_->port);
        
        if (impl_->use_ssl) {
            // SSL connection
            beast::get_lowest_layer(*impl_->ssl_stream).connect(results);
            impl_->ssl_stream->handshake(ssl::stream_base::client);
        } else {
            // Plain TCP connection
            impl_->tcp_stream->connect(results);
        }
        
        impl_->connected = true;
        utils::Logger::info("HTTP client connected to {}:{}", impl_->host, impl_->port);
        return true;
        
    } catch (const std::exception& e) {
        utils::Logger::error("HTTP client connection failed: {}", e.what());
        impl_->connected = false;
        return false;
    }
}

void HttpClient::disconnect() {
    if (!impl_->connected) return;
    
    try {
        beast::error_code ec;
        
        if (impl_->use_ssl) {
            beast::get_lowest_layer(*impl_->ssl_stream).close();
        } else {
            impl_->tcp_stream->close();
        }
        
        impl_->connected = false;
        utils::Logger::info("HTTP client disconnected from {}:{}", impl_->host, impl_->port);
        
    } catch (const std::exception& e) {
        utils::Logger::error("HTTP client disconnect error: {}", e.what());
    }
}

bool HttpClient::is_connected() const {
    return impl_->connected;
}

HttpResponse HttpClient::request(const HttpRequest& req) {
    HttpResponse response;
    auto start_time = std::chrono::steady_clock::now();
    
    try {
        impl_->total_requests++;
        
        // Check rate limiting
        if (impl_->rate_limiter && !impl_->rate_limiter->can_make_request()) {
            auto delay = impl_->rate_limiter->get_delay_until_next_request();
            if (delay > std::chrono::milliseconds(0)) {
                response.error_message = "Rate limit exceeded, retry after " + 
                                       std::to_string(delay.count()) + "ms";
                impl_->failed_requests++;
                return response;
            }
        }
        
        // Build HTTP request
        http::request<http::string_body> http_req{
            http::string_to_verb(req.method), req.target, 11
        };
        
        http_req.set(http::field::host, impl_->host);
        http_req.set(http::field::user_agent, impl_->user_agent);
        
        // Add default headers
        for (const auto& [key, value] : impl_->default_headers) {
            http_req.set(key, value);
        }
        
        // Add request-specific headers
        for (const auto& [key, value] : req.headers) {
            http_req.set(key, value);
        }
        
        // Set body if present
        if (!req.body.empty()) {
            http_req.body() = req.body;
            http_req.prepare_payload();
        }
        
        // Send request
        if (impl_->use_ssl) {
            http::write(*impl_->ssl_stream, http_req);
        } else {
            http::write(*impl_->tcp_stream, http_req);
        }
        
        // Read response
        impl_->buffer.clear();
        http::response<http::string_body> http_res;
        
        if (impl_->use_ssl) {
            http::read(*impl_->ssl_stream, impl_->buffer, http_res);
        } else {
            http::read(*impl_->tcp_stream, impl_->buffer, http_res);
        }
        
        // Fill response
        response.status_code = static_cast<int>(http_res.result());
        response.body = http_res.body();
        response.success = http_utils::is_success_status(response.status_code);
        
        // Copy headers
        for (const auto& header : http_res) {
            response.headers[std::string(header.name_string())] = 
                std::string(header.value());
        }
        
        auto end_time = std::chrono::steady_clock::now();
        response.latency = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time);
        
        // Record statistics
        {
            std::lock_guard<std::mutex> lock(impl_->latencies_mutex);
            impl_->latencies.push_back(response.latency);
            if (impl_->latencies.size() > 1000) {
                impl_->latencies.erase(impl_->latencies.begin());
            }
        }
        
        if (response.success) {
            impl_->successful_requests++;
        } else {
            impl_->failed_requests++;
            response.error_message = "HTTP " + std::to_string(response.status_code) + 
                                   " " + http_utils::get_status_message(response.status_code);
        }
        
        // Record rate limiting
        if (impl_->rate_limiter) {
            impl_->rate_limiter->record_request();
        }
        
    } catch (const std::exception& e) {
        response.success = false;
        response.error_message = e.what();
        impl_->failed_requests++;
        
        auto end_time = std::chrono::steady_clock::now();
        response.latency = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time);
    }
    
    return response;
}

void HttpClient::async_request(const HttpRequest& req, HttpResponseCallback callback) {
    auto http_req = std::make_shared<http::request<http::string_body>>(
        http::string_to_verb(req.method), req.target, 11
    );
    
    http_req->set(http::field::host, impl_->host);
    http_req->set(http::field::user_agent, impl_->user_agent);
    
    // Add headers and body similar to synchronous version
    for (const auto& [key, value] : impl_->default_headers) {
        http_req->set(key, value);
    }
    
    for (const auto& [key, value] : req.headers) {
        http_req->set(key, value);
    }
    
    if (!req.body.empty()) {
        http_req->body() = req.body;
        http_req->prepare_payload();
    }
    
    // Start async chain
    impl_->resolver.async_resolve(impl_->host, impl_->port,
        beast::bind_front_handler(&HttpClient::on_resolve, shared_from_this(),
                                 http_req, callback));
}

void HttpClient::set_user_agent(const std::string& user_agent) {
    impl_->user_agent = user_agent;
}

void HttpClient::set_default_headers(const std::unordered_map<std::string, std::string>& headers) {
    impl_->default_headers = headers;
}

void HttpClient::set_rate_limiter(std::unique_ptr<RateLimiter> limiter) {
    impl_->rate_limiter = std::move(limiter);
}

size_t HttpClient::get_total_requests() const {
    return impl_->total_requests;
}

size_t HttpClient::get_successful_requests() const {
    return impl_->successful_requests;
}

size_t HttpClient::get_failed_requests() const {
    return impl_->failed_requests;
}

std::chrono::milliseconds HttpClient::get_average_latency() const {
    std::lock_guard<std::mutex> lock(impl_->latencies_mutex);
    
    if (impl_->latencies.empty()) {
        return std::chrono::milliseconds(0);
    }
    
    auto total = std::accumulate(impl_->latencies.begin(), impl_->latencies.end(),
                                std::chrono::milliseconds(0));
    return total / impl_->latencies.size();
}

// Async handlers implementation would go here...
void HttpClient::on_resolve(beast::error_code ec, tcp::resolver::results_type results,
                           std::shared_ptr<http::request<http::string_body>> req,
                           HttpResponseCallback callback) {
    // Implementation details for async operation...
    // This would handle the full async chain similar to WebSocket implementation
}

// HTTP utility functions
namespace http_utils {

std::string url_encode(const std::string& str) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;
    
    for (char c : str) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            escaped << std::uppercase;
            escaped << '%' << std::setw(2) << static_cast<int>(static_cast<unsigned char>(c));
            escaped << std::nouppercase;
        }
    }
    
    return escaped.str();
}

std::string url_decode(const std::string& str) {
    std::string decoded;
    decoded.reserve(str.size());
    
    for (size_t i = 0; i < str.size(); ++i) {
        if (str[i] == '%' && i + 2 < str.size()) {
            std::string hex = str.substr(i + 1, 2);
            char c = static_cast<char>(std::stoi(hex, nullptr, 16));
            decoded += c;
            i += 2;
        } else if (str[i] == '+') {
            decoded += ' ';
        } else {
            decoded += str[i];
        }
    }
    
    return decoded;
}

std::string build_query_string(const std::unordered_map<std::string, std::string>& params) {
    if (params.empty()) return "";
    
    std::ostringstream query;
    bool first = true;
    
    for (const auto& [key, value] : params) {
        if (!first) query << "&";
        query << url_encode(key) << "=" << url_encode(value);
        first = false;
    }
    
    return query.str();
}

std::unordered_map<std::string, std::string> default_headers() {
    return {
        {"Accept", "application/json"},
        {"Accept-Encoding", "gzip, deflate"},
        {"Connection", "keep-alive"}
    };
}

std::unordered_map<std::string, std::string> json_headers() {
    auto headers = default_headers();
    headers["Content-Type"] = "application/json";
    return headers;
}

bool is_success_status(int status_code) {
    return status_code >= 200 && status_code < 300;
}

bool is_client_error(int status_code) {
    return status_code >= 400 && status_code < 500;
}

bool is_server_error(int status_code) {
    return status_code >= 500 && status_code < 600;
}

std::string get_status_message(int status_code) {
    switch (status_code) {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 429: return "Too Many Requests";
        case 500: return "Internal Server Error";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        default: return "Unknown Status";
    }
}

std::string format_error(const std::string& operation, const beast::error_code& ec) {
    return operation + " failed: " + ec.message();
}

} // namespace http_utils

} // namespace price_collector
} // namespace ats