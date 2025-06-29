#include "rest_client.hpp"
#include "network_exception.hpp"
#include "../utils/logger.hpp"
#include <chrono>
#include <thread>
#include <sstream>
#include <iomanip>
#include <algorithm>

#ifdef HAVE_CURL
#include <curl/curl.h>
#endif

namespace ats {

// CURL callback functions - defined as class static methods

RestClient::RestClient()
    : user_agent_("ATS-V3/1.0")
    , default_timeout_ms_(5000)
    , verify_ssl_(true)
    , pool_running_(false)
    , max_pool_size_(4)
    , total_requests_(0)
    , successful_requests_(0)
    , failed_requests_(0)
    , average_response_time_ms_(0.0) {
    
    StartThreadPool();
}

RestClient::~RestClient() {
    StopThreadPool();
}

void RestClient::StartThreadPool(size_t pool_size) {
    if (pool_running_.load()) {
        return;
    }
    
    max_pool_size_ = pool_size;
    pool_running_ = true;
    
    for (size_t i = 0; i < pool_size; ++i) {
        thread_pool_.emplace_back([this] {
            while (pool_running_.load()) {
                std::function<void()> task;
                
                {
                    std::unique_lock<std::mutex> lock(queue_mutex_);
                    queue_cv_.wait(lock, [this] {
                        return !task_queue_.empty() || !pool_running_.load();
                    });
                    
                    if (!pool_running_.load()) {
                        break;
                    }
                    
                    if (!task_queue_.empty()) {
                        task = std::move(task_queue_.front());
                        task_queue_.pop();
                    }
                }
                
                if (task) {
                    try {
                        task();
                    } catch (const std::exception& e) {
                        LOG_ERROR("Exception in thread pool task: {}", e.what());
                    }
                }
            }
        });
    }
    
    LOG_DEBUG("Started thread pool with {} threads", pool_size);
}

void RestClient::StopThreadPool() {
    if (!pool_running_.load()) {
        return;
    }
    
    pool_running_ = false;
    queue_cv_.notify_all();
    
    for (auto& thread : thread_pool_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    
    thread_pool_.clear();
    LOG_DEBUG("Stopped thread pool");
}

void RestClient::GetAsync(const std::string& url, ResponseCallback callback,
                         const std::unordered_map<std::string, std::string>& headers) {
    if (!pool_running_.load()) {
        StartThreadPool();
    }
    
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        task_queue_.emplace([this, url, callback, headers] {
            try {
                auto response = Get(url, headers);
                if (callback) {
                    callback(response);
                }
            } catch (const std::exception& e) {
                LOG_ERROR("Error in async GET request: {}", e.what());
                if (callback) {
                    HttpResponse error_response;
                    error_response.status_code = 0;
                    error_response.error_message = e.what();
                    callback(error_response);
                }
            }
        });
    }
    queue_cv_.notify_one();
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
    auto start_time = std::chrono::steady_clock::now();
    total_requests_.fetch_add(1);
    
    HttpResponse response;
    response.status_code = 0;
    response.response_time_ms = 0;
    
#ifdef HAVE_CURL
    CURL* curl = curl_easy_init();
    if (!curl) {
        failed_requests_.fetch_add(1);
        response.error_message = "Failed to initialize CURL handle";
        LOG_ERROR("Failed to initialize CURL handle");
        return response;
    }
    
    try {
        // Set URL
        curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
        
        // Set method and body
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
        
        // Set callbacks - Fixed: Use response.body instead of request
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, RestClient::WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, RestClient::HeaderCallback);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response.headers);
        
        // Set timeouts
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, request.timeout_ms);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, connect_timeout_ms_);
        
        // Set user agent
        curl_easy_setopt(curl, CURLOPT_USERAGENT, user_agent_.c_str());
        
        // Set SSL verification
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, verify_ssl_ ? 1L : 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, verify_ssl_ ? 2L : 0L);
        
        // Set redirect options
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, request.follow_redirects ? 1L : 0L);
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
        
        // Set headers - Using RAII for automatic cleanup
        struct curl_slist* header_list = nullptr;
        auto cleanup_header_list = [&header_list]() {
            if (header_list) {
                curl_slist_free_all(header_list);
                header_list = nullptr;
            }
        };
        
        CURLcode result = CURLE_OK;
        
        try {
            for (const auto& header : request.headers) {
                std::string header_str = header.first + ": " + header.second;
                header_list = curl_slist_append(header_list, header_str.c_str());
            }
            
            if (header_list) {
                curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
            }
            
            // Perform request
            result = curl_easy_perform(curl);
            
            // Clean up header list
            cleanup_header_list();
            
        } catch (...) {
            // Ensure cleanup even if exception occurs
            cleanup_header_list();
            throw;
        }
        
        // Get response code
        long response_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
        response.status_code = response_code;
        
        // Clean up
        curl_easy_cleanup(curl);
        
        // Check for errors
        if (result != CURLE_OK) {
            failed_requests_.fetch_add(1);
            std::string error_message = curl_easy_strerror(result);
            LOG_ERROR("CURL request failed: {} ({})", error_message, result);
            if (result == CURLE_OPERATION_TIMEDOUT) {
                throw TimeoutException("Request timed out: " + error_message);
            } else {
                throw ConnectionException("Request failed: " + error_message);
            }
        } else {
            successful_requests_.fetch_add(1);
            if (response_code >= 400) {
                LOG_WARNING("HTTP error {}: {}", response_code, request.url);
            }
        }
        
        LOG_DEBUG("HTTP {} {} -> {} ({} bytes)", request.method, request.url, 
                 response_code, response.body.length());
        
    } catch (const std::exception& e) {
        failed_requests_.fetch_add(1);
        response.error_message = e.what();
        LOG_ERROR("Exception during HTTP request: {}", e.what());
        curl_easy_cleanup(curl);
    }
    
