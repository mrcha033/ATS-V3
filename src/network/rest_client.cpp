#include "rest_client.hpp"
#include "../utils/logger.hpp"
#include <chrono>
#include <thread>
#include <sstream>

namespace ats {

#ifdef HAVE_CURL
// CURL-based implementation
RestClient::RestClient() 
    : curl_handle_(nullptr), 
      user_agent_("ATS-V3/1.0"),
      default_timeout_ms_(5000),
      verify_ssl_(true),
      total_requests_(0),
      successful_requests_(0),
      failed_requests_(0),
      average_response_time_ms_(0.0) {
}

RestClient::~RestClient() {
    Cleanup();
}

bool RestClient::Initialize() {
    std::lock_guard<std::mutex> lock(curl_mutex_);
    
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        LOG_ERROR("Failed to initialize libcurl");
        return false;
    }
    
    curl_handle_ = curl_easy_init();
    if (!curl_handle_) {
        LOG_ERROR("Failed to create CURL handle");
        return false;
    }
    
    LOG_INFO("RestClient initialized with libcurl");
    return true;
}

void RestClient::Cleanup() {
    std::lock_guard<std::mutex> lock(curl_mutex_);
    
    if (curl_handle_) {
        curl_easy_cleanup(curl_handle_);
        curl_handle_ = nullptr;
    }
}

void RestClient::SetUserAgent(const std::string& user_agent) {
    user_agent_ = user_agent;
}

void RestClient::SetDefaultTimeout(long timeout_ms) {
    default_timeout_ms_ = timeout_ms;
}

void RestClient::SetSslVerification(bool verify) {
    verify_ssl_ = verify;
}

HttpResponse RestClient::Get(const std::string& url, 
                           const std::unordered_map<std::string, std::string>& headers) {
    HttpRequest request;
    request.url = url;
    request.method = "GET";
    request.headers = headers;
    request.timeout_ms = default_timeout_ms_;
    
    return Request(request);
}

HttpResponse RestClient::Post(const std::string& url, 
                            const std::string& body,
                            const std::unordered_map<std::string, std::string>& headers) {
    HttpRequest request;
    request.url = url;
    request.method = "POST";
    request.body = body;
    request.headers = headers;
    request.timeout_ms = default_timeout_ms_;
    
    return Request(request);
}

HttpResponse RestClient::Put(const std::string& url, 
                           const std::string& body,
                           const std::unordered_map<std::string, std::string>& headers) {
    HttpRequest request;
    request.url = url;
    request.method = "PUT";
    request.body = body;
    request.headers = headers;
    request.timeout_ms = default_timeout_ms_;
    
    return Request(request);
}

HttpResponse RestClient::Delete(const std::string& url,
                              const std::unordered_map<std::string, std::string>& headers) {
    HttpRequest request;
    request.url = url;
    request.method = "DELETE";
    request.headers = headers;
    request.timeout_ms = default_timeout_ms_;
    
    return Request(request);
}

HttpResponse RestClient::Request(const HttpRequest& request) {
    std::lock_guard<std::mutex> lock(curl_mutex_);
    
    HttpResponse response;
    auto start_time = std::chrono::steady_clock::now();
    
    if (!curl_handle_) {
        response.status_code = 0;
        response.error_message = "CURL handle not initialized";
        last_error_ = response.error_message;
        UpdateStatistics(response);
        return response;
    }
    
    try {
        // Reset curl handle
        curl_easy_reset(curl_handle_);
        
        // Set common options
        SetCommonOptions(curl_handle_, request);
        
        // Set callback for response body
        curl_easy_setopt(curl_handle_, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl_handle_, CURLOPT_WRITEDATA, &response.body);
        
        // Set callback for headers
        curl_easy_setopt(curl_handle_, CURLOPT_HEADERFUNCTION, HeaderCallback);
        curl_easy_setopt(curl_handle_, CURLOPT_HEADERDATA, &response.headers);
        
        // Set headers
        struct curl_slist* headers = nullptr;
        for (const auto& header : request.headers) {
            std::string header_str = header.first + ": " + header.second;
            headers = curl_slist_append(headers, header_str.c_str());
        }
        
        if (headers) {
            curl_easy_setopt(curl_handle_, CURLOPT_HTTPHEADER, headers);
        }
        
        // Perform request
        CURLcode res = curl_easy_perform(curl_handle_);
        
        // Clean up headers
        if (headers) {
            curl_slist_free_all(headers);
        }
        
        // Get response info
        if (res == CURLE_OK) {
            curl_easy_getinfo(curl_handle_, CURLINFO_RESPONSE_CODE, &response.status_code);
        } else {
            response.status_code = 0;
            response.error_message = curl_easy_strerror(res);
            last_error_ = response.error_message;
        }
        
    } catch (const std::exception& e) {
        response.status_code = 0;
        response.error_message = "Exception during request: " + std::string(e.what());
        last_error_ = response.error_message;
    }
    
    // Calculate response time
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    response.response_time_ms = duration.count();
    
    UpdateStatistics(response);
    
    return response;
}

