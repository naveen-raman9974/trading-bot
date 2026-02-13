#pragma once
#include "order_executor.h"
#include <string>

class FuturesRestOrderExecutor : public OrderExecutor {
public:
    FuturesRestOrderExecutor(const std::string& api_key,
                             const std::string& api_secret,
                             const std::string& symbol,
                             const std::string& settle);

    void place_order(double price, double quantity) override;

private:
    std::string api_key_;
    std::string api_secret_;
    std::string symbol_;
    std::string settle_;
};
