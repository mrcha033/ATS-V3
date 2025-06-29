#pragma once

#include <string>
#include <memory>
#include <vector>
#include "../core/types.hpp"
#include "../core/risk_manager.hpp"

struct sqlite3;

namespace ats {

class DatabaseManager {
public:
    DatabaseManager(const std::string& db_path);
    ~DatabaseManager();

    bool Open();
    void Close();

    bool SaveTrade(const TradeRecord& trade);
    std::vector<TradeRecord> GetTradeHistory(int limit = 100);

private:
    std::string db_path_;
    sqlite3* db_;
};

} // namespace ats
