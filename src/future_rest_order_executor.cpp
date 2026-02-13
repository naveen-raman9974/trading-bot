#include "future_rest_order_executor.h"

#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <nlohmann/json.hpp>
#include <openssl/hmac.h>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <iostream>

namespace beast = boost::beast;
namespace http  = beast::http;
namespace net   = boost::asio;
namespace ssl   = boost::asio::ssl;
using tcp = net::ip::tcp;

static std::string hmac_sha512(const std::string& key,
                              const std::string& msg) {
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int len = 0;

    HMAC(EVP_sha512(), key.data(), key.size(),
         (unsigned char*)msg.data(), msg.size(), hash, &len);

    std::ostringstream oss;
    for (unsigned int i = 0; i < len; ++i)
        oss << std::hex << std::setw(2)
            << std::setfill('0') << (int)hash[i];
    return oss.str();
}

FuturesRestOrderExecutor::FuturesRestOrderExecutor(
    const std::string& api_key,
    const std::string& api_secret,
    const std::string& symbol,
    const std::string& settle)
    : api_key_(api_key),
      api_secret_(api_secret),
      symbol_(symbol),
      settle_(settle) {}

void FuturesRestOrderExecutor::place_order(double price, double quantity) {
    try {
        net::io_context ioc;
        ssl::context ctx{ssl::context::tlsv12_client};
        ctx.set_default_verify_paths();

        tcp::resolver resolver{ioc};
        beast::ssl_stream<tcp::socket> stream{ioc, ctx};

        auto const results =
            resolver.resolve("fx-api-testnet.gateio.ws", "443");
        net::connect(stream.next_layer(), results);
        stream.handshake(ssl::stream_base::client);

        nlohmann::json body = {
            {"contract", symbol_},
            {"size", (int)quantity},
            {"price", std::to_string(price)},
            {"side", "buy"},
            {"tif", "gtc"}
        };

        std::string body_str = body.dump();
        std::string timestamp = std::to_string(std::time(nullptr));

        std::string sign_payload =
            "POST\n/api/v4/futures/" + settle_ + "/orders\n\n" +
            body_str + "\n" + timestamp;

        std::string sign = hmac_sha512(api_secret_, sign_payload);

        http::request<http::string_body> req{
            http::verb::post,
            "/api/v4/futures/" + settle_ + "/orders",
            11
        };

        req.set(http::field::host, "fx-api-testnet.gateio.ws");
        req.set(http::field::content_type, "application/json");
        req.set("KEY", api_key_);
        req.set("SIGN", sign);
        req.set("Timestamp", timestamp);
        req.body() = body_str;
        req.prepare_payload();

        http::write(stream, req);

        beast::flat_buffer buffer;
        http::response<http::string_body> res;
        http::read(stream, buffer, res);

        std::cout << "[FUTURES REST] Response: "
                  << res.body() << std::endl;

        stream.shutdown();
    } catch (const std::exception& e) {
        std::cerr << "[FUTURES REST] Error: "
                  << e.what() << std::endl;
    }
}
