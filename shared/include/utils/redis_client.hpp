#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

namespace ats {
namespace utils {

// Placeholder Redis client class for future implementation
class RedisClient {
public:
    RedisClient() = default;
    virtual ~RedisClient() = default;
    
    // Connection management
    virtual bool connect(const std::string& host, int port = 6379) = 0;
    virtual bool disconnect() = 0;
    virtual bool is_connected() const = 0;
    
    // Basic operations
    virtual bool set(const std::string& key, const std::string& value) = 0;
    virtual std::string get(const std::string& key) = 0;
    virtual bool exists(const std::string& key) = 0;
    virtual bool del(const std::string& key) = 0;
    
    // Hash operations
    virtual bool hset(const std::string& key, const std::string& field, const std::string& value) = 0;
    virtual std::string hget(const std::string& key, const std::string& field) = 0;
    virtual std::unordered_map<std::string, std::string> hgetall(const std::string& key) = 0;
    
    // List operations
    virtual bool lpush(const std::string& key, const std::string& value) = 0;
    virtual bool rpush(const std::string& key, const std::string& value) = 0;
    virtual std::string lpop(const std::string& key) = 0;
    virtual std::string rpop(const std::string& key) = 0;
    
    // Pub/Sub operations
    virtual bool publish(const std::string& channel, const std::string& message) = 0;
    virtual bool subscribe(const std::string& channel) = 0;
    virtual bool unsubscribe(const std::string& channel) = 0;
};

// Factory function for creating Redis client implementations
std::shared_ptr<RedisClient> create_redis_client();

} // namespace utils
} // namespace ats