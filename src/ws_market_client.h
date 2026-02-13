#pragma once

#include <boost/beast/ssl/ssl_stream.hpp>
#include <string>
#include <ctime>
#include <atomic>
#include <memory>

#include <boost/beast.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

// -------------------------------
// Domain object
// -------------------------------
struct BookTicker {
    double bid;
    double ask;
};

namespace net = boost::asio;
namespace ssl = net::ssl;
namespace websocket = boost::beast::websocket;
namespace beast = boost::beast;
using tcp = net::ip::tcp;

// -------------------------------
// WebSocket Market Client
// -------------------------------
template <typename Handler>
class WsMarketClient {
public:
    WsMarketClient(std::string symbol, Handler& handler)
        : symbol_(std::move(symbol)),
          handler_(handler),
          stop_requested_(false)
    {}

    void request_stop() {
        stop_requested_.store(true, std::memory_order_release);
        // Close websocket to break out of read
        if (ws_ && ws_->is_open()) {
            boost::system::error_code ec;
            ws_->close(websocket::close_code::normal, ec);
        }
    }

    // Blocking call
    void connect_and_run()
{
    try {
        // Testnet endpoint details
        std::string host = "ws-testnet.gate.io"; 
        std::string port = "443";
        std::string path = "/v4/ws/spot";

        net::io_context ioc;
        ssl::context ctx{ssl::context::tlsv12_client};
        ctx.set_default_verify_paths();

        tcp::resolver resolver{ioc};
        ws_ = std::make_unique<websocket::stream<beast::ssl_stream<tcp::socket>>>(ioc, ctx);

        //Resolve the host and port
        auto const results = resolver.resolve(host, port);
        
        // Use get_lowest_layer to ensure we are connecting the actual TCP socket
        boost::asio::connect(beast::get_lowest_layer(*ws_), results);

        // Set SNI Hostname
        if(!SSL_set_tlsext_host_name(ws_->next_layer().native_handle(), host.c_str())) {
            throw beast::system_error(
                beast::error_code(static_cast<int>(::ERR_get_error()), net::error::get_ssl_category())
            );
        }

        //SSL Handshake
        ws_->next_layer().handshake(ssl::stream_base::client);

        //WebSocket Handshake
        ws_->handshake(host, path);

        spdlog::info("[WS-MARKET] Connected to {}", host);

        //Subscribe to Book Ticker
        nlohmann::json sub = {
            {"time", std::time(nullptr)},
            {"channel", "spot.book_ticker"},
            {"event", "subscribe"},
            {"payload", {symbol_}}
        };
        ws_->write(net::buffer(sub.dump()));

            beast::flat_buffer buffer;

            while (!stop_requested_.load(std::memory_order_acquire)) {
                buffer.clear();
                
                boost::system::error_code ec;
                ws_->read(buffer, ec);
                
                if (ec) {
                    if (stop_requested_.load(std::memory_order_acquire)) {
                        break;
                    }
                    throw beast::system_error(ec);
                }

                auto msg = beast::buffers_to_string(buffer.data());
                auto json = nlohmann::json::parse(msg, nullptr, false);

                if (json.is_discarded() || !json.contains("result"))
                    continue;

                const auto& r = json["result"];
                if (!r.contains("b") || !r.contains("a"))
                    continue;

                BookTicker bt{
                    std::stod(r["b"].get<std::string>()),
                    std::stod(r["a"].get<std::string>())
                };
                handler_(bt);
            }
            
            // Close gracefully
            if (ws_->is_open()) {
                boost::system::error_code ec;
                ws_->close(websocket::close_code::normal, ec);
            }
        }
        catch (const std::exception& e) {
            spdlog::error("WS client error: {}", e.what());
        }
    }

private:
    std::string symbol_;
    Handler& handler_;
    std::atomic<bool> stop_requested_;
    std::unique_ptr<websocket::stream<beast::ssl_stream<tcp::socket>>> ws_;
};
