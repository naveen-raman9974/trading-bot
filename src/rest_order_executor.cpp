#include "rest_order_executor.h"

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <nlohmann/json.hpp>

#include <openssl/hmac.h>
#include <ctime>
#include <iostream>

namespace beast = boost::beast;
namespace http  = beast::http;
namespace ssl   = boost::asio::ssl;
namespace net   = boost::asio;
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
      symbol_(symbol) {}

void RestOrderExecutor::place_order(double price, double quantity) {
    try {
        net::io_context ioc;
        ssl::context ctx{ssl::context::tlsv12_client};
        ctx.set_default_verify_paths();

        tcp::resolver resolver{ioc};
        beast::ssl_stream<tcp::socket> stream{ioc, ctx};

        auto const results = resolver.resolve("api-testnet.gateapi.io", "443");
        net::connect(stream.next_layer(), results);
        stream.handshake(ssl::stream_base::client);

        std::string price_str = format_precision(price, 2);
        std::string amount_str = format_precision(quantity, 4);
        //Request body
        nlohmann::json body = {
            {"currency_pair", symbol_},
            {"type", "limit"},
            {"side", "buy"},
            {"price", price_str},
            {"amount", amount_str}
        };

        std::string body_str = body.dump();
        std::string hashed_payload = sha512_hex(body_str);
        // TIMESTAMP
        std::string timestamp = std::to_string(std::time(nullptr));
        
        //SIGN STRING (EXACT)
        std::string sign_payload =
            "POST\n"
            "/api/v4/spot/orders\n"
            "\n"
            + hashed_payload + "\n" +
            timestamp;

        std::string sign = hmac_sha512(api_secret_, sign_payload);

        // HTTP request
        http::request<http::string_body> req{
            http::verb::post, "/api/v4/spot/orders", 11
        };

        req.set(http::field::host, "api-testnet.gateapi.io");
        req.set(http::field::content_type, "application/json");
        req.set("KEY", api_key_);
        req.set("SIGN", sign);
        req.set("Timestamp", timestamp);
        req.body() = body_str;
        req.prepare_payload();

        http::write(stream, req);

        // Read Response
        beast::flat_buffer buffer;
        http::response<http::string_body> res;
        http::read(stream, buffer, res);

        std::cout << "[REST] Order response: "
                  << res.body() << std::endl;
                  
        //Shutdown after every order
        boost::system::error_code ec;
        stream.shutdown(ec);
        if (ec == boost::asio::ssl::error::stream_truncated) {
            ec = {}; 
        }

        if (ec) {
            std::cerr << "[REST] Shutdown error: " << ec.message() << std::endl;
        }
            } catch (const std::exception& ex) {
                std::cerr << "[REST] Order error: "
                        << ex.what() << std::endl;
            }
        }
