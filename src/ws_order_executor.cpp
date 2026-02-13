#include "ws_order_executor.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <openssl/hmac.h>
#include <chrono>

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = net::ip::tcp;

// Connection Implementation
WsOrderExecutor::WsOrderExecutor(std::string api_key, std::string api_secret, std::string symbol)
    : api_key_(std::move(api_key)), api_secret_(std::move(api_secret)), symbol_(std::move(symbol)),
      ssl_ctx_(net::ssl::context::tlsv12_client), ws_(ioc_, ssl_ctx_) 
{
    connect();
}

WsOrderExecutor::~WsOrderExecutor() {
    // Close socket
    boost::system::error_code ec;
    beast::get_lowest_layer(ws_).close(ec);
    
    if (reader_.joinable()) {
        reader_.join();
    }
}

// Hashing
static std::string hmac_sha512(const std::string& key, const std::string& data) {
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    HMAC(EVP_sha512(), key.data(), key.size(), (unsigned char*)data.data(), data.size(), hash, &len);
    std::stringstream ss;
    for (unsigned int i = 0; i < len; i++) ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    return ss.str();
}

void WsOrderExecutor::connect() {
    try {
        std::string host = "ws-testnet.gate.io";
        tcp::resolver resolver(ioc_);
        auto const results = resolver.resolve(host, "443");

        net::connect(beast::get_lowest_layer(ws_), results);
        SSL_set_tlsext_host_name(ws_.next_layer().native_handle(), host.c_str());
        ws_.next_layer().handshake(net::ssl::stream_base::client);
        ws_.handshake(host, "/v4/ws/spot");

        std::cout << "[WS-ORDER] Socket connected. Sending login" << std::endl;

    //Reader loop in different thread    
    logged_in_ = false;
    reader_ = std::thread(&WsOrderExecutor::reader_loop, this);

    std::unique_lock<std::mutex> lock(mtx_); 
    send_login();

    //We wait for logged_in flag to be true in reader loop
    if (cv_.wait_for(lock, std::chrono::seconds(5), [this] { return logged_in_.load(); })) {
        std::cout << "[WS-ORDER] Authentication confirmed!" << std::endl;
    } else {
        std::cout << "[ERROR] Login failed" << std::endl;
    }
}
     catch (const std::exception& e) {
        std::cerr << "[WS-ORDER] Connection error: " << e.what() << std::endl;
    }
}

void WsOrderExecutor::send_login() {
    std::time_t ts = std::time(nullptr);
    std::string ts_str = std::to_string(ts);
    
    // Get MS for req_id
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    std::string req_id = std::to_string(ms) + "-1";

    // Signature generation
    std::string sign_str = "api\nspot.login\n\n" + ts_str;

    // Generate HMAC-SHA512
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    HMAC(EVP_sha512(), api_secret_.data(), (int)api_secret_.size(),
         (unsigned char*)sign_str.data(), (int)sign_str.size(), hash, &len);

    std::stringstream ss;
    for (unsigned int i = 0; i < len; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    }
    std::string signature = ss.str();

    // build login request
    nlohmann::json login_msg = {
        {"time", ts},
        {"channel", "spot.login"},
        {"event", "api"},
        {"payload", {
            {"api_key", api_key_},
            {"signature", signature},
            {"timestamp", ts_str},
            {"req_id", req_id}
        }}
    };

    std::lock_guard<std::mutex> lock(ws_mtx_);
    ws_.write(boost::asio::buffer(login_msg.dump()));
    std::cout << "[WS-ORDER] Login request sent. Payload: " << login_msg.dump() << std::endl;
}

void WsOrderExecutor::place_order(double price, double quantity) {
    if (!logged_in_) return; //False when login fails

    std::ostringstream ss_p, ss_q;
    ss_p << std::fixed << std::setprecision(2) << price;
    ss_q << std::fixed << std::setprecision(4) << quantity;

    nlohmann::json order_msg = {
        {"time", std::time(nullptr)},
        {"channel", "spot.order_place"},
        {"event", "api"},
        {"payload", {
            {"req_id", "order-" + std::to_string(std::time(nullptr))},
            {"req_param", {
                {"text", "t-" + std::to_string(std::time(nullptr))},
                {"currency_pair", symbol_},
                {"type", "limit"},
                {"account", "spot"},
                {"side", "buy"},
                {"amount", ss_q.str()},
                {"price", ss_p.str()},
                {"time_in_force", "gtc"}
            }}
        }}
    };

    try {
        std::lock_guard<std::mutex> lock(ws_mtx_);
        ws_.write(boost::asio::buffer(order_msg.dump()));
        std::cout << "[WS-ORDER] Order Sent: " << ss_q.str() << " at " << ss_p.str() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[WS-ORDER] Order Send Error: " << e.what() << std::endl;
    }
}

void WsOrderExecutor::reader_loop() {
    beast::flat_buffer buffer;
    try {
        while (true) {
            buffer.clear();
            ws_.read(buffer);
            std::string msg = beast::buffers_to_string(buffer.data());
            
            auto j = nlohmann::json::parse(msg, nullptr, false);
            if (j.is_discarded()) continue;

            // Extract channel if it exists in the header
            std::string channel = "";
            if (j.contains("header") && j["header"].contains("channel")) {
                channel = j["header"]["channel"];
            }

            if (channel == "spot.login") {
                if (j["header"]["status"] == "200" || j["header"]["status"] == 200) {
                    std::cout << "[WS-ORDER] Authentication SUCCESS." << std::endl;
                    {
                        std::lock_guard<std::mutex> lock(mtx_);
                        logged_in_ = true;
                    }
                    cv_.notify_all();
                }
            } 
            else if (channel == "spot.order_place") {
                std::cout << "[ORDER RESULT]: " << msg << std::endl;
                // You can add logic here to notify your strategy that the trade is done
            }
            else if (channel == "spot.pong") {
                // Silently ignore pongs to keep the console clean
            }
            else {
                // Print unknown messages for debugging
                std::cout << "[WS-ORDER RECV]: " << msg << std::endl;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "[WS-ORDER] Reader Error: " << e.what() << std::endl;
        logged_in_ = false;
    }
}