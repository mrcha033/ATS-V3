#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <chrono>

namespace ats {
namespace utils {

// InfluxDB data point structure
struct InfluxDBPoint {
    std::string measurement;
    std::unordered_map<std::string, std::string> tags;
    std::unordered_map<std::string, double> fields;
    std::chrono::system_clock::time_point timestamp;
    
    InfluxDBPoint(const std::string& measurement_name) 
        : measurement(measurement_name)
        , timestamp(std::chrono::system_clock::now()) {}
};

// Placeholder InfluxDB client class for future implementation
class InfluxDBClient {
public:
    InfluxDBClient() = default;
    virtual ~InfluxDBClient() = default;
    
    // Connection management
    virtual bool connect(const std::string& url, const std::string& database) = 0;
    virtual bool disconnect() = 0;
    virtual bool is_connected() const = 0;
    
    // Database operations
    virtual bool create_database(const std::string& database) = 0;
    virtual bool drop_database(const std::string& database) = 0;
    virtual std::vector<std::string> list_databases() = 0;
    
    // Write operations
    virtual bool write_point(const InfluxDBPoint& point) = 0;
    virtual bool write_points(const std::vector<InfluxDBPoint>& points) = 0;
    virtual bool write_line(const std::string& line_protocol) = 0;
    
    // Query operations
    virtual std::string query(const std::string& query) = 0;
    virtual std::vector<std::unordered_map<std::string, std::string>> 
        query_table(const std::string& query) = 0;
    
    // Batch operations
    virtual void begin_batch() = 0;
    virtual void add_to_batch(const InfluxDBPoint& point) = 0;
    virtual bool commit_batch() = 0;
    virtual void clear_batch() = 0;
    
    // Utility methods
    virtual std::string point_to_line_protocol(const InfluxDBPoint& point) = 0;
};

// Factory function for creating InfluxDB client implementations
std::shared_ptr<InfluxDBClient> create_influxdb_client();

} // namespace utils
} // namespace ats