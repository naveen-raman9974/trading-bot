#pragma once

#include "order_executor.h"
#include <string>
#include <memory>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/http.hpp>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;

class RestOrderExecutor : public OrderExecutor {
public:
    RestOrderExecutor(const std::string& api_key,
                      const std::string& api_secret,
                      const std::string& symbol);
    ~RestOrderExecutor();

    void place_order(double price, double quantity) override;

private:
    void connect();
    void reconnect();
    void do_place_order(double price, double quantity);

    std::string api_key_;
    std::string api_secret_;
    std::string symbol_;
    
    // Persistent connection members
    net::io_context ioc_;
    ssl::context ssl_ctx_;
    std::unique_ptr<beast::ssl_stream<tcp::socket>> stream_;
    bool connected_ = false;
    static constexpr const char* host_ = "api-testnet.gateapi.io";
    static constexpr const char* port_ = "443";
};