void RestClient::GetAsync(const std::string& url, ResponseCallback callback,
                         const std::unordered_map<std::string, std::string>& headers) {
    // Simple async implementation using thread
    // For production, consider using a thread pool
    std::thread([this, url, headers, callback]() {
        auto response = Get(url, headers);
        callback(response);
    }).detach();
}

// Static callback functions
size_t RestClient::WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    size_t total_size = size * nmemb;
    userp->append(static_cast<char*>(contents), total_size);
    return total_size;
}

size_t RestClient::HeaderCallback(char* buffer, size_t size, size_t nitems, 
                                 std::unordered_map<std::string, std::string>* headers) {
    size_t total_size = size * nitems;
    std::string header(buffer, total_size);
    
    // Parse header (format: "Key: Value\r\n")
    size_t colon_pos = header.find(':');
    if (colon_pos != std::string::npos) {
        std::string key = header.substr(0, colon_pos);
        std::string value = header.substr(colon_pos + 1);
        
        // Trim whitespace
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t\r\n") + 1);
        
        (*headers)[key] = value;
    }
    
    return total_size;
}

void RestClient::SetCommonOptions(CURL* curl, const HttpRequest& request) {
    // Basic options
    curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, user_agent_.c_str());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, request.timeout_ms);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, request.timeout_ms / 2);
    
    // SSL options
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, verify_ssl_ ? 1L : 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, verify_ssl_ ? 2L : 0L);
    
    // Follow redirects
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, request.follow_redirects ? 1L : 0L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    
    // Set HTTP method and body
    if (request.method == "POST") {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        if (!request.body.empty()) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, request.body.length());
        }
    } else if (request.method == "PUT") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
        if (!request.body.empty()) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, request.body.length());
        }
    } else if (request.method == "DELETE") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    }
    
    // Performance optimizations for Raspberry Pi
    curl_easy_setopt(curl, CURLOPT_TCP_NODELAY, 1L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE, 60L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL, 30L);
}

void RestClient::UpdateStatistics(const HttpResponse& response) {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    
    total_requests_++;
    
    if (response.IsSuccess()) {
        successful_requests_++;
    } else {
        failed_requests_++;
    }
    
    // Update average response time (simple moving average)
    average_response_time_ms_ = ((average_response_time_ms_ * (total_requests_ - 1)) + 
                                response.response_time_ms) / total_requests_;
}

std::string RestClient::BuildQueryString(const std::unordered_map<std::string, std::string>& params) {
    if (params.empty()) {
        return "";
    }
    
    std::ostringstream query;
    bool first = true;
    
    for (const auto& param : params) {
        if (!first) {
            query << "&";
        }
        
        // URL encode the key and value
        query << param.first << "=" << param.second;
        first = false;
    }
    
    return query.str();
}

// Statistics getters
long long RestClient::GetTotalRequests() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return total_requests_;
}

long long RestClient::GetSuccessfulRequests() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return successful_requests_;
}

long long RestClient::GetFailedRequests() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return failed_requests_;
}

double RestClient::GetAverageResponseTime() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return average_response_time_ms_;
}

double RestClient::GetSuccessRate() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    if (total_requests_ == 0) return 0.0;
    return static_cast<double>(successful_requests_) / total_requests_ * 100.0;
}

bool RestClient::IsHealthy() const {
    return curl_handle_ != nullptr && GetSuccessRate() > 50.0;
}

std::string RestClient::GetLastError() const {
    return last_error_;
}

#else
// Stub implementation when CURL is not available
RestClient::RestClient() 
    : curl_handle_(nullptr), 
      user_agent_("ATS-V3/1.0"),
      default_timeout_ms_(5000),
      verify_ssl_(true),
      total_requests_(0),
      successful_requests_(0),
      failed_requests_(0),
      average_response_time_ms_(0.0) {
}

RestClient::~RestClient() {
    Cleanup();
}

bool RestClient::Initialize() {
    LOG_WARNING("RestClient stub: CURL not available, HTTP requests will be simulated");
    return true;
}

void RestClient::Cleanup() {
    // Nothing to cleanup in stub implementation
}

