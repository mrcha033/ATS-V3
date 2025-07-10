#include "fallback_http.hpp"
#include <sstream>
#include <iostream>
#include <iomanip>
#include <cstring>

#ifdef _WIN32
    #include <windows.h>
    #include <winhttp.h>
    #pragma comment(lib, "winhttp.lib")
#else
    #include <sys/socket.h>
    #include <netdb.h>
    #include <unistd.h>
    #include <arpa/inet.h>
#endif

namespace ats {

FallbackHttpClient::HttpResponse FallbackHttpClient::get(const std::string& url, 
                                                         const std::map<std::string, std::string>& headers) {
#ifdef _WIN32
    return windows_http_request("GET", url, "", headers);
#else
    return unix_http_request("GET", url, "", headers);
#endif
}

FallbackHttpClient::HttpResponse FallbackHttpClient::post(const std::string& url, 
                                                          const std::string& data,
                                                          const std::map<std::string, std::string>& headers) {
#ifdef _WIN32
    return windows_http_request("POST", url, data, headers);
#else
    return unix_http_request("POST", url, data, headers);
#endif
}

std::string FallbackHttpClient::url_encode(const std::string& value) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;

    for (char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            escaped << std::uppercase;
            escaped << '%' << std::setw(2) << int(static_cast<unsigned char>(c));
            escaped << std::nouppercase;
        }
    }

    return escaped.str();
}

std::map<std::string, std::string> FallbackHttpClient::parse_url(const std::string& url) {
    std::map<std::string, std::string> result;
    
    // Simple URL parsing (just for basic HTTP/HTTPS)
    size_t protocol_end = url.find("://");
    if (protocol_end == std::string::npos) {
        result["error"] = "Invalid URL format";
        return result;
    }
    
    result["protocol"] = url.substr(0, protocol_end);
    size_t host_start = protocol_end + 3;
    
    size_t path_start = url.find('/', host_start);
    if (path_start == std::string::npos) {
        path_start = url.length();
        result["path"] = "/";
    } else {
        result["path"] = url.substr(path_start);
    }
    
    std::string host_port = url.substr(host_start, path_start - host_start);
    size_t colon_pos = host_port.find(':');
    
    if (colon_pos == std::string::npos) {
        result["host"] = host_port;
        result["port"] = (result["protocol"] == "https") ? "443" : "80";
    } else {
        result["host"] = host_port.substr(0, colon_pos);
        result["port"] = host_port.substr(colon_pos + 1);
    }
    
    return result;
}

