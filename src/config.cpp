#include "config.h"
#include <fstream>
#include <nlohmann/json.hpp>

Config load_config(const std::string& path) {
    std::ifstream f(path);
    nlohmann::json j;
    f >> j;

    Config cfg;
    cfg.symbol = j["symbol"];
    cfg.quantity = j["quantity"];
    cfg.order_mode = j["order_mode"];
    cfg.api_key = j["api_key"];
    cfg.api_secret = j["api_secret"];
    cfg.settle = j["settle"];

    return cfg;
}
