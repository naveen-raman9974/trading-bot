#include <thread>
#include <atomic>
#include <chrono>
#include <memory>
#include <csignal>

#include <spdlog/spdlog.h>

#include "config.h"
#include "ws_market_client.h"
#include "order_executor.h"
#include "rest_order_executor.h"
#include "ws_order_executor.h"

// Global shutdown flag
std::atomic<bool> g_shutdown{false};

// Signal handler
void signal_handler(int signal) {
    spdlog::info("Received signal {}, initiating shutdown...", signal);
    g_shutdown.store(true, std::memory_order_release);
}

// Market data handler
class BookTickerHandler {
public:
    BookTickerHandler(std::atomic<double>& pending_price,
                      std::atomic<double>& last_sent_price,
                      std::atomic<bool>& order_pending)
        : pending_price_(pending_price),
          last_sent_price_(last_sent_price),
          order_pending_(order_pending)
    {}

    void operator()(const BookTicker& bt) {
        if (g_shutdown.load(std::memory_order_acquire)) {
            return;
        }
        
        double mid = (bt.bid + bt.ask) / 2.0;
        spdlog::info("price: {}", mid);
        // VERIFICATION
        spdlog::debug("Received Price: {}", mid);
        double last = last_sent_price_.load(std::memory_order_acquire);
        if (std::abs(mid - last) > 0.0000001) {
            pending_price_.store(mid, std::memory_order_release);
            order_pending_.store(true, std::memory_order_release);
            spdlog::debug("Mid changed from {} to {}", last, mid);
        }
    }

private:
    std::atomic<double>& pending_price_;
    std::atomic<double>& last_sent_price_;
    std::atomic<bool>& order_pending_;
};


std::unique_ptr<OrderExecutor>
create_executor(const Config& cfg)
{
    if (cfg.order_mode == "REST") {
        return std::make_unique<RestOrderExecutor>(
            cfg.api_key, cfg.api_secret, cfg.symbol);
    }

    if (cfg.order_mode == "WS") {
        return std::make_unique<WsOrderExecutor>(
            cfg.api_key, cfg.api_secret, cfg.symbol);
    }

    throw std::runtime_error("Invalid order_mode");
}

// Execution loop
void run_execution_loop(OrderExecutor& executor,
                        std::atomic<double>& pending_price,
                        std::atomic<double>& last_sent_price,
                        std::atomic<bool>& order_pending,
                        std::atomic<bool>& order_in_flight,
                        double quantity)
{
    while (!g_shutdown.load(std::memory_order_acquire)) {
        if (!order_pending.exchange(false, std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        if (order_in_flight.exchange(true)) {
            spdlog::debug("Order skipped: Another order is already in-flight.");
            continue;
        }

        // Ensure flag reset on all paths
        struct OrderInFlightReset {
            std::atomic<bool>& flag;
            ~OrderInFlightReset() { flag.store(false); }
        } reset{order_in_flight};

        try {
           spdlog::debug("Calling place_order for {} units...", quantity);
            double price = pending_price.load(std::memory_order_acquire);
            executor.place_order(price, quantity);
            last_sent_price.store(price, std::memory_order_release);
            spdlog::debug("place_order call finished.");
        }
        catch (const std::exception& e) {
            spdlog::error("{}", e.what());
        }
    }
    spdlog::info("Execution loop shutting down...");
}

// Entry point
int main(int argc, char* argv[])
{
    // Initialize spdlog
    spdlog::set_level(spdlog::level::debug);
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%s:%#] %v");

    // Setup signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    if (argc < 2) {
        spdlog::error("Usage: ./bot <config.json>");
        return 1;
    }
    //Config parser
    Config cfg = load_config(argv[1]);

    //Order Executor (
    auto executor = create_executor(cfg);

    std::atomic<double> pending_price{0.0};
    std::atomic<double> last_sent_price{0.0};
    std::atomic<bool> order_pending{false};
    std::atomic<bool> order_in_flight{false};

    // Market data handler
    BookTickerHandler handler(pending_price, last_sent_price, order_pending);
    WsMarketClient<BookTickerHandler> ws(cfg.symbol, handler);

    // Run market data client in separate thread
    std::thread market_data_thread(
        &WsMarketClient<BookTickerHandler>::connect_and_run,
        &ws
    );

    // Send Orders
    run_execution_loop(*executor,
                        pending_price,
                        last_sent_price,
                        order_pending,
                        order_in_flight,
                        cfg.quantity);

    // Shutdown sequence
    spdlog::info("Waiting for market data thread to finish...");
    ws.request_stop();
    if (market_data_thread.joinable()) {
        market_data_thread.join();
    }
    spdlog::info("Shutdown complete");
    return 0;
}
