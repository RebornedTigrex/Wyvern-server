#pragma once

#include "../modules/dataStorage/IRelayStore.h"
#include "modules/BaseModule.h"
#include "runtime/ConfigSection.h"

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/json.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace Wyvern::Relay {

using WsStream = boost::beast::websocket::stream<boost::asio::ip::tcp::socket>;

// RelayServer — transport-level relay module.
//
// Responsibilities:
//  - Listen for incoming WebSocket connections.
//  - Track registered peers by overlay ID.
//  - Process REGISTER / RENDEZVOUS_REQUEST / RELAY_DATA frames.
//  - Keep ephemeral relay state in IRelayStore.
//  - Provide diagnostic status via module command.
class RelayServer : public BaseModule {
public:
    static std::string moduleType() { return "wyvern.relay-server"; }

    static boost::json::object defaults() {
        boost::json::object obj;
        obj["host"] = "0.0.0.0";
        obj["port"] = 9002;
        return obj;
    }

    RelayServer(const core::runtime::ConfigSection& cfg,
                boost::asio::io_context& ioc,
                std::shared_ptr<Wyvern::DataStorage::IRelayStore> relay_store);

    ~RelayServer();

    std::string moduleKey() const override { return moduleType(); }
    std::vector<core::contracts::CommandDescriptor> commands() override;

    bool start();
    void stop();

    std::size_t getConnectedPeerCount() const;
    std::string getStatus() const;

protected:
    bool onInitialize() override;
    void onShutdown() override;

private:
    core::contracts::CommandResult cmdStatus(const core::contracts::CommandArgs& args);

    void acceptConnections();
    void handleConnection(std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void startSessionRead(std::shared_ptr<WsStream> ws,
                          std::shared_ptr<boost::beast::flat_buffer> buffer);

    struct ClientSession {
        std::string overlay_id;
        std::string session_id;
        bool registered = false;
    };

    void processClientMessage(const std::string& overlay_id,
                              const std::string& json_text);
    void handleRegister(const std::string& overlay_id,
                        const std::string& client_session_id);
    void handleRendezvousRequest(const std::string& requesting_overlay_id,
                                 const std::string& target_overlay_id);
    void handleRelayData(const std::string& sender_overlay_id,
                         const std::string& target_overlay_id,
                         const std::string& base64_payload);

    boost::asio::io_context& ioc_;
    std::string listen_host_;
    std::uint16_t listen_port_;
    std::shared_ptr<Wyvern::DataStorage::IRelayStore> relay_store_;

    std::unique_ptr<boost::asio::ip::tcp::acceptor> acceptor_;
    mutable std::mutex clients_mutex_;
    std::unordered_map<std::string, ClientSession> clients_;
    std::atomic<bool> running_{false};
};

} // namespace Wyvern::Relay
