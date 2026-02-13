#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>
#include <memory>

#include "config.h"
#include "ws_market_client.h"
#include "order_executor.h"
#include "rest_order_executor.h"
#include "ws_order_executor.h"

// Market data handler
class BookTickerHandler {
public:
    BookTickerHandler(double& pending_price,
                      double& last_sent_price,
                      std::atomic<bool>& order_pending)
        : pending_price_(pending_price),
          last_sent_price_(last_sent_price),
          order_pending_(order_pending)
    {}

    void operator()(const BookTicker& bt) {
        double mid = (bt.bid + bt.ask) / 2.0;
        std::cout<<"price: "<<mid<<std::endl;
        // VERIFICATION 
        std::cout << "[DEBUG] Received Price: " << mid  << std::endl;
        if (mid != last_sent_price_) {
            pending_price_ = mid;
            order_pending_.store(true, std::memory_order_release);
            std::cout << "[DEBUG] Mid changed from " <<last_sent_price_<<" to "<< mid << std::endl;
        }
    }

private:
    double& pending_price_;
    double& last_sent_price_;
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
                        double& pending_price,
                        double& last_sent_price,
                        std::atomic<bool>& order_pending,
                        std::atomic<bool>& order_in_flight,
                        double quantity)
{
    while (true) {
        if (!order_pending.exchange(false, std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        if (order_in_flight.exchange(true)) {
            std::cout << "[DEBUG] Order skipped: Another order is already in-flight." << std::endl;
            continue;
        }

        // Ensure flag reset on all paths
        struct OrderInFlightReset {
            std::atomic<bool>& flag;
            ~OrderInFlightReset() { flag.store(false); }
        } reset{order_in_flight};

        try {
           std::cout << "[DEBUG] Calling place_order for " << quantity << " units..." << std::endl;
            executor.place_order(pending_price, quantity);
            last_sent_price = pending_price;
            std::cout << "[DEBUG] place_order call finished." << std::endl;
        }
        catch (const std::exception& e) {
            std::cerr << "[ERROR] " << e.what() << std::endl;
        }
    }
}

// Entry point
int main(int argc, char* argv[])
{
    if (argc < 2) {
        std::cerr << "Usage: ./bot <config.json>\n";
        return 1;
    }
    //Config parser
    Config cfg = load_config(argv[1]);

    //Order Executor (
    auto executor = create_executor(cfg);

    double pending_price = 0.0;
    double last_sent_price = 0.0;
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

    market_data_thread.join();
    return 0;
}
