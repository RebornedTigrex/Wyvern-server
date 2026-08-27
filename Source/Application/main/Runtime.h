#pragma once

#include "rtc/rtc.hpp"
#include "Boost/asio.hpp"

#include <memory>
#include <iostream>
#include <functional>
#include <map>
#include <queue>
#include "boost/json.hpp"

//typedef Server;
//typedef Node;

enum class NodeActivityStatus {
    Online,
    Offline
};

struct Node {
    NodeActivityStatus status;

    std::shared_ptr<rtc::WebSocket> connection;
    std::deque<boost::json::object> pendingMessages;
};

using Server = rtc::WebSocketServer;


class RelayServer {

    std::optional<Server> serverInstance;

	std::unordered_map<std::string ,Node> storedNodes;

	bool storeDelayedMessage(Node, boost::json::object);
	bool changeNodeActivity(Node, NodeActivityStatus);

	bool isNodeActive(Node);

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

	bool setupServer(boost::asio::ip::address endpoint);
	void serverWorker();

	void onReqest(std::function<void>);

private:
    void onIncoming(std::shared_ptr<rtc::WebSocket> ws) {//TODO: Вынести в cpp, проверить код
        // handshake ещё не закончен
        ws->onOpen([this, ws] {
            const std::string id = normalize_path(ws->path().value_or(""));
            if (id.empty()) {
                ws->close();
                return;
            }
            {
                std::lock_guard lk{ mtx_ };
                if (auto it = storedNodes.find(id); it != storedNodes.end())
                    it->second.connection->close();//FIXME: Дедлок? Если да - вынести в pendingClose
                storedNodes[id].connection = ws;
            }
            });

        ws->onMessage([this, ws](rtc::message_variant msg) {
            if (!std::holds_alternative<rtc::string>(msg))
                return; // сигналинг — текст
            route(ws, std::get<rtc::string>(std::move(msg)));
            });

        ws->onClosed([this, ws] {
            std::lock_guard lk{ mtx_ };
            for (auto it = storedNodes.begin(); it != storedNodes.end(); ) {
                if (it->second.connection == ws) it = storedNodes.erase(it);
                else ++it;
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
            dest = it->second.connection;
        }
        dest->send(std::move(body));
    }

    static inline std::string normalize_path(std::string p) {
        if (!p.empty() && p.front() == '/') p.erase(p.begin());
        return p;
    }

    std::mutex mtx_;
    std::deque<std::shared_ptr<rtc::WebSocket>> pendingCloseConnection;


	std::shared_ptr<boost::asio::ip::tcp::acceptor> acceptor;
	boost::asio::io_context ioc;
	
};


class PeerWork {
	rtc::Configuration config;
	std::shared_ptr<rtc::PeerConnection> peerConnection;
	std::shared_ptr<rtc::DataChannel> dc;

	void setupRuntime();

	void startConnectionTo(std::string_view ipAddr);

	void loop();

public:
	PeerWork() : peerConnection(std::make_shared<rtc::PeerConnection>(config)) {
		setupRuntime();
	};

};

class SignalingServer {
	boost::asio::io_context ioc;
	std::shared_ptr<boost::asio::ip::tcp::acceptor> acceptor;

	void start();
public:
	SignalingServer() {};
};