#else
    // Fallback implementation without libcurl
    failed_requests_.fetch_add(1);
    response.error_message = "libcurl not available";
    LOG_ERROR("libcurl not available - cannot perform HTTP request to: {}", request.url);
#endif
    
    // Update statistics
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    response.response_time_ms = duration.count();
    UpdateStatistics(response);
    
    return response;
}

void RestClient::SetBaseUrl(const std::string& base_url) {
    base_url_ = base_url;
    // Remove trailing slash
    if (!base_url_.empty() && base_url_.back() == '/') {
        base_url_.pop_back();
    }
}

void RestClient::AddHeader(const std::string& key, const std::string& value) {
    headers_[key] = value;
}

void RestClient::RemoveHeader(const std::string& key) {
    headers_.erase(key);
}

void RestClient::ClearHeaders() {
    headers_.clear();
}

void RestClient::SetDefaultTimeout(long timeout_ms) {
    default_timeout_ms_ = timeout_ms;
}

void RestClient::SetUserAgent(const std::string& user_agent) {
    user_agent_ = user_agent;
}

void RestClient::SetSslVerification(bool verify) {
    verify_ssl_ = verify;
}

void RestClient::SetFollowRedirects(bool follow, int max_redirects) {
    follow_redirects_ = follow;
    max_redirects_ = max_redirects;
}

void RestClient::SetConnectTimeout(long timeout_ms) {
    connect_timeout_ms_ = timeout_ms;
}



double RestClient::GetErrorRate() const {
    long long total = total_requests_.load();
    if (total == 0) return 0.0;
    return static_cast<double>(failed_requests_.load()) / total * 100.0;
}

void RestClient::ResetStatistics() {
    total_requests_.store(0);
    successful_requests_.store(0);
    failed_requests_.store(0);
    std::lock_guard<std::mutex> lock(stats_mutex_);
    average_response_time_ms_ = 0.0;
}

void RestClient::LogStatistics() const {
    LOG_INFO("=== RestClient Statistics ===");
    LOG_INFO("Total requests: {}", total_requests_.load());
    LOG_INFO("Successful requests: {}", successful_requests_.load());
    LOG_INFO("Failed requests: {}", failed_requests_.load());
    LOG_INFO("Error rate: {:.2f}%", GetErrorRate());
    LOG_INFO("Average response time: {:.2f} ms", GetAverageResponseTime());
    LOG_INFO("Base URL: {}", base_url_);
}

std::string RestClient::BuildUrl(const std::string& endpoint, const std::map<std::string, std::string>& params) {
    std::string url = base_url_;
    
    // Add endpoint
    if (!endpoint.empty()) {
        if (endpoint[0] != '/') {
            url += "/";
        }
        url += endpoint;
    }
    
    // Add query parameters
    if (!params.empty()) {
        url += "?";
        bool first = true;
        for (const auto& pair : params) {
            if (!first) {
                url += "&";
            }
            url += UrlEncode(pair.first) + "=" + UrlEncode(pair.second);
            first = false;
        }
    }
    
    return url;
}

std::string RestClient::BuildPostData(const std::map<std::string, std::string>& data) {
    std::ostringstream oss;
    bool first = true;
    
    for (const auto& pair : data) {
        if (!first) {
            oss << "&";
        }
        oss << UrlEncode(pair.first) << "=" << UrlEncode(pair.second);
        first = false;
    }
    
    return oss.str();
}

std::string RestClient::BuildQueryString(const std::unordered_map<std::string, std::string>& params) {
    if (params.empty()) {
        return "";
    }
    
    std::ostringstream oss;
    bool first = true;
    
    for (const auto& pair : params) {
        if (!first) {
            oss << "&";
        }
        oss << UrlEncode(pair.first) << "=" << UrlEncode(pair.second);
        first = false;
    }
    
    return oss.str();
}

