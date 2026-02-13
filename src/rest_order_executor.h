#pragma once

#include "order_executor.h"
#include <string>

class RestOrderExecutor : public OrderExecutor {
public:
    RestOrderExecutor(const std::string& api_key,
                      const std::string& api_secret,
                      const std::string& symbol);

    void place_order(double price, double quantity) override;

private:
    std::string api_key_;
    std::string api_secret_;
    std::string symbol_;
};
