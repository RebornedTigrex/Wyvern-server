#pragma once

#include "rtc/rtc.hpp"
#include "Boost/asio.hpp"

#include <memory>
#include <iostream>
#include <functional>
#include <deque>
#include "boost/json.hpp"

#include "SupportUtils.h"


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
    explicit RelayServer(uint16_t port) {
        rtc::WebSocketServer::Configuration cfg;
        cfg.port = port;
        cfg.bindAddress = "0.0.0.0";
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
            if (!std::holds_alternative<rtc::string>(msg))
                return; // сигналинг — текст
            route(ws, std::get<rtc::string>(std::move(msg)));
            });

        ws->onClosed([this, ws] {
            std::lock_guard lk{ mtx_ };
            for (auto it = storedNodes.begin(); it != storedNodes.end(); ) {// FIXME: O(n), либо оптимизировать, либо вынести в асинхрон
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

    void route(const std::shared_ptr<rtc::WebSocket>& from, std::string body) {
        boost::json::value j;
        try {
            j = boost::json::parse(body);
        }
        catch (...) {
            return;
        }
        auto* obj = j.if_object();
        if (!obj) return;

        auto* to_v = obj->if_contains("to");
        if (!to_v || !to_v->is_string()) return;
        const std::string to{ to_v->as_string() };

        std::shared_ptr<rtc::WebSocket> dest;
        {
            std::lock_guard lk{ mtx_ };
            auto it = storedNodes.find(to);
            if (it == storedNodes.end()) return;
            dest = it->second->connection;
        }
        dest->send(std::move(body));
    }

    std::mutex mtx_;
    std::deque<std::shared_ptr<rtc::WebSocket>> pendingCloseConnection;//TODO: Посмотреть: а надо ли
};