std::string RestClient::UrlEncode(const std::string& value) {
#ifdef HAVE_CURL
    CURL* curl = curl_easy_init();
    if (curl) {
        char* encoded = curl_easy_escape(curl, value.c_str(), value.length());
        if (encoded) {
            std::string result(encoded);
            curl_free(encoded);
            curl_easy_cleanup(curl);
            return result;
        }
        curl_easy_cleanup(curl);
    }
#endif
    
    // Fallback URL encoding
    std::ostringstream oss;
    for (char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            oss << c;
        } else {
            oss << '%' << std::hex << std::uppercase << static_cast<unsigned char>(c);
        }
    }
    return oss.str();
}



void RestClient::UpdateStatistics(const HttpResponse& response) {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    
    // Update average response time using exponential moving average
    if (total_requests_.load() == 1) {
        average_response_time_ms_ = response.response_time_ms;
    } else {
        const double alpha = 0.1; // Smoothing factor
        average_response_time_ms_ = alpha * response.response_time_ms + 
                                   (1.0 - alpha) * average_response_time_ms_;
    }
}

long long RestClient::GetTotalRequests() const {
    return total_requests_.load();
}

long long RestClient::GetSuccessfulRequests() const {
    return successful_requests_.load();
}

long long RestClient::GetFailedRequests() const {
    return failed_requests_.load();
}

double RestClient::GetAverageResponseTime() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return average_response_time_ms_;
}

double RestClient::GetSuccessRate() const {
    long long total = total_requests_.load();
    if (total == 0) return 0.0;
    return static_cast<double>(successful_requests_.load()) / total * 100.0;
}

bool RestClient::IsHealthy() const {
    // Consider healthy if success rate > 90% and we have some requests
    return GetSuccessRate() > 90.0 && GetTotalRequests() > 0;
}

std::string RestClient::GetLastError() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return last_error_;
}

// RestClientManager implementation
std::unique_ptr<RestClient> RestClientManager::instance_ = nullptr;
std::mutex RestClientManager::instance_mutex_;

// CURL global initialization management
class CurlGlobalManager {
private:
    static std::atomic<bool> initialized_;
    static std::mutex init_mutex_;
    static std::atomic<int> ref_count_;

public:
    static bool Initialize() {
        std::lock_guard<std::mutex> lock(init_mutex_);
        if (!initialized_.load()) {
#ifdef HAVE_CURL
            CURLcode result = curl_global_init(CURL_GLOBAL_DEFAULT);
            if (result != CURLE_OK) {
                LOG_ERROR("Failed to initialize libcurl: {}", curl_easy_strerror(result));
                return false;
            }
            initialized_.store(true);
            LOG_DEBUG("libcurl initialized successfully");
#endif
        }
        ref_count_.fetch_add(1);
        return true;
    }
    
    static void Cleanup() {
        std::lock_guard<std::mutex> lock(init_mutex_);
        if (ref_count_.fetch_sub(1) == 1 && initialized_.load()) {
#ifdef HAVE_CURL
            curl_global_cleanup();
            initialized_.store(false);
            LOG_DEBUG("libcurl cleaned up");
#endif
        }
    }
};

std::atomic<bool> CurlGlobalManager::initialized_{false};
std::mutex CurlGlobalManager::init_mutex_;
std::atomic<int> CurlGlobalManager::ref_count_{0};

RestClient& RestClientManager::Instance() {
    std::lock_guard<std::mutex> lock(instance_mutex_);
    if (!instance_) {
        if (CurlGlobalManager::Initialize()) {
            instance_ = std::make_unique<RestClient>();
        } else {
            LOG_ERROR("Failed to initialize CURL global state");
            throw std::runtime_error("CURL initialization failed");
        }
    }
    return *instance_;
}

void RestClientManager::Initialize() {
    std::lock_guard<std::mutex> lock(instance_mutex_);
    if (!instance_) {
        if (CurlGlobalManager::Initialize()) {
            instance_ = std::make_unique<RestClient>();
        } else {
            LOG_ERROR("Failed to initialize CURL global state");
        }
    }
}

void RestClientManager::Cleanup() {
    std::lock_guard<std::mutex> lock(instance_mutex_);
    if (instance_) {
        instance_.reset();
        CurlGlobalManager::Cleanup();
    }
}

// RestClient static callback methods
size_t RestClient::WriteCallback(void* contents, size_t size, size_t nmemb, std::string* data) {
    size_t total_size = size * nmemb;
    data->append((char*)contents, total_size);
    return total_size;
}

size_t RestClient::HeaderCallback(char* buffer, size_t size, size_t nitems, 
                                 std::map<std::string, std::string>* headers) {
    size_t total_size = size * nitems;
    std::string header(buffer, total_size);
    
    size_t pos = header.find(':');
    if (pos != std::string::npos) {
        std::string key = header.substr(0, pos);
        std::string value = header.substr(pos + 1);
        
        // Trim whitespace
        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t\r\n") + 1);
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t\r\n") + 1);
        
        (*headers)[key] = value;
    }
    
    return total_size;
}

} // namespace ats 
