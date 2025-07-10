#pragma once

#include <string>
#include <map>

namespace ats {

// Simple HTTP client fallback for when libcurl is not available
class FallbackHttpClient {
public:
    struct HttpResponse {
        int status_code = 0;
        std::string body;
        std::map<std::string, std::string> headers;
        bool success = false;
        std::string error_message;
    };

    // Simple GET request
    static HttpResponse get(const std::string& url, 
                           const std::map<std::string, std::string>& headers = {});
    
    // Simple POST request
    static HttpResponse post(const std::string& url, 
                            const std::string& data,
                            const std::map<std::string, std::string>& headers = {});

private:
    static std::string url_encode(const std::string& value);
    static std::map<std::string, std::string> parse_url(const std::string& url);
    
#ifdef _WIN32
    static HttpResponse windows_http_request(const std::string& method,
                                           const std::string& url,
                                           const std::string& data,
                                           const std::map<std::string, std::string>& headers);
#else
    static HttpResponse unix_http_request(const std::string& method,
                                        const std::string& url,
                                        const std::string& data,
                                        const std::map<std::string, std::string>& headers);
#endif
};

} // namespace ats