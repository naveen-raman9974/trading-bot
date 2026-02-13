#pragma once

#include <boost/beast/ssl/ssl_stream.hpp>
#include <string>
#include <iostream>
#include <ctime>

#include <boost/beast.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <nlohmann/json.hpp>

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
          handler_(handler)
    {}

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
        websocket::stream<beast::ssl_stream<tcp::socket>> ws{ioc, ctx};

        //Resolve the host and port
        auto const results = resolver.resolve(host, port);
        
        // Use get_lowest_layer to ensure we are connecting the actual TCP socket
        boost::asio::connect(beast::get_lowest_layer(ws), results);

        // Set SNI Hostname
        if(!SSL_set_tlsext_host_name(ws.next_layer().native_handle(), host.c_str())) {
            throw beast::system_error(
                beast::error_code(static_cast<int>(::ERR_get_error()), net::error::get_ssl_category())
            );
        }

        //SSL Handshake
        ws.next_layer().handshake(ssl::stream_base::client);

        //WebSocket Handshake
        ws.handshake(host, path);

        std::cout << "[WS-MARKET] Connected to " << host << std::endl;

        //Subscribe to Book Ticker
        nlohmann::json sub = {
            {"time", std::time(nullptr)},
            {"channel", "spot.book_ticker"},
            {"event", "subscribe"},
            {"payload", {symbol_}}
        };
        ws.write(net::buffer(sub.dump()));

            beast::flat_buffer buffer;

            while (true) {
                buffer.clear();
                ws.read(buffer);

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
        }
        catch (const std::exception& e) {
            std::cerr << "WS client error: " << e.what() << std::endl;
        }
    }

private:
    std::string symbol_;
    Handler& handler_;
};
