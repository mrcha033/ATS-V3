#include "database_manager.hpp"
#include "../utils/logger.hpp"
#include "sqlite3.h"
#include <chrono>

namespace ats {

DatabaseManager::DatabaseManager(const std::string& db_path)
    : db_path_(db_path), db_(nullptr) {}

DatabaseManager::~DatabaseManager() {
    Close();
}

bool DatabaseManager::Open() {
    if (sqlite3_open(db_path_.c_str(), &db_)) {
        LOG_ERROR("Can't open database: {}", sqlite3_errmsg(db_));
        return false;
    } else {
        LOG_INFO("Opened database successfully");
    }

    const char* sql = "CREATE TABLE IF NOT EXISTS trades("
                      "id TEXT PRIMARY KEY NOT NULL,"
                      "symbol TEXT NOT NULL,"
                      "buy_exchange TEXT NOT NULL,"
                      "sell_exchange TEXT NOT NULL,"
                      "volume REAL NOT NULL,"
                      "buy_price REAL NOT NULL,"
                      "sell_price REAL NOT NULL,"
                      "pnl REAL NOT NULL,"
                      "timestamp INTEGER NOT NULL);";
    char* zErrMsg = 0;
    if (sqlite3_exec(db_, sql, 0, 0, &zErrMsg) != SQLITE_OK) {
        LOG_ERROR("SQL error: {}", zErrMsg);
        sqlite3_free(zErrMsg);
        return false;
    }
    return true;
}

void DatabaseManager::Close() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool DatabaseManager::SaveTrade(const TradeRecord& trade) {
    if (!db_) return false;

    std::string sql = "INSERT INTO trades (id,symbol,buy_exchange,sell_exchange,volume,buy_price,sell_price,pnl,timestamp) "
                      "VALUES (?,?,?,?,?,?,?,?,?);";
    
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) {
        LOG_ERROR("Failed to prepare statement: {}", sqlite3_errmsg(db_));
        return false;
    }

    sqlite3_bind_text(stmt, 1, trade.trade_id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, trade.symbol.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, trade.buy_exchange.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, trade.sell_exchange.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_double(stmt, 5, trade.volume);
    sqlite3_bind_double(stmt, 6, trade.buy_price);
    sqlite3_bind_double(stmt, 7, trade.sell_price);
    sqlite3_bind_double(stmt, 8, trade.realized_pnl);
    sqlite3_bind_int64(stmt, 9, std::chrono::duration_cast<std::chrono::seconds>(trade.end_time.time_since_epoch()).count());

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        LOG_ERROR("Failed to execute statement: {}", sqlite3_errmsg(db_));
        sqlite3_finalize(stmt);
        return false;
    }

    sqlite3_finalize(stmt);
    return true;
}

std::vector<TradeRecord> DatabaseManager::GetTradeHistory(int limit) {
    std::vector<TradeRecord> trades;
    if (!db_) return trades;

    std::string sql = "SELECT * FROM trades ORDER BY timestamp DESC LIMIT ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) {
        LOG_ERROR("Failed to prepare statement: {}", sqlite3_errmsg(db_));
        return trades;
    }

    sqlite3_bind_int(stmt, 1, limit);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        TradeRecord trade;
        trade.trade_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        trade.symbol = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        trade.buy_exchange = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        trade.sell_exchange = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        trade.volume = sqlite3_column_double(stmt, 4);
        trade.buy_price = sqlite3_column_double(stmt, 5);
        trade.sell_price = sqlite3_column_double(stmt, 6);
        trade.realized_pnl = sqlite3_column_double(stmt, 7);
        trade.end_time = std::chrono::system_clock::from_time_t(sqlite3_column_int64(stmt, 8));
        trades.push_back(trade);
    }

    sqlite3_finalize(stmt);
    return trades;
}

} // namespace ats
