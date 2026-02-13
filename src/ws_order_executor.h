#pragma once

#include <boost/beast/ssl/ssl_stream.hpp>
#include <boost/beast.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include "order_executor.h"

class WsOrderExecutor : public OrderExecutor {
public:
    WsOrderExecutor(std::string api_key, std::string api_secret, std::string symbol);
    ~WsOrderExecutor();

    void place_order(double price, double quantity) override;
    bool is_logged_in() const { return logged_in_; }

private:
    void connect();
    void send_login();
    void reader_loop();

    std::string api_key_;
    std::string api_secret_;
    std::string symbol_;

    boost::asio::io_context ioc_;
    boost::asio::ssl::context ssl_ctx_;
    boost::beast::websocket::stream<boost::beast::ssl_stream<boost::asio::ip::tcp::socket>> ws_;

    std::thread reader_;
    std::atomic<bool> logged_in_{false};
    
    std::mutex ws_mtx_; 
    std::mutex mtx_;
    std::condition_variable cv_;
};