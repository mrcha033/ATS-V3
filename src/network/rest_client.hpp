#pragma once

#include <string>
#include <memory>
#include <functional>
#include <unordered_map>
#include <mutex>
#include <vector>
#include <queue>
#include <condition_variable>
#include <atomic>
#include <map>

#ifdef HAVE_CURL
#include <curl/curl.h>
#else
// Stub types when CURL is not available
typedef void CURL;
#endif

namespace ats {

struct HttpResponse {
    long status_code;
    std::string body;
    std::unordered_map<std::string, std::string> headers;
    long response_time_ms;
    std::string error_message;
    
    bool IsSuccess() const { return status_code >= 200 && status_code < 300; }
    bool IsClientError() const { return status_code >= 400 && status_code < 500; }
    bool IsServerError() const { return status_code >= 500; }
};

struct HttpRequest {
    std::string url;
    std::string method = "GET";
    std::unordered_map<std::string, std::string> headers;
    std::string body;
    long timeout_ms = 5000;
    bool follow_redirects = true;
    
    // For authenticated requests
    std::string api_key;
    std::string signature;
};

class RestClient {
private:
    CURL* curl_handle_;
    std::mutex curl_mutex_;
    
    // Configuration
    std::string user_agent_;
    long default_timeout_ms_;
    bool verify_ssl_;
    std::string base_url_;
    std::unordered_map<std::string, std::string> headers_;
    bool follow_redirects_;
    int max_redirects_;
    long connect_timeout_ms_;
    
    // Thread pool for async operations
    std::vector<std::thread> thread_pool_;
    std::queue<std::function<void()>> task_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::atomic<bool> pool_running_;
    size_t max_pool_size_;
    
    // Statistics
    mutable std::mutex stats_mutex_;
    long long total_requests_;
    long long successful_requests_;
    long long failed_requests_;
    double average_response_time_ms_;
    
    // Additional configuration
    mutable std::string last_error_;
    
public:
    RestClient();
    ~RestClient();
    
    // Note: CURL global initialization is handled by RestClientManager
    
    // Configuration
    void SetUserAgent(const std::string& user_agent);
    void SetDefaultTimeout(long timeout_ms);
    void SetSslVerification(bool verify);
    void SetConnectTimeout(long timeout_ms);
    void SetBaseUrl(const std::string& base_url);
    void AddHeader(const std::string& key, const std::string& value);
    void RemoveHeader(const std::string& key);
    void ClearHeaders();
    void SetFollowRedirects(bool follow, int max_redirects = 5);
    
    // Thread pool management
    void StartThreadPool(size_t pool_size = 4);
    void StopThreadPool();
    
    // HTTP Methods
    HttpResponse Get(const std::string& url, 
                    const std::unordered_map<std::string, std::string>& headers = {});
    
    HttpResponse Post(const std::string& url, 
                     const std::string& body,
                     const std::unordered_map<std::string, std::string>& headers = {});
    
    HttpResponse Put(const std::string& url, 
                    const std::string& body,
                    const std::unordered_map<std::string, std::string>& headers = {});
    
    HttpResponse Delete(const std::string& url,
                       const std::unordered_map<std::string, std::string>& headers = {});
    
    // Advanced request method
    HttpResponse Request(const HttpRequest& request);
    
    // Async methods
    using ResponseCallback = std::function<void(const HttpResponse&)>;
    void GetAsync(const std::string& url, ResponseCallback callback,
                 const std::unordered_map<std::string, std::string>& headers = {});
    
    // Statistics
    long long GetTotalRequests() const;
    long long GetSuccessfulRequests() const;
    long long GetFailedRequests() const;
    double GetAverageResponseTime() const;
    double GetSuccessRate() const;
    double GetErrorRate() const;
    
    // Health check
    bool IsHealthy() const;
    std::string GetLastError() const;
    
    // Utility methods
    std::string BuildUrl(const std::string& endpoint, const std::map<std::string, std::string>& params = {});
    std::string BuildPostData(const std::map<std::string, std::string>& data);
    std::string BuildQueryString(const std::unordered_map<std::string, std::string>& params);
    
private:
    // CURL callback functions
    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp);
    static size_t HeaderCallback(char* buffer, size_t size, size_t nitems, 
                                std::map<std::string, std::string>* headers);
    
    // Helper methods
    void UpdateStatistics(const HttpResponse& response);
    std::string UrlEncode(const std::string& str);
};

// Global instance for convenience
class RestClientManager {
private:
    static std::unique_ptr<RestClient> instance_;
    static std::mutex instance_mutex_;
    
public:
    static RestClient& Instance();
    static void Initialize();
    static void Cleanup();
};

} // namespace ats 