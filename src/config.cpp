#include "config.h"
#include <fstream>
#include <nlohmann/json.hpp>
#include <cstdlib>

Config load_config(const std::string& path) {
    std::ifstream f(path);
    if (!f) {
        throw std::runtime_error("Cannot open config file: " + path);
    }

    nlohmann::json j;
    f >> j;

    Config cfg;
    cfg.symbol = j.at("symbol");
    cfg.quantity = j.at("quantity");
    cfg.order_mode = j.at("order_mode");
    cfg.settle = j.at("settle");

    // Read API credentials from environment variables (preferred) or config file
    const char* env_api_key = std::getenv("API_KEY");
    const char* env_api_secret = std::getenv("API_SECRET");

    if (env_api_key) {
        cfg.api_key = env_api_key;
    } else if (j.contains("api_key")) {
        cfg.api_key = j.at("api_key");
    }

    if (env_api_secret) {
        cfg.api_secret = env_api_secret;
    } else if (j.contains("api_secret")) {
        cfg.api_secret = j.at("api_secret");
    }

    return cfg;
}
