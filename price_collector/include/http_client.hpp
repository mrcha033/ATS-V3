#pragma once

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <set>
#include <queue>
#include <memory>
#include <string>
#include <functional>
#include <chrono>
#include <queue>
#include <mutex>

namespace ats {
namespace price_collector {

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = boost::asio::ip::tcp;

// HTTP response structure
struct HttpResponse {
    int status_code;
    std::string body;
    std::unordered_map<std::string, std::string> headers;
    std::chrono::milliseconds latency;
    bool success;
    std::string error_message;
    
    HttpResponse() : status_code(0), latency(0), success(false) {}
};

// HTTP request structure
struct HttpRequest {
    std::string method;
    std::string target;
    std::string body;
    std::unordered_map<std::string, std::string> headers;
    std::chrono::milliseconds timeout;
    
    HttpRequest(const std::string& m = "GET", const std::string& t = "/")
        : method(m), target(t), timeout(std::chrono::milliseconds(5000)) {}
};

// Callback types
using HttpResponseCallback = std::function<void(const HttpResponse&)>;

// Rate limiter for HTTP requests
class RateLimiter {
public:
    RateLimiter(int requests_per_minute);
    
    bool can_make_request() const;
    void record_request();
    std::chrono::milliseconds get_delay_until_next_request() const;
    void reset();
    
private:
    int max_requests_per_minute_;
    std::queue<std::chrono::steady_clock::time_point> request_times_;
    mutable std::mutex mutex_;
};

// Asynchronous HTTP client using Boost.Beast
class HttpClient : public std::enable_shared_from_this<HttpClient> {
public:
    HttpClient(net::io_context& ioc, ssl::context& ssl_ctx, const std::string& host, 
              const std::string& port = "443", bool use_ssl = true);
    ~HttpClient();
    
    // Synchronous HTTP request
    HttpResponse request(const HttpRequest& req);
    
    // Asynchronous HTTP request
    void async_request(const HttpRequest& req, HttpResponseCallback callback);
    
    // Connection management
    bool connect();
    void disconnect();
    bool is_connected() const;
    
    // Configuration
    void set_user_agent(const std::string& user_agent);
    void set_default_headers(const std::unordered_map<std::string, std::string>& headers);
    void set_rate_limiter(std::unique_ptr<RateLimiter> limiter);
    
    // Statistics
    size_t get_total_requests() const;
    size_t get_successful_requests() const;
    size_t get_failed_requests() const;
    std::chrono::milliseconds get_average_latency() const;
    
private:
    struct Implementation;
    std::unique_ptr<Implementation> impl_;
    
    void on_resolve(beast::error_code ec, tcp::resolver::results_type results,
                   std::shared_ptr<http::request<http::string_body>> req,
                   HttpResponseCallback callback);
    
    void on_connect(beast::error_code ec, tcp::resolver::results_type::endpoint_type,
                   std::shared_ptr<http::request<http::string_body>> req,
                   HttpResponseCallback callback);
    
    void on_handshake(beast::error_code ec,
                     std::shared_ptr<http::request<http::string_body>> req,
                     HttpResponseCallback callback);
    
    void on_write(beast::error_code ec, std::size_t bytes_transferred,
                 std::shared_ptr<http::request<http::string_body>> req,
                 HttpResponseCallback callback);
    
    void on_read(beast::error_code ec, std::size_t bytes_transferred,
                std::shared_ptr<http::response<http::string_body>> res,
                HttpResponseCallback callback);
};

// HTTP client pool for managing multiple connections
class HttpClientPool {
public:
    HttpClientPool(net::io_context& ioc, ssl::context& ssl_ctx, 
                  const std::string& host, const std::string& port = "443", 
                  bool use_ssl = true, size_t pool_size = 5);
    
    std::shared_ptr<HttpClient> get_client();
    void return_client(std::shared_ptr<HttpClient> client);
    
    // Pool-wide request method
    void async_request(const HttpRequest& req, HttpResponseCallback callback);
    
    size_t get_pool_size() const;
    size_t get_available_clients() const;
    size_t get_busy_clients() const;
    
private:
    net::io_context& ioc_;
    ssl::context& ssl_ctx_;
    std::string host_;
    std::string port_;
    bool use_ssl_;
    
    std::queue<std::shared_ptr<HttpClient>> available_clients_;
    std::set<std::shared_ptr<HttpClient>> busy_clients_;
    std::mutex pool_mutex_;
    
    void create_client();
};

// Utility functions for common HTTP operations
namespace http_utils {
    
    // URL encoding/decoding
    std::string url_encode(const std::string& str);
    std::string url_decode(const std::string& str);
    
    // Query string building
    std::string build_query_string(const std::unordered_map<std::string, std::string>& params);
    
    // Header parsing
    std::unordered_map<std::string, std::string> parse_headers(const std::string& header_string);
    
    // Common headers
    std::unordered_map<std::string, std::string> default_headers();
    std::unordered_map<std::string, std::string> json_headers();
    
    // Response validation
    bool is_success_status(int status_code);
    bool is_client_error(int status_code);
    bool is_server_error(int status_code);
    
    // Error handling
    std::string get_status_message(int status_code);
    std::string format_error(const std::string& operation, const beast::error_code& ec);
}

} // namespace price_collector
} // namespace ats