#include "rest_order_executor.h"

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <nlohmann/json.hpp>

#include <openssl/hmac.h>
#include <ctime>
#include <spdlog/spdlog.h>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;

static std::string hmac_sha512(const std::string& key,
                               const std::string& msg) {
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int len = 0;

    HMAC(EVP_sha512(),
         key.data(), key.size(),
         reinterpret_cast<const unsigned char*>(msg.data()),
         msg.size(),
         hash, &len);

    std::ostringstream oss;
    for (unsigned int i = 0; i < len; ++i)
        oss << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<int>(hash[i]);
    return oss.str();
}

std::string format_precision(double value, int precision) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(precision) << value;
    return out.str();
}

static std::string sha512_hex(const std::string& data) {
    unsigned char hash[SHA512_DIGEST_LENGTH];
    SHA512(reinterpret_cast<const unsigned char*>(data.c_str()), data.size(), hash);

    std::ostringstream oss;
    for (int i = 0; i < SHA512_DIGEST_LENGTH; ++i)
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    return oss.str();
}

RestOrderExecutor::RestOrderExecutor(const std::string& api_key,
                                     const std::string& api_secret,
                                     const std::string& symbol)
    : api_key_(api_key),
      api_secret_(api_secret),
      symbol_(symbol),
      ssl_ctx_(ssl::context::tlsv12_client) {
    ssl_ctx_.set_default_verify_paths();
}

RestOrderExecutor::~RestOrderExecutor() {
    if (stream_ && connected_) {
        boost::system::error_code ec;
        stream_->shutdown(ec);
        if (ec == boost::asio::ssl::error::stream_truncated) {
            ec = {};
        }
    }
}

void RestOrderExecutor::connect() {
    try {
        tcp::resolver resolver{ioc_};
        auto const results = resolver.resolve(host_, port_);
        
        stream_ = std::make_unique<beast::ssl_stream<tcp::socket>>(ioc_, ssl_ctx_);
        
        net::connect(stream_->next_layer(), results);
        stream_->handshake(ssl::stream_base::client);
        
        connected_ = true;
        spdlog::info("[REST] Connected to {}", host_);
    } catch (const std::exception& e) {
        connected_ = false;
        spdlog::error("[REST] Connection failed: {}", e.what());
        throw;
    }
}

void RestOrderExecutor::reconnect() {
    spdlog::warn("[REST] Reconnecting...");
    connected_ = false;
    stream_.reset();
    connect();
}

void RestOrderExecutor::do_place_order(double price, double quantity) {
    std::string price_str = format_precision(price, 2);
    std::string amount_str = format_precision(quantity, 4);
    
    nlohmann::json body = {
        {"currency_pair", symbol_},
        {"type", "limit"},
        {"side", "buy"},
        {"price", price_str},
        {"amount", amount_str}
    };

    std::string body_str = body.dump();
    std::string hashed_payload = sha512_hex(body_str);
    std::string timestamp = std::to_string(std::time(nullptr));
    
    std::string sign_payload =
        "POST\n"
        "/api/v4/spot/orders\n"
        "\n"
        + hashed_payload + "\n" +
        timestamp;

    std::string sign = hmac_sha512(api_secret_, sign_payload);

    http::request<http::string_body> req{
        http::verb::post, "/api/v4/spot/orders", 11
    };

    req.set(http::field::host, host_);
    req.set(http::field::content_type, "application/json");
    req.set("KEY", api_key_);
    req.set("SIGN", sign);
    req.set("Timestamp", timestamp);
    req.set(http::field::connection, "keep-alive");  // Enable keep-alive
    req.body() = body_str;
    req.prepare_payload();

    http::write(*stream_, req);

    beast::flat_buffer buffer;
    http::response<http::string_body> res;
    http::read(*stream_, buffer, res);

    spdlog::info("[REST] Order response: {}", res.body());
}

void RestOrderExecutor::place_order(double price, double quantity) {
    try {
        // Connect on first use
        if (!connected_ || !stream_) {
            connect();
        }
        
        do_place_order(price, quantity);
        
    } catch (const std::exception& ex) {
        spdlog::error("[REST] Order error: {}", ex.what());
        
        // Try to reconnect and retry once
        try {
            reconnect();
            do_place_order(price, quantity);
        } catch (const std::exception& retry_ex) {
            spdlog::error("[REST] Retry failed: {}", retry_ex.what());
            throw;
        }
    }
}
