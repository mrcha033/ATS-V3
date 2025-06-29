add_library(ats-v3-lib
    main.cpp
    core/app_state.cpp
    core/arbitrage_engine.cpp
    core/opportunity_detector.cpp
    core/portfolio_manager.cpp
    core/price_monitor.cpp
    core/risk_manager.cpp
    core/trade_executor.cpp
    data/database_manager.cpp
    data/market_data.cpp
    data/price_cache.cpp
    data/sqlite3.c
    exchange/binance_exchange.cpp
    exchange/exchange_factory.cpp
    exchange/exchange_interface.cpp
    exchange/upbit_exchange.cpp
    monitoring/health_check.cpp
    monitoring/system_monitor.cpp
    network/rest_client.cpp
    network/websocket_client.cpp
    network/rate_limiter.cpp
    utils/config_manager.cpp
    utils/json_parser.cpp
    utils/logger.cpp
)

target_include_directories(ats-v3-lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