#ifdef _WIN32
FallbackHttpClient::HttpResponse FallbackHttpClient::windows_http_request(const std::string& method,
                                                                          const std::string& url,
                                                                          const std::string& data,
                                                                          const std::map<std::string, std::string>& headers) {
    HttpResponse response;
    
    auto url_parts = parse_url(url);
    if (url_parts.count("error")) {
        response.error_message = url_parts["error"];
        return response;
    }
    
    // Convert strings to wide strings
    std::wstring whost(url_parts["host"].begin(), url_parts["host"].end());
    std::wstring wpath(url_parts["path"].begin(), url_parts["path"].end());
    std::wstring wmethod(method.begin(), method.end());
    
    DWORD dwPort = std::stoi(url_parts["port"]);
    DWORD dwFlags = (url_parts["protocol"] == "https") ? WINHTTP_FLAG_SECURE : 0;
    
    // Initialize WinHTTP
    HINTERNET hSession = WinHttpOpen(L"ATS-V3/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, 
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        response.error_message = "Failed to initialize WinHTTP session";
        return response;
    }
    
    // Connect to server
    HINTERNET hConnect = WinHttpConnect(hSession, whost.c_str(), dwPort, 0);
    if (!hConnect) {
        response.error_message = "Failed to connect to server";
        WinHttpCloseHandle(hSession);
        return response;
    }
    
    // Create request
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, wmethod.c_str(), wpath.c_str(),
                                           NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, dwFlags);
    if (!hRequest) {
        response.error_message = "Failed to create request";
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return response;
    }
    
    // Send request
    BOOL bResult = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                     const_cast<char*>(data.c_str()), data.length(),
                                     data.length(), 0);
    
    if (bResult) {
        bResult = WinHttpReceiveResponse(hRequest, NULL);
    }
    
    if (bResult) {
        // Get status code
        DWORD dwSize = sizeof(DWORD);
        DWORD dwStatusCode = 0;
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                           WINHTTP_HEADER_NAME_BY_INDEX, &dwStatusCode, &dwSize, WINHTTP_NO_HEADER_INDEX);
        response.status_code = dwStatusCode;
        
        // Read response body
        DWORD dwDownloaded = 0;
        do {
            DWORD dwAvailable = 0;
            if (!WinHttpQueryDataAvailable(hRequest, &dwAvailable)) {
                break;
            }
            
            if (dwAvailable > 0) {
                char* pBuffer = new char[dwAvailable + 1];
                ZeroMemory(pBuffer, dwAvailable + 1);
                
                if (WinHttpReadData(hRequest, pBuffer, dwAvailable, &dwDownloaded)) {
                    response.body.append(pBuffer, dwDownloaded);
                }
                delete[] pBuffer;
            }
        } while (dwAvailable > 0);
        
        response.success = true;
    } else {
        response.error_message = "Failed to send/receive HTTP request";
    }
    
    // Clean up
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    
    return response;
}
#else
FallbackHttpClient::HttpResponse FallbackHttpClient::unix_http_request(const std::string& method,
                                                                       const std::string& url,
                                                                       const std::string& data,
                                                                       const std::map<std::string, std::string>& headers) {
    HttpResponse response;
    
    auto url_parts = parse_url(url);
    if (url_parts.count("error")) {
        response.error_message = url_parts["error"];
        return response;
    }
    
    // Create socket
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        response.error_message = "Failed to create socket";
        return response;
    }
    
    // Get host info
    struct hostent* server = gethostbyname(url_parts["host"].c_str());
    if (!server) {
        response.error_message = "Failed to resolve hostname";
        close(sockfd);
        return response;
    }
    
    // Setup server address
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);
    serv_addr.sin_port = htons(std::stoi(url_parts["port"]));
    
    // Connect
    if (connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        response.error_message = "Failed to connect to server";
        close(sockfd);
        return response;
    }
    
    // Build HTTP request
    std::ostringstream request;
    request << method << " " << url_parts["path"] << " HTTP/1.1\r\n";
    request << "Host: " << url_parts["host"] << "\r\n";
    request << "Connection: close\r\n";
    
    // Add custom headers
    for (const auto& header : headers) {
        request << header.first << ": " << header.second << "\r\n";
    }
    
    if (!data.empty()) {
        request << "Content-Length: " << data.length() << "\r\n";
        request << "Content-Type: application/json\r\n";
    }
    
    request << "\r\n";
    if (!data.empty()) {
        request << data;
    }
    
    std::string request_str = request.str();
    
    // Send request
    if (send(sockfd, request_str.c_str(), request_str.length(), 0) < 0) {
        response.error_message = "Failed to send request";
        close(sockfd);
        return response;
    }
    
    // Read response
    char buffer[4096];
    std::string raw_response;
    ssize_t bytes_read;
    
    while ((bytes_read = recv(sockfd, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[bytes_read] = '\0';
        raw_response += buffer;
    }
    
    close(sockfd);
    
    if (raw_response.empty()) {
        response.error_message = "No response received";
        return response;
    }
    
    // Parse response
    size_t header_end = raw_response.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        response.error_message = "Invalid HTTP response format";
        return response;
    }
    
    std::string header_section = raw_response.substr(0, header_end);
    response.body = raw_response.substr(header_end + 4);
    
    // Parse status code
    size_t status_start = header_section.find(" ") + 1;
    size_t status_end = header_section.find(" ", status_start);
    if (status_start != std::string::npos && status_end != std::string::npos) {
        response.status_code = std::stoi(header_section.substr(status_start, status_end - status_start));
    }
    
    response.success = true;
    return response;
}
#endif

} // namespace ats