#include "RelayServer.h"
#include "../modules/dataStorage/InMemoryRelayStore.h"
#include <boost/asio/io_context.hpp>
#include <iostream>
#include <thread>
#include <csignal>

static bool g_running = true;

void signalHandler(int signal) {
    std::cout << "\n[RelayServerMain] Received signal " << signal << ", shutting down...\n";
    g_running = false;
}

int main(int argc, char* argv[]) {
    std::string listen_host = "0.0.0.0";
    uint16_t listen_port = 9002;

    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--host" && i + 1 < argc) {
            listen_host = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            listen_port = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if (arg == "--help") {
            std::cout << "Usage: wyvern_relay [options]\n"
                      << "Options:\n"
                      << "  --host <address>   Listen address (default: 0.0.0.0)\n"
                      << "  --port <number>    Listen port (default: 9002)\n"
                      << "  --help              Show this help message\n";
            return 0;
        }
    }

    // Register signal handlers for graceful shutdown
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    try {
        std::cout << "[RelayServerMain] Starting Wyvern relay server\n";
        std::cout << "[RelayServerMain] Listening on ws://" << listen_host << ":" << listen_port << "\n";

        // Create IO context and relay store
        boost::asio::io_context ioc;
        auto relay_store = std::make_shared<Wyvern::DataStorage::InMemoryRelayStore>();

        // Create and start relay server
        Wyvern::Relay::RelayServer relay_server(ioc, listen_host, listen_port, relay_store);
        if (!relay_server.start()) {
            std::cerr << "[RelayServerMain] Failed to start relay server\n";
            return 1;
        }

        std::cout << "[RelayServerMain] " << relay_server.getStatus() << "\n";

        // Run IO context in main thread until shutdown signal
        std::thread io_thread([&ioc]() {
            ioc.run();
        });

        // Wait for shutdown signal
        while (g_running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        // Stop server and wait for IO thread
        relay_server.stop();
        ioc.stop();
        io_thread.join();

        std::cout << "[RelayServerMain] Relay server stopped\n";
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "[RelayServerMain] Fatal error: " << e.what() << "\n";
        return 1;
    }
}
