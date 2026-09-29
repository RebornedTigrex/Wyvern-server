#pragma once

#include "rtc/rtc.hpp"
#include "Boost/asio.hpp"

#include <memory>
#include <iostream>
#include <functional>
#include <deque>
#include "boost/json.hpp"

#include "SupportUtils.h"
#include "ProtocolSerialize.h"


using Server = rtc::WebSocketServer;

enum class NodeActivityStatus {
    Online,
    Offline
};

struct RelayNodeInfo {
    NodeActivityStatus status;

    std::shared_ptr<rtc::WebSocket> connection;
    std::deque<boost::json::object> pendingMessages;
};

class RelayServer {

    std::optional<Server> serverInstance;

    std::unordered_map< std::string, std::shared_ptr<RelayNodeInfo>> storedNodes;

    bool storeDelayedMessage(RelayNodeInfo, boost::json::object) { return true; };
    bool changeNodeActivity(RelayNodeInfo, NodeActivityStatus) { return true; };

    bool isNodeActive(RelayNodeInfo) { return true; };

public:
    explicit RelayServer(std::shared_ptr<Wyvern::Endpoint> endpoint) {
        rtc::WebSocketServer::Configuration cfg;
        cfg.port = endpoint->port;
        cfg.bindAddress = endpoint->host;
        cfg.enableTls = false;
        cfg.maxMessageSize = 256 * 1024; // SDP + trickle ICE спокойно влезут

        serverInstance.emplace(std::move(cfg));
        serverInstance->onClient([this](std::shared_ptr<rtc::WebSocket> ws) {
            onIncoming(std::move(ws));
            });
    }

    void callCheckAndSendPendingMessages(std::weak_ptr<RelayNodeInfo> node) {
        auto nodePtr = node.lock();
        if (!nodePtr) return;

        return;
    }

private:
    void onIncoming(std::shared_ptr<rtc::WebSocket> ws) {//TODO: Вынести в cpp, проверить код
        // handshake ещё не закончен
        ws->onOpen([this, ws] {
            const std::string id = Wyvern::Utilities::normalize_path(ws->path().value_or(""));
            if (id.empty()) {
                ws->close();
                return;
            }

            std::shared_ptr<RelayNodeInfo> node;

            {
                std::lock_guard lk{ mtx_ };
                auto& nodePtr = storedNodes [id]; // Находит или создаёт std::shared_ptr
                if (!nodePtr) {
                    nodePtr = std::make_shared<RelayNodeInfo>();
                }

                nodePtr->status = NodeActivityStatus::Online;
                nodePtr->connection = ws;
                node = nodePtr;
            }

            callCheckAndSendPendingMessages(node);
            storedNodes [id]->connection = ws;

            });

        ws->onMessage([this, ws](rtc::message_variant msg) {
            if (!std::holds_alternative<rtc::binary>(msg))
                return; // сигналинг — текст
            route(ws, std::get<rtc::binary>(std::move(msg)));
            });

        ws->onClosed([this, ws] {
            std::lock_guard lk{ mtx_ };
            for (auto it = storedNodes.begin(); it != storedNodes.end(); ) {// FIXME: O(n), либо оптимизировать, либо вынести в асинхрон. В целом, если использовать ws как ключ - сложность будет O(1)
                if (it->second->connection == ws) {
                    it->second->status = NodeActivityStatus::Offline;
                }
                ++it;
            }
            });

        ws->onError([](std::string e) {
            // лог
            });
    }

    void route(const std::shared_ptr<rtc::WebSocket>& from, std::vector<std::byte> body) {
        Wyvern::Protocol::Message msg = Wyvern::Protocol::deserialize(body);
        std::visit([&](const auto& m) { handle(from, m); }, msg);
    }

private:
    void handle(const std::shared_ptr<rtc::WebSocket>& from,
        const Wyvern::Protocol::Packets::InitMsg& req)
    {
        using namespace Wyvern::Protocol;

        std::string status;

        switch (req.payload.action) {
        case ActionType::NoAction: { status = "ok"; break; }
        case ActionType::GetRelayList: { status = "Not implemented"; break; }
        }
            

        Packets::InitAckPayload payload{
            .status = status,
            .relays_payload_size = UINT32_MAX,
            .relays = {},
        };
        
        auto ack = make_reply<Packets::InitAckMsg>(
            req.env,
            Packets::InitAckMsg::kType,
            static_cast<std::uint64_t>(outSeq_.newNum()),
            Wyvern::Utilities::getSelfID(),
            Wyvern::Utilities::NowMs(),
            std::move(payload));

        from->send(serialize(Message{ std::move(ack) }));
    }

    void handle(const std::shared_ptr<rtc::WebSocket>& from,
        const Wyvern::Protocol::Packets::InitAckMsg& req){return;}

    void handle(const std::shared_ptr<rtc::WebSocket>& from,
        const Wyvern::Protocol::Packets::PublishMsg& req)
    {
        return;
    }

    void handle(const std::shared_ptr<rtc::WebSocket>& from,
        const Wyvern::Protocol::Packets::AckMsg& req){ return; }
    void handle(const std::shared_ptr<rtc::WebSocket>& from,
        const Wyvern::Protocol::Packets::NackMsg& req){ return; }
    
    Wyvern::Utilities::ThreadSafeCounter outSeq_;
    std::mutex mtx_;
    std::deque<std::shared_ptr<rtc::WebSocket>> pendingCloseConnection;//TODO: Посмотреть: а надо ли
};