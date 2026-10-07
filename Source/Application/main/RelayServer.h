#pragma once

#include "rtc/rtc.hpp"
#include "Boost/asio.hpp"

#include <memory>
#include <iostream>
#include <functional>
#include <deque>
#include "boost/json.hpp"

#include "SupportUtils.h"
#include "Serialize.h"
#include "MessageRouter.h"

#include "Encryption.h"


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
    Wyvern::Protocol::MessageRouter<Wyvern::Protocol::ProtocolVisitor> router{};

    std::unordered_map< std::string, std::shared_ptr<RelayNodeInfo>> storedNodes;

    bool storeDelayedMessage(RelayNodeInfo, boost::json::object) { return true; };
    bool changeNodeActivity(RelayNodeInfo, NodeActivityStatus) { return true; };

    bool isNodeActive(RelayNodeInfo) { return true; };

public:
    explicit RelayServer(const Wyvern::Endpoint& endpoint) {
        rtc::WebSocketServer::Configuration cfg;
        cfg.port = endpoint.port;
        cfg.bindAddress = endpoint.host;
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
    void checkIdentity() {

    }


    void onIncoming(std::shared_ptr<rtc::WebSocket> ws) {//TODO: Переделать. Протокол теперь главный.
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
            callCheckAndSendPendingMessages(node);//TODO: Позже прикрутить после шифрования

            });

        ws->onMessage([this, ws](rtc::message_variant msg) {
            if (!std::holds_alternative<rtc::binary>(msg))
                return;
            router.route(ws, std::get<rtc::binary>(std::move(msg)));
            });

        ws->onClosed([this, ws] {
            std::lock_guard lk{ mtx_ };
            for (auto it = storedNodes.begin(); it != storedNodes.end(); ) {// FIXME: Оптимизировать, либо вынести в асинхрон. В целом, если использовать ws как ключ - сложность будет O(1)
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
private:
    
    std::mutex mtx_;
    std::deque<std::shared_ptr<rtc::WebSocket>> pendingCloseConnection;//TODO: Посмотреть: а надо ли
};