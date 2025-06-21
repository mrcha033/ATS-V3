#pragma once

#include <string>
#include <memory>
#include <functional>
#include <unordered_map>
#include <mutex>

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
    
    // Statistics
    mutable std::mutex stats_mutex_;
    long long total_requests_;
    long long successful_requests_;
    long long failed_requests_;
    double average_response_time_ms_;
    
public:
    RestClient();
    ~RestClient();
    
    // Lifecycle
    bool Initialize();
    void Cleanup();
    
    // Configuration
    void SetUserAgent(const std::string& user_agent);
    void SetDefaultTimeout(long timeout_ms);
    void SetSslVerification(bool verify);
    
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
    
    // Health check
    bool IsHealthy() const;
    std::string GetLastError() const;
    
private:
    // CURL callback functions
    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp);
    static size_t HeaderCallback(char* buffer, size_t size, size_t nitems, 
                                std::unordered_map<std::string, std::string>* headers);
    
    // Helper methods
    void SetCommonOptions(CURL* curl, const HttpRequest& request);
    void UpdateStatistics(const HttpResponse& response);
    std::string BuildQueryString(const std::unordered_map<std::string, std::string>& params);
    std::string UrlEncode(const std::string& str);
    
    mutable std::string last_error_;
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