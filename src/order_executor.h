#pragma once

class OrderExecutor {
public:
    virtual void place_order(double price, double quantity) = 0;
    virtual ~OrderExecutor() = default;
};