void RestClient::SetUserAgent(const std::string& user_agent) {
    user_agent_ = user_agent;
}

void RestClient::SetDefaultTimeout(long timeout_ms) {
    default_timeout_ms_ = timeout_ms;
}

void RestClient::SetSslVerification(bool verify) {
    verify_ssl_ = verify;
}

HttpResponse RestClient::Get(const std::string& url, 
                           const std::unordered_map<std::string, std::string>& headers) {
    LOG_DEBUG("Stub GET request to: {}", url);
    
    HttpResponse response;
    response.status_code = 200;
    response.body = R"({"status":"stub_response","message":"CURL not available"})";
    response.response_time_ms = 100;
    
    UpdateStatistics(response);
    return response;
}

HttpResponse RestClient::Post(const std::string& url, 
                            const std::string& body,
                            const std::unordered_map<std::string, std::string>& headers) {
    LOG_DEBUG("Stub POST request to: {}", url);
    
    HttpResponse response;
    response.status_code = 200;
    response.body = R"({"status":"stub_response","message":"CURL not available"})";
    response.response_time_ms = 100;
    
    UpdateStatistics(response);
    return response;
}

HttpResponse RestClient::Put(const std::string& url, 
                           const std::string& body,
                           const std::unordered_map<std::string, std::string>& headers) {
    LOG_DEBUG("Stub PUT request to: {}", url);
    
    HttpResponse response;
    response.status_code = 200;
    response.body = R"({"status":"stub_response","message":"CURL not available"})";
    response.response_time_ms = 100;
    
    UpdateStatistics(response);
    return response;
}

HttpResponse RestClient::Delete(const std::string& url,
                              const std::unordered_map<std::string, std::string>& headers) {
    LOG_DEBUG("Stub DELETE request to: {}", url);
    
    HttpResponse response;
    response.status_code = 200;
    response.body = R"({"status":"stub_response","message":"CURL not available"})";
    response.response_time_ms = 100;
    
    UpdateStatistics(response);
    return response;
}

HttpResponse RestClient::Request(const HttpRequest& request) {
    LOG_DEBUG("Stub {} request to: {}", request.method, request.url);
    
    HttpResponse response;
    response.status_code = 200;
    response.body = R"({"status":"stub_response","message":"CURL not available"})";
    response.response_time_ms = 100;
    
    UpdateStatistics(response);
    return response;
}

void RestClient::GetAsync(const std::string& url, ResponseCallback callback,
                         const std::unordered_map<std::string, std::string>& headers) {
    std::thread([this, url, callback, headers]() {
        auto response = Get(url, headers);
        callback(response);
    }).detach();
}

void RestClient::UpdateStatistics(const HttpResponse& response) {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    
    total_requests_++;
    
    if (response.IsSuccess()) {
        successful_requests_++;
    } else {
        failed_requests_++;
    }
    
    // Update average response time (simple moving average)
    average_response_time_ms_ = ((average_response_time_ms_ * (total_requests_ - 1)) + 
                                response.response_time_ms) / total_requests_;
}

#endif // HAVE_CURL

// Common implementation (works with both CURL and stub)
long long RestClient::GetTotalRequests() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return total_requests_;
}

long long RestClient::GetSuccessfulRequests() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return successful_requests_;
}

long long RestClient::GetFailedRequests() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return failed_requests_;
}

double RestClient::GetAverageResponseTime() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return average_response_time_ms_;
}

double RestClient::GetSuccessRate() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    if (total_requests_ == 0) return 0.0;
    return static_cast<double>(successful_requests_) / total_requests_ * 100.0;
}

bool RestClient::IsHealthy() const {
    return last_error_.empty() && GetSuccessRate() > 50.0;
}

std::string RestClient::GetLastError() const {
    return last_error_;
}



// RestClientManager implementation
std::unique_ptr<RestClient> RestClientManager::instance_ = nullptr;
std::mutex RestClientManager::instance_mutex_;

RestClient& RestClientManager::Instance() {
    std::lock_guard<std::mutex> lock(instance_mutex_);
    if (!instance_) {
        instance_ = std::make_unique<RestClient>();
        instance_->Initialize();
    }
    return *instance_;
}

void RestClientManager::Initialize() {
    std::lock_guard<std::mutex> lock(instance_mutex_);
    if (!instance_) {
        instance_ = std::make_unique<RestClient>();
        instance_->Initialize();
    }
}

void RestClientManager::Cleanup() {
    std::lock_guard<std::mutex> lock(instance_mutex_);
    if (instance_) {
        instance_->Cleanup();
        instance_.reset();
    }
}

} // namespace ats 