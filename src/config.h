#pragma once
#include <string>

struct Config {
    std::string symbol;
    double quantity;
    std::string order_mode;
    std::string api_key;
    std::string api_secret;
    std::string settle;
};


Config load_config(const std::string& path);